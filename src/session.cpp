#include <ilias/task.hpp>
#include <random>
#include "session.hpp"

using namespace std::literals;

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
        // std::pair{"router.bittorrent.com", "6881"},
        std::pair{"dht.transmissionbt.com", "6881"},
        std::pair{"router.utorrent.com", "6881"}
    };
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
    // Do normal DHT management
    co_return;
}

auto DhtSession::onQuery(const BenObject &message, const IPEndpoint &from) -> IoTask<void> {
    DHT_LOG("Incoming query {} from {}", message, from);
    auto query = message["q"].toString();
    if (query == "ping") { // Give the pong back
        auto ping = PingQuery::fromMessage(message);
        mRoutingTable.updateNode({ping.id, from});
        auto reply = PingReply {
            .transId = ping.transId,
            .id = mId
        };
        auto encoded = reply.toMessage().encode();
        if (auto res = co_await mClient.sendto(makeBuffer(encoded), from); !res) {
            co_return unexpected(res.error());
        }
        co_return {};
    }
    else if (query == "find_node") {
        auto find = FindNodeQuery::fromMessage(message);
        auto nodes = mRoutingTable.findClosestNodes(find.targetId, 8);
        mRoutingTable.updateNode({find.id, from});
        if (nodes.empty()) {
            DHT_LOG("No nodes found for {}", find.targetId);
            co_return {}; // TODO: Maybe send a failure
        }
        auto reply = FindNodeReply {
            .transId = find.transId,
            .id = mId,
            .nodes = nodes
        };
        auto encoded = reply.toMessage().encode();
        if (auto res = co_await mClient.sendto(makeBuffer(encoded), from); !res) {
            co_return unexpected(res.error());
        }
        co_return {};
    }
    else if (query == "get_peers") {
        auto getPeers = GetPeersQuery::fromMessage(message);
        mRoutingTable.updateNode({getPeers.id, from});
        auto nodes = mRoutingTable.findClosestNodes(getPeers.infoHash, 8);
        if (nodes.empty()) {
            DHT_LOG("No nodes found for {}", getPeers.infoHash);
            co_return {}; // TODO: Maybe send a failure
        }
        auto reply = GetPeersReply {
            .transId = getPeers.transId,
            .id = mId,
            .nodes = nodes
        };
        auto encoded = reply.toMessage().encode();
        if (auto res = co_await mClient.sendto(makeBuffer(encoded), from); !res) {
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
    auto res = co_await (receiver.recv() | setTimeout(10s));
    if (!res) { // Timeout or another error, remove it in map
        mPendingQueries.erase(it);
    }
    co_return res;
}

auto DhtSession::findNode(const NodeId &target, const IPEndpoint &endpoint)
    -> IoTask<std::vector<NodeEndpoint>> 
{
    FindNodeEnv env;
    co_return co_await findNodeImpl(target, endpoint, 0, env);
}

auto DhtSession::findNode(const NodeId &target)
    -> IoTask<std::vector<NodeEndpoint>> 
{
    FindNodeEnv env;
    std::vector<NodeEndpoint> nodes = mRoutingTable.findClosestNodes(target, 8);
    std::vector<IoTask<std::vector<NodeEndpoint>> > tasks;
    for (const auto &node : nodes) {
        tasks.push_back(findNodeImpl(target, node.ip, 0, env));
    }
    auto vec = co_await whenAll(std::move(tasks));
    std::vector<NodeEndpoint> res;
    for (auto &v : vec) {
        if (v) {
            res.insert(res.end(), v->begin(), v->end());
        }
    }
    // Sort it by distance
    std::sort(res.begin(), res.end(), [&](const auto &a, const auto &b) {
        return a.id.distance(target) < b.id.distance(target);
    });
    // Trim if exceeds 8
    if (res.size() > KBUCKET_SIZE) {
        res.resize(KBUCKET_SIZE);
    }
    co_return res;
}

auto DhtSession::findNodeImpl(const NodeId &target, const IPEndpoint &endpoint, size_t depth, FindNodeEnv &env)
    -> IoTask<std::vector<NodeEndpoint> > 
{
    if (depth > 8) { // MAX_DEPTH ?
        DHT_LOG("Max depth reached, target {}, endpoint {}, depth {}", target, endpoint, depth);
        co_return unexpected(Error::Unknown);
    }
    FindNodeQuery query {
        .transId = allocateTransactionId(),
        .id = mId,
        .targetId = target
    };
    auto res = co_await sendKrpc(query.toMessage(), endpoint);
    if (!res) {
        co_return unexpected(res.error());
    }
    auto &[message, from] = *res;
    if (isErrorMessage(message)) {
        co_return unexpected(Error::Unknown); // TODO: Return by this error message
    }

    auto reply = FindNodeReply::fromMessage(message);
    mRoutingTable.updateNode({reply.id, from}); // This node give us reply, add it to routing table
    env.visited.insert({reply.id, from}); // Mark as visited
    std::sort(reply.nodes.begin(), reply.nodes.end(), [&](const auto &a, const auto &b) {
        return a.id.distance(target) < b.id.distance(target);
    }); // Sort by distance, first is the closest
    if (reply.nodes.empty()) {
        co_return unexpected(Error::Unknown); // TODO: Return by this error message
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
        if (nodeDis < curDis) {
            vec.emplace_back(id, ip);
        }
        else {
            DHT_LOG("Node {} is far than current closest node {}, distance: {} < {}", id, reply.id, nodeDis, curDis);
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
            DHT_LOG("Node {} is already visited", id);
            continue; // Skip it
        }
        scope.spawn([&, this]() -> Task<void> {
            auto res = co_await findNodeImpl(target, ip, depth + 1, env);
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
            std::sort(result.begin(), result.end(), [&](const auto &a, const auto &b) {
                return a.id.distance(target) < b.id.distance(target);
            });

            // Remove the far node
            if (result.size() > KBUCKET_SIZE) {
                result.resize(KBUCKET_SIZE);
            }

            // Check the first node is the target
            if (!result.empty() && result.front().id == target) {
                DHT_LOG("Found target node {}", target);
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
        std::sort(result.begin(), result.end(), [&](const auto &a, const auto &b) {
            return a.id.distance(target) < b.id.distance(target);
        });
        if (result.size() > KBUCKET_SIZE) {
            result.resize(KBUCKET_SIZE);
        }
    }
    co_return result;
}

auto DhtSession::bootstrap(const IPEndpoint &nodeIp) -> IoTask<void> {
    DHT_LOG("Bootstrap to {}", nodeIp);
    // std::mt19937_64 gen(std::random_device{}());
    // std::uniform_int_distribution<uint64_t> dis(1, 20);
    for (size_t i = 1; i < 20; i++) {
        auto res = co_await findNode(mId.randWithDistance(i), nodeIp);
        if (!res) {
            DHT_LOG("Bootstrap to {} failed: {}", nodeIp, res.error());
            co_return unexpected(res.error());
        }
    }
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
            DHT_LOG("DhtSession::processInput recvfrom failed: {}", res.error());
            break;
        }

        // Try parse to BenObject
        auto data = std::string_view(buffer, *res);
        auto message = BenObject::decode(data);
        if (message.isNull()) {
            continue;
        }
        auto type = getMessageType(message);
        auto id = getMessageTransactionId(message);

        // Dispatch
        if (type == MessageType::Reply || type == MessageType::Error) {
            auto it = mPendingQueries.find(id); //< Try find the query of the reply
            if (it == mPendingQueries.end()) {
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
    return std::string(reinterpret_cast<const char*>(&mTransactionId), sizeof(mTransactionId));
}
