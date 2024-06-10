#pragma once

#include <functional>
#include <chrono>
#include <vector>
#include "route.hpp"
#include "krpc.hpp"
#include "net.hpp"

/**
 * @brief Session for provide dht service
 * 
 */
class DhtSession {
public:
    DhtSession(IoContext &ctxt);
    DhtSession(const DhtSession &) = delete;
    ~DhtSession();

    auto setBindEndpoint(const IPEndpoint &endpoint) -> void;
    auto setNodeId(const NodeId &id) -> void;
    auto start() -> void;
private:
    auto _run() -> Task<>;
    auto _bootstrap() -> Task<>;
    /**
     * @brief send ping query to the target
     * 
     * @param where 
     * @param id 
     * @return auto 
     */
    auto _ping(NodeId node);
    /**
     * @brief send find node query to ip with the targetNodeId
     * 
     * @param nodeIp
     * @param target
     * @param curDistance 
     * @return Task<NodeInfo> finded closest node 
     */
    auto _sendFindNode(const IPEndpoint &nodeIp, const NodeId &targetNodeId, size_t curDistance = 160) -> Task<NodeInfo>;
    /**
     * @brief On the ping query sended to us
     * 
     * @param msg 
     * @return Task<> 
     */
    auto _onPing(const BenObject &msg) -> Task<>;
    /**
     * @brief On the find node query sended to us
     * 
     * @param msg 
     * @return Task<> 
     */
    auto _onFindNode(const BenObject &msg) -> Task<>;
    /**
     * @brief Alloc a transcation id
     * 
     * @return uint16_t 
     */
    auto _allocTranscationId() -> uint16_t;
    /**
     * @brief Send krpc to the target and wait for the result
     * 
     * @param ip 
     * @param msg 
     * @return Task<BenObject> 
     */
    auto _doKrpc(const IPEndpoint &ip, const BenObject &msg) -> Task<BenObject>;

    IoContext &mCtxt;
    UdpClient mClient4;

    // Config
    NodeId mSelfId = NodeId::rand();
    IPEndpoint mEndpoint = "0.0.0.0:0"; //< Endpoint for bind

    // KBucket
    RoutingTable mRoutingTable {mSelfId};

    // Process incoming query table
    std::map<
        std::string, 
        std::function<Task<> (const BenObject &msg)>,
        std::less<>
    > mIncomingTable {
        {"ping", std::bind(&DhtSession::_onPing, this, std::placeholders::_1)},
        {"find_node", std::bind(&DhtSession::_onFindNode, this, std::placeholders::_1)},
    };

    // Process incoming reply table
    std::map<
        uint16_t, //< Transaction Id
        Sender<BenObject>
    > mIncomingReplyTable;
    uint16_t mTransactionId = 0;

    // Bootstrap
    std::vector<IPEndpoint> mBootstrapNodes {
        {IPAddress::fromHostname("router.bittorrent.com"), 6881},
        {IPAddress::fromHostname("dht.transmissionbt.com"), 6881},
        {IPAddress::fromHostname("router.utorrent.com"), 6881},
        {IPAddress::fromHostname("dht.libtorrent.org"), 6881},
        {IPAddress::fromHostname("dht.aelitis.com"), 6881},
    };
};

inline DhtSession::DhtSession(IoContext &ctxt) : mCtxt(ctxt), mClient4(ctxt, AF_INET) {

}

inline DhtSession::~DhtSession() {
    mClient4.close();
}

inline auto DhtSession::start() -> void {
    auto ok = mClient4.bind(mEndpoint);
    DHT_LOG("Bind on {}", mEndpoint.toString());
    if (!ok) {
        DHT_LOG("Error for {}", ok.error().toString());
        return;
    }
    // Start the worker
    ilias_go _run();
    ilias_go _bootstrap();
}
inline auto DhtSession::setBindEndpoint(const IPEndpoint &ep) -> void {
    mEndpoint = ep;
}
inline auto DhtSession::setNodeId(const NodeId &id) -> void {
    mSelfId = id;
}

inline auto DhtSession::_run() -> Task<> {
    DHT_LOG("Start recvform");
while (true) {
    char buffer[1024] {0};
    auto ret = co_await mClient4.recvfrom(buffer, sizeof(buffer));
    if (!ret) {
        co_return {};
    }
    auto [size, endpoint] = *ret;
    // Parse it
    auto msg = BenObject::decode(buffer, size);
    if (msg.isNull()) {
        continue;
    }

    // Get the type
    switch (GetMessageType(msg)) {
        case MessageType::Query: {
            // Incoming Query
            DHT_LOG("Got query from {}, transId {}", endpoint.toString(), GetMessageTransactionId(msg));
            auto iter = mIncomingTable.find(msg["y"].toString());
            if (iter != mIncomingTable.end()) {
                co_await iter->second(msg);
            }
            break;
        }
        case MessageType::Reply: {
            // Our query's reply
            DHT_LOG("Got reply from {}, transId {}", endpoint.toString(), GetMessageTransactionId(msg));
            auto transId = GetMessageTransactionId(msg);
            auto iter = mIncomingReplyTable.find(transId);
            if (iter != mIncomingReplyTable.end()) {
                co_await iter->second.send(msg);
            }
            break;
        }
        case MessageType::Error: {
            // ?
            auto reply = ErrorReply::fromMessage(msg);
            // std::cout << reply.errorCode << ':' << reply.error << std::endl;
            DHT_LOG("Got error reply from {}, errorCode {}, error {}", endpoint.toString(), reply.errorCode, reply.error);
            break;
        }
    }
}
}

inline auto DhtSession::_allocTranscationId() -> uint16_t {
    while(true) {
        mTransactionId = (mTransactionId + 1) % 0xFFFF;
        if (!mIncomingReplyTable.contains(mTransactionId)) {
            return mTransactionId;
        }
    }
}
inline auto DhtSession::_doKrpc(const IPEndpoint &endpoint, const BenObject &msg) -> Task<BenObject> {
    auto transId = GetMessageTransactionId(msg);
    auto encoded = msg.encode();
    DHT_LOG("Send krpc to {}, transId {}", endpoint.toString(), transId);
    auto ret = co_await mClient4.sendto(encoded.data(), encoded.size(), endpoint);
    if (!ret) {
        co_return Unexpected(ret.error());
    }

    // Register the channel
    auto [tx, rx] = Channel<BenObject>::make();
    mIncomingReplyTable[transId] = tx;
    // Wait the channel
    auto [val, timeout] = co_await (rx.recv() || Sleep(std::chrono::seconds(15)));
    // Remove it
    mIncomingReplyTable.erase(transId);
    if (val) {
        co_return val.value();
    }
    DHT_LOG("Krpc to {}, transId {} was timedout", endpoint.toString(), transId);
    co_return Unexpected(Error::TimedOut);
}

// --- do query
inline auto DhtSession::_bootstrap() -> Task<> {
    auto id = NodeId::rand();
    for (const auto &ep : mBootstrapNodes) {
        DHT_LOG("Bootstrap by {}", ep.toString());
        auto node = co_await _sendFindNode(ep, id);
        if (!node) {
            DHT_LOG("Bootstrap by {} failed", ep.toString());
            continue;
        }
    }
}
inline auto DhtSession::_sendFindNode(const IPEndpoint &nodeIp, const NodeId &targetNodeId, size_t curDistance) -> Task<NodeInfo> {
    FindNodeQuery query {
        .transId = _allocTranscationId(),
        .id = mSelfId,
        .targetId = targetNodeId
    };
    auto ret = co_await _doKrpc(nodeIp, query.toMessage());
    if (!ret) {
        // Doesn't reply, bad node
        co_return Unexpected(ret.error());
    }
    auto reply = FindNodeReply::fromMessage(ret.value());
    // Oh, this node reply, add to good node
    mRoutingTable.updateGoodNode(reply.id, nodeIp);

    // Let us check is more closer?
    size_t distance = 160;
    std::optional<NodeInfo> closest;
    for (auto nodeInfo : reply.nodes) {
        if (nodeInfo.id == mSelfId) {
            continue;
        }
        size_t d = nodeInfo.id.distance(targetNodeId);
        if (d < distance) {
            distance = d;
            closest = nodeInfo;
        }
    }
    if (closest && closest->id == targetNodeId) {
        // Got target
        co_return *closest;
    }
    if (distance >= curDistance) {
        // This search doesnot make it closer
        co_return *closest;
    }
    // Begin DPS
    for (auto nodeInfo : reply.nodes) {
        if (nodeInfo.id == mSelfId) {
            continue;
        }
        auto ret = co_await _sendFindNode(nodeInfo.endpoint, targetNodeId, distance);
        if (!ret) {
            // Bad node, doesn't reply
            continue;
        }
        if (ret->id == targetNodeId) {
            // Got
            co_return *ret;
        }
        auto newDistance = ret->id.distance(targetNodeId);
        if (newDistance < distance) {
            // Got closer
            distance = newDistance;
            closest = ret.value();
        }
    }
    if (closest) {
        co_return closest.value();
    }
    co_return Unexpected(Error::Unknown);
}

// --- handle query
inline auto DhtSession::_onPing(const BenObject &msg) -> Task<> {
    co_return {};
}

inline auto DhtSession::_onFindNode(const BenObject &msg) -> Task<> {
    co_return {};
}