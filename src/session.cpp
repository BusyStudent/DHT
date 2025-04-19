#include <ilias/task.hpp>
#include <random>
#include "session.hpp"

using namespace std::literals;

inline constexpr auto MAX_DEPTH = 20;
inline constexpr auto BFS_UNTIL = 8;

namespace node_utils {

/**
 * @brief Sort the vector of NodeEndpoint by the distance to the target and remove duplicates
 * 
 * @param vec 
 * @param target 
 * @return auto 
 */
auto sort(std::vector<NodeEndpoint> &vec, const NodeId &target) {
    std::sort(vec.begin(), vec.end(), [&target](const NodeEndpoint &a, const NodeEndpoint &b) {
        return a.id.distance(target) < b.id.distance(target);
    });
    vec.erase(std::unique(vec.begin(), vec.end()), vec.end());
}

}

DhtSession::DhtSession(IoContext &ctxt, const NodeId &id, const IPEndpoint &addr) : 
    mCtxt(ctxt), mScope(ctxt), mClient(ctxt, addr.family()), 
    mEndpoint(addr), mId(id), mRoutingTable(id)
{
    mClient.setOption(sockopt::ReuseAddress(true)).value();
    mClient.bind(addr).value();
}

DhtSession::~DhtSession() {
    mScope.cancel();
    mScope.wait();
}

auto DhtSession::run() -> Task<void> { 
    auto handle = mScope.spawn(&DhtSession::processInput, this);
    const auto bootstrapNodes = {
        std::pair{"router.bittorrent.com", "6881"},
        std::pair{"dht.transmissionbt.com", "6881"},
        std::pair{"router.utorrent.com", "6881"}
    };
    if (!mSkipBootstrap) {
        bool booststraped = false;
        for (const auto [host, port] : bootstrapNodes) {
            addrinfo_t hints {
                .ai_family = mEndpoint.family()
            };
            auto info = co_await AddressInfo::fromHostnameAsync(host, port, hints);
            if (!info) {
                continue;
            }
            for (auto &endpoint : info->endpoints()) {
                if (auto ret = co_await bootstrap(endpoint); ret) {
                    booststraped = true;
                    break;
                }
            }
            if (booststraped) {
                break;
            }
        }
        if (!booststraped) {
            DHT_LOG("Failed to bootstrap");
            co_return;
        }
    }
    // Do normal DHT management
    mScope.spawn(cleanupPeersThread());
    mScope.spawn(refreshTableThread());
    mScope.spawn(randomSearchThread());
    // Join the scope
    co_await mScope;
    co_return;
}

auto DhtSession::onQuery(const BenObject &message, const IPEndpoint &from) -> IoTask<void> {
    DHT_LOG("Incoming query {} from {}", message, from);
    auto query = message["q"].toString();
    if (query == "ping") { // Give the pong back
        auto ping = PingQuery::fromMessage(message);
        if (!ping) {
            DHT_LOG("Invalid ping query");
            co_return {};
        }
        mRoutingTable.updateNode({ping->id, from});
        auto reply = PingReply {
            .transId = ping->transId,
            .id = mId
        };
        auto encoded = reply.toMessage().encode();
        if (auto res = co_await mClient.sendto(ilias::makeBuffer(encoded), from); !res) {
            co_return unexpected(res.error());
        }
        co_return {};
    }
    else if (query == "find_node") {
        auto find = FindNodeQuery::fromMessage(message);
        if (!find) {
            DHT_LOG("Invalid find node query");
            co_return {};
        }
        auto nodes = mRoutingTable.findClosestNodes(find->targetId, 8);
        mRoutingTable.updateNode({find->id, from});
        if (nodes.empty()) {
            DHT_LOG("No nodes found for {}", find->targetId);
        }
        auto reply = FindNodeReply {
            .transId = find->transId,
            .id = mId,
            .nodes = nodes
        };
        auto encoded = reply.toMessage().encode();
        if (auto res = co_await mClient.sendto(ilias::makeBuffer(encoded), from); !res) {
            co_return unexpected(res.error());
        }
        co_return {};
    }
    else if (query == "get_peers") {
        auto getPeers = GetPeersQuery::fromMessage(message);
        if (!getPeers) {
            DHT_LOG("Invalid get peers query");
            co_return {};
        }
        mRoutingTable.updateNode({getPeers->id, from});
        auto nodes = mRoutingTable.findClosestNodes(getPeers->infoHash, 8);
        if (nodes.empty()) {
            DHT_LOG("No nodes found for {}", getPeers->infoHash);
            co_return {}; // TODO: Maybe send a failure
        }
        auto reply = GetPeersReply {
            .transId = getPeers->transId,
            .id = mId,
            .token = "token", // TODO: Generate a token
            .nodes = nodes
        };
        // Find the peers and push them to the reply
        auto it = mPeers.find(getPeers->infoHash);
        if (it != mPeers.end()) {
            auto &[_, set] = *it;
            for (auto &peer : set) {
                reply.values.push_back(peer);
                if (reply.values.size() >= 100) { // TOO MANY PEERS
                    break;
                }
            }
            // If bigger than 8 random peers, send the reply
            if (reply.values.size() >= KBUCKET_SIZE) {
                std::shuffle(reply.values.begin(), reply.values.end(), mRandom);
            }
        }
        auto encoded = reply.toMessage().encode();
        if (auto res = co_await mClient.sendto(ilias::makeBuffer(encoded), from); !res) {
            co_return unexpected(res.error());
        }
        co_return {};
    }
    else if (query == "announce_peer") {
        // TODO: Record it
        auto announce = AnnouncePeerQuery::fromMessage(message);
        if (!announce) {
            DHT_LOG("Invalid announce peer query");
            co_return {};
        }
        DHT_LOG("Announce peer infoHash {} from {}", announce->infoHash, from);
        if (mOnAnnouncePeer) {
            mOnAnnouncePeer(announce->infoHash, from);
        }
        mPeers[announce->infoHash].insert(from);
        mRoutingTable.updateNode({announce->id, from});
        auto reply = AnnouncePeerReply {
            .transId = announce->transId,
            .id = mId
        };
        auto encoded = reply.toMessage().encode();
        if (auto res = co_await mClient.sendto(ilias::makeBuffer(encoded), from); !res) {
            co_return unexpected(res.error());
        }
        co_return {};
    }
    // Finally, if we don't know the query, we send an error
    DHT_LOG("Unknown query {}", query);
    auto error = ErrorReply {
        .transId = getMessageTransactionId(message),
        .errorCode = 204,
        .error = "Method Unknown"
    };
    auto encoded = error.toMessage().encode();
    if (auto res = co_await mClient.sendto(makeBuffer(encoded), from); !res) {
        co_return unexpected(res.error());
    }
    co_return {};
}

auto DhtSession::sendKrpc(const BenObject &message, const IPEndpoint &endpoint)
    -> IoTask<std::pair<BenObject, IPEndpoint> > 
{
    auto content = message.encode();
    auto id = getMessageTransactionId(message);
    auto [sender, receiver] = oneshot::channel<std::pair<BenObject, IPEndpoint> >();
    auto [it, emplace] = mPendingQueries.try_emplace(id, std::move(sender));
    if (!emplace) {
        DHT_LOG("Exisiting id in queries ?, may bug");
        // Raise the debugger
#if defined(_MSC_VER)
        __debugbreak();
#else
        asm("int3");
#endif
    }
    // Send it
    if (auto res = co_await mClient.sendto(makeBuffer(content), endpoint); !res) {
        co_return unexpected(res.error());
    }
    auto res = co_await (receiver.recv() | setTimeout(mTimeout));
    if (!res) { // Timeout or another error, remove it in map
        // mPendingQueries.erase(it);
        mPendingQueries.erase(id); // More safe ?
    }
    co_return res;
}

auto DhtSession::findNode(const NodeId &target, const IPEndpoint &endpoint)
    -> IoTask<std::vector<NodeEndpoint>> 
{
    FindNodeEnv env;
    co_return co_await findNodeImpl(target, std::nullopt, endpoint, 0, env);
}

auto DhtSession::findNode(const NodeId &target)
    -> IoTask<std::vector<NodeEndpoint>> 
{
    FindNodeEnv env;
    std::vector<NodeEndpoint> nodes = mRoutingTable.findClosestNodes(target, 8);
    std::vector<IoTask<std::vector<NodeEndpoint>> > tasks;
    for (const auto &node : nodes) {
        tasks.push_back(findNodeImpl(target, node.id, node.ip, 0, env));
    }
    auto vec = co_await whenAll(std::move(tasks));
    std::vector<NodeEndpoint> res;
    for (auto &v : vec) {
        if (v) {
            res.insert(res.end(), v->begin(), v->end());
        }
    }
    // Sort it by distance
    node_utils::sort(res, target);
    // Trim if exceeds 8
    if (res.size() > KBUCKET_SIZE) {
        res.resize(KBUCKET_SIZE);
    }
    co_return res;
}

auto DhtSession::ping(const IPEndpoint &nodeIp) -> IoTask<NodeId> {
    PingQuery query {
        .transId = allocateTransactionId(),
        .id = mId
    };
    auto res = co_await sendKrpc(query.toMessage(), nodeIp);
    if (!res) {
        co_return unexpected(res.error());
    }
    auto &[message, from] = *res;
    if (isErrorMessage(message)) {
        co_return unexpected(KrpcError::RpcErrorMessage);
    }
    auto reply = PingReply::fromMessage(message);
    if (!reply) {
        co_return unexpected(KrpcError::BadReply);
    }
    co_return reply->id;
}

auto DhtSession::routingTable() const -> const RoutingTable & {
    return mRoutingTable;
}

auto DhtSession::routingTable() -> RoutingTable & {
    return mRoutingTable;
}

auto DhtSession::peers() const -> const std::map<InfoHash, std::set<IPEndpoint>> & {
    return mPeers;
}

auto DhtSession::setOnAnouncePeer(
    std::function<void(const InfoHash &hash, const IPEndpoint &peer)> callback)
    -> void {
  mOnAnnouncePeer = std::move(callback);
}

auto DhtSession::sampleInfoHashes(const IPEndpoint &nodeIp)
    -> IoTask<std::vector<InfoHash> > 
{
    SampleInfoHashesQuery query {
        .transId = allocateTransactionId(),
        .id = mId,
        .target = NodeId::rand()
    };
    auto res = co_await sendKrpc(query.toMessage(), nodeIp);
    if (!res) {
        co_return unexpected(res.error());
    }
    auto &[message, from] = *res;
    if (isErrorMessage(message)) {
        co_return unexpected(KrpcError::RpcErrorMessage);
    }
    auto reply = SampleInfoHashesReply::fromMessage(message);
    if (!reply) {
        co_return unexpected(KrpcError::BadReply);
    }
    co_return reply->samples;
}

auto DhtSession::findNodeImpl(const NodeId &target, std::optional<NodeId> id, const IPEndpoint &endpoint, size_t depth, FindNodeEnv &env)
    -> IoTask<std::vector<NodeEndpoint> > 
{
    if (depth > MAX_DEPTH) { // MAX_DEPTH ?
        DHT_LOG("Max depth reached, target {}, endpoint {}, depth {}", target, endpoint, depth);
        co_return unexpected(Error::Unknown);
    }
    DHT_LOG("Find node {}, endpoint {}, depth {}", target, endpoint, depth);
    FindNodeQuery query {
        .transId = allocateTransactionId(),
        .id = mId,
        .targetId = target
    };
    auto res = co_await sendKrpc(query.toMessage(), endpoint);
    if (!res) {
        if (id) { // If the id is known, try to mark it as bad node in routing table
            mRoutingTable.markBadNode({*id, endpoint});
        }
        co_return unexpected(res.error());
    }
    auto &[message, from] = *res;
    if (isErrorMessage(message)) {
        co_return unexpected(KrpcError::RpcErrorMessage);
    }

    auto replyParsed = FindNodeReply::fromMessage(message);
    if (!replyParsed) { // Failed to parse
        co_return unexpected(KrpcError::BadReply);
    }
    auto reply = std::move(*replyParsed);
    mRoutingTable.updateNode({reply.id, from}); // This node give us reply, add it to routing table
    env.visited.insert({reply.id, from}); // Mark as visited
    
    // Sort by distance, first is the closest
    node_utils::sort(reply.nodes, target);
    
    if (reply.nodes.empty()) {
        co_return unexpected(KrpcError::TargetNotFound);
    }
    if (reply.nodes.front().id == target) { // Got the target node
        co_return reply.nodes;
    }
    if (!env.closest || env.closest->distance(target) > reply.id.distance(target)) {
        // No closest node or the closest node is farther than the reply's node
        env.closest = reply.id;
    }

    // Remove the node far than the reply's node
    std::vector<NodeEndpoint> vec;
    for (auto &[id, ip] : reply.nodes) {
        assert(env.closest); // Must have a closest node
        auto nodeDis = id.distance(target); // The node distance to target
        auto curDis = env.closest->distance(target); // The closest node distance to target
        if (nodeDis < curDis || depth <= BFS_UNTIL) { // Until reach BFS_UNTIL depth we do BFS
            vec.emplace_back(id, ip);
        }
        else {
            DHT_LOG("Node {} is far than current closest node {}, distance: {} > {}, depth: {}", id, reply.id, nodeDis, curDis, depth);
        }
    }
    if (vec.empty()) {
        // No node closer than the reply's node, return the reply's node
        co_return reply.nodes;
    }

    // Begin DFS Search
    std::vector<NodeEndpoint> result;
    auto scope = co_await TaskScope::make();
    for (auto &[id, ip] : vec) {
        auto [it, emplace] = env.visited.emplace(id, ip);
        if (!emplace) { // Already visited
            // DHT_LOG("Node {} is already visited", id);
            continue; // Skip it
        }
        scope.spawn([&, this]() -> Task<void> {
            auto res = co_await findNodeImpl(target, id, ip, depth + 1, env);
            if (!res) {
                if (res.error() == Error::Canceled) {
                    scope.cancel();
                }
                co_return;
            }
            // Merge the result
            for (auto &node : *res) {
                result.push_back(node);
            }

            // Sort it again
            node_utils::sort(result, target);

            // Check the first node is the target
            if (!result.empty() && result.front().id == target) {
                DHT_LOG("Found target node {}, in depth", target, depth + 1);
                scope.cancel();
            }
        });
    }
    co_await scope; // Join all tasks
    if (result.size() < KBUCKET_SIZE) {
        // Merge the result with the reply's node
        for (auto &[id, ip] : reply.nodes) {
            result.push_back({id, ip});
        }
        node_utils::sort(result, target);
        if (result.size() > KBUCKET_SIZE) {
            result.resize(KBUCKET_SIZE);
        }
    }
    else if (result.size() > KBUCKET_SIZE) {
        result.resize(KBUCKET_SIZE);
    }
    co_return result;
}

auto DhtSession::bootstrap(const IPEndpoint &nodeIp) -> IoTask<void> {
    DHT_LOG("Bootstrap to {}", nodeIp);
    auto res = co_await findNode(mId, nodeIp);
    if (!res) {
        DHT_LOG("Bootstrap to {} failed: {}", nodeIp, res.error());
        co_return unexpected(res.error());
    }
    for (size_t i = 10; i < 150; i += 20) { // Try 10, 40, 60, 80, 100, 120, 140, 150, walkthrough the whole address space
        auto res = co_await findNode(mId.randWithDistance(i));
        if (!res) {
            DHT_LOG("Bootstrap to {} failed: {}", nodeIp, res.error());
            co_return unexpected(res.error());
        }
    }
    mRoutingTable.dumpInfo();
    DHT_LOG("Bootstrap to {} success", nodeIp);
    co_return {};
}

auto DhtSession::processInput() -> Task<void> { 
    DHT_LOG("DhtSession::processInput start");
    char buffer[65535];
    IPEndpoint endpoint;
    while (true) {
        auto res = co_await mClient.recvfrom(makeBuffer(buffer), endpoint);
        if (!res) {
            if (res.error() != Error::Canceled) {
                DHT_LOG("DhtSession::processInput recvfrom failed: {}", res.error());
            }
            break;
        }

        // Try parse to BenObject
        auto data = std::string_view(buffer, *res);
        auto message = BenObject::decode(data);
        if (message.isNull()) {
            DHT_LOG("DhtSession::processInput parse message failed: from endpoint {}", endpoint);
            continue;
        }
        auto type = getMessageType(message);
        auto id = getMessageTransactionId(message);

        // Dispatch
        if (type == MessageType::Reply || type == MessageType::Error) {
            auto it = mPendingQueries.find(id); //< Try find the query of the reply
            if (it == mPendingQueries.end()) {
                ILIAS_LOG("DhtSession::processInput unknown reply: {} from endpoint {}, no pending query matched", message);
                continue;
            }
            auto sender = std::move(it->second);
            mPendingQueries.erase(it);
            sender.send(std::pair{std::move(message), endpoint});
            continue;
        }
        if (type == MessageType::Query) {
            // Handle query
            if (!co_await onQuery(message, endpoint)) {
                break;
            }
        }
    }
}

auto DhtSession::allocateTransactionId() -> std::string {
    if (mTransactionId == UINT16_MAX) {
        mTransactionId = 0;
    }
    mTransactionId += 1;
    auto id = std::bit_cast<std::array<char, sizeof(mTransactionId)> >(mTransactionId);
    return std::string(id.begin(), id.end());
}

auto DhtSession::cleanupPeersThread() -> Task<void> { 
    while (true) {
        auto res = co_await sleep(mCleanupInterval);
        if (!res) {
            DHT_LOG("DhtSession::cleanupPeersThread request quit");
            break;
        }
        mPeers.clear();
        DHT_LOG("DhtSession::cleanupPeersThread clear peers");
    }
}

auto DhtSession::refreshTableThread() -> Task<void> { 
    while (true) {
        if (auto res = co_await sleep(mRefreshInterval); !res) {
            DHT_LOG("DhtSession::refreshTableThread request quit");
            break;
        }
        auto node = mRoutingTable.nextRefresh();
        if (!node) {
            continue;
        }
        // Send ping request
        auto res = co_await ping(node->ip);
        if (!res && res.error() == Error::Canceled) {
            DHT_LOG("DhtSession::refreshTableThread request quit");
            break;
        }
        if (!res) {
            DHT_LOG("DhtSession::refreshTableThread send ping request to {} failed: {}", *node, res.error());
            mRoutingTable.markBadNode(*node);
            continue;
        }
        if (*res != node->id) {
            DHT_LOG("DhtSession::refreshTableThread send ping request to {} failed: id mismatch", *node);
            mRoutingTable.markBadNode(*node);
            continue;
        }
        mRoutingTable.updateNode(*node);
        DHT_LOG("DhtSession::refreshTableThread send ping request to {} success", *node);
    }
}

auto DhtSession::randomSearchThread() -> Task<void> { 
    while (true) {
        if (auto res = co_await sleep(mRandomSearchInterval); !res) { // Do random search every 5 minutes
            DHT_LOG("DhtSession::randomSearchThread request quit");
            break;
        }
        auto res = co_await findNode(NodeId::rand());
        if (!res && res.error() == Error::Canceled) {
            DHT_LOG("DhtSession::randomSearchThread request quit");
            break;
        }
        if (!res) {
            DHT_LOG("DhtSession::randomSearchThread find node failed: {}", res.error());
            continue;
        }
        DHT_LOG("DhtSession::randomSearchThread done random search");
    }
}
