#pragma once

#include "bencode.hpp"
#include "nodeid.hpp"
#include "net.hpp"
#include <cassert>

enum class MessageType {
    Query,
    Reply,
    Error,
    Unknown,
};
struct NodeEndpoint {
    NodeId id;
    IPEndpoint ip;

    auto operator ==(const NodeEndpoint &other) const {
        return id == other.id && ip == other.ip;
    }

    auto operator !=(const NodeEndpoint &other) const {
        return !(*this == other);
    }

    auto operator <(const NodeEndpoint &other) const {
        return id < other.id;
    }
};

inline auto getMessageType(const BenObject &msg) -> MessageType {
    const auto &y = msg["y"];
    if (y == "q") {
        return MessageType::Query;
    } 
    else if (y == "r") {
        return MessageType::Reply;
    }
    else if (y == "e") {
        return MessageType::Error;
    }
    return MessageType::Unknown;
}

inline auto encodeIPEndpoint(const IPEndpoint &endpoint) -> std::string {
    std::string ret;
    switch (endpoint.family()) {
        case AF_INET: {
            auto addr = endpoint.address4();
            ret.append(reinterpret_cast<const char*>(&addr), sizeof(addr));
            break;
        }
        case AF_INET6: {
            auto addr = endpoint.address6();
            ret.append(reinterpret_cast<const char*>(&addr), sizeof(addr));
            break;
        }
        default: {
            assert(false);
        }
    }
    auto port = ::htons(endpoint.port());
    ret.append(reinterpret_cast<const char*>(&port), sizeof(port));
    return ret;
}

inline auto decodeIPEndpoint(std::string_view endpoint) -> IPEndpoint {
    // Adress : Port
    IPAddress address;
    if (endpoint.size() == sizeof(::in_addr)) {
        address = IPAddress::fromRaw(endpoint.data(), sizeof(::in_addr)).value();
    }
    else if (endpoint.size() == sizeof(::in6_addr)) {
        address = IPAddress::fromRaw(endpoint.data(), sizeof(::in6_addr)).value();
    }
    else {
        assert(false);
    }
    auto port = *reinterpret_cast<const uint16_t*>(endpoint.data() + address.length());
    return IPEndpoint(address, ::ntohs(port));
}

inline auto decodeIPEndpoints(std::string_view endpoints) -> std::vector<IPEndpoint> {
    std::vector<IPEndpoint> ret;
    // Check is V6 or v4
    size_t stride = 0;
    if (endpoints.size() % 6 == 0) {
        stride = 6; // IPv4
    }
    else if (endpoints.size() % 18 == 0) {
        stride = 18; // IPv6
    }
    else {
        assert(false);
    }
    for (size_t i = 0; i < endpoints.size(); i += stride) {
        ret.emplace_back(decodeIPEndpoint(endpoints.substr(i, stride)));
    }
    return ret;
}

inline auto decodeNodes(std::string_view nodes) -> std::vector<NodeEndpoint> {
    // Check is V6 or v4
    if (nodes.size() % 26 == 0) {
        // IPv4
        std::vector<NodeEndpoint> ret;
        for (size_t i = 0; i < nodes.size(); i += 26) {
            auto str = nodes.substr(i, 20);
            auto id = NodeId::from(str.data(), str.size());
            auto ip = decodeIPEndpoint(nodes.substr(i + 20, 6));
            ret.emplace_back(id, ip);
        }
        return ret;
    }
    else if (nodes.size() % 38 == 0) {
        // IPv6
        std::vector<NodeEndpoint> ret;
        for (size_t i = 0; i < nodes.size(); i += 38) {
            auto str = nodes.substr(i, 32);
            auto id = NodeId::from(str.data(), str.size());
            auto ip = decodeIPEndpoint(nodes.substr(i + 32, 6));
            ret.emplace_back(id, ip);
        }
        return ret;
    }
    else {
        assert(false);
    }
}

/**
 * @brief Check this message is query
 * 
 * @param msg 
 * @return BenObject 
 */
inline auto isQueryMessage(const BenObject &msg) -> bool {
    return getMessageType(msg) == MessageType::Query;
}

/**
 * @brief Check this message is reply
 * 
 * @param msg 
 * @return true 
 * @return false 
 */
inline auto isReplyMessage(const BenObject &msg) -> bool {
    return getMessageType(msg) == MessageType::Reply;
}

inline auto isErrorMessage(const BenObject &msg) -> bool {
    return getMessageType(msg) == MessageType::Error;
}

/**
 * @brief Get the any message Transaction Id object
 * 
 * @param msg 
 * @return std::string 
 */
inline auto getMessageTransactionId(const BenObject &msg) -> std::string {
    return msg["t"].toString();
}

template <typename T>
inline auto getMessageTransactionId(const BenObject &msg) -> T {
    auto str = msg["t"].toString();
    assert(str.size() == sizeof(T));
    return *reinterpret_cast<const T*>(str.c_str());
}

inline auto fillMessageTransactionId(BenObject &msg, std::string_view idStr) -> void {
    msg["t"] = idStr;
}

/**
 * @brief For ping query
 * 
 */
struct PingQuery {
    std::string transId;
    NodeId   id;

    auto operator <=>(const PingQuery &) const = default;
    /**
     * @brief Make the query to the message
     * 
     * @return BenObject 
     */
    auto toMessage() const -> BenObject {
        BenObject msg = BenObject::makeDict();
        msg["t"] = transId;
        msg["y"] = "q";
        msg["q"] = "ping";

        msg["a"] = BenObject::makeDict();
        msg["a"]["id"] = id.toStringView();
        return msg;
    }
    static auto fromMessage(const BenObject &msg) -> PingQuery {
        assert(isQueryMessage(msg));
        auto id = msg["a"]["id"].toString();
        assert(id.size() == 20);
        auto nId = NodeId::from(id.data(), id.size());

        return {
            .transId = getMessageTransactionId(msg),
            .id = nId
        };
    }
};
/**
 * @brief The Ping Reply
 * 
 */
struct PingReply {
    std::string transId;
    NodeId   id;

    auto operator <=>(const PingReply &) const = default;
    auto toMessage() const -> BenObject {
        BenObject msg = BenObject::makeDict();
        msg["t"] = transId;
        msg["y"] = "r";
        msg["r"] = BenObject::makeDict();
        msg["r"]["id"] = id.toStringView();
        return msg;
    }
    static auto fromMessage(const BenObject &msg) -> PingReply {
        assert(isReplyMessage(msg));
        auto id = msg["r"]["id"].toString();
        assert(id.size() == 20);
        auto nId = NodeId::from(id.data(), id.size());

        return {
            .transId = getMessageTransactionId(msg),
            .id = nId
        };
    }
};

struct FindNodeQuery {
    std::string transId;
    NodeId   id; //< witch node send this query
    NodeId   targetId; //< Target NodeId

    auto operator <=>(const FindNodeQuery &) const = default;
    auto toMessage() const -> BenObject {
        BenObject msg = BenObject::makeDict();
        msg["t"] = transId;
        msg["y"] = "q";
        msg["q"] = "find_node";

        msg["a"] = BenObject::makeDict();
        msg["a"]["id"] = id.toStringView();
        msg["a"]["target"] = targetId.toStringView();
        return msg;
    };
    static auto fromMessage(const BenObject &msg) -> FindNodeQuery {
        assert(isQueryMessage(msg));
        auto id = msg["a"]["id"].toString();
        assert(id.size() == 20);
        auto nId = NodeId::from(id.data(), id.size());

        auto targetId = msg["a"]["target"].toString();
        assert(targetId.size() == 20);
        auto nTargetId = NodeId::from(targetId.data(), targetId.size());
        return {
            .transId = getMessageTransactionId(msg),
            .id = nId,
            .targetId = nTargetId
        };
    }
};

struct FindNodeReply {
    std::string transId;
    NodeId   id; //< witch node give this reply
    std::vector<NodeEndpoint> nodes; //< Id: IP: Port

    auto operator <=>(const FindNodeReply &) const = default;

    auto toMessage() const -> BenObject {
        BenObject msg = BenObject::makeDict();
        msg["t"] = transId;
        msg["y"] = "r";
        msg["r"] = BenObject::makeDict();
        msg["r"]["id"] = id.toStringView();

        std::string nodesStr;
        bool v6 = false;
        for (auto &node : nodes) {
            nodesStr += node.id.toStringView();
            nodesStr += encodeIPEndpoint(node.ip);
            v6 = v6 || node.ip.family() == AF_INET6;
        }
        if (!nodesStr.empty()) {
            if (v6) {
                msg["r"]["nodes6"] = nodesStr;
            }
            else {
                msg["r"]["nodes"] = nodesStr;
            }
        }
        return msg;
    }
    static auto fromMessage(const BenObject &msg) -> FindNodeReply {
        assert(isReplyMessage(msg));

        FindNodeReply reply;
        reply.transId = getMessageTransactionId(msg);
        auto id = msg["r"]["id"].toString();
        assert(id.size() == 20);
        reply.id = NodeId::from(id.data(), id.size());

        // Parse response nodes
        if (auto &nodesObject = msg["r"]["nodes"]; nodesObject.isString()) {
            auto &nodes = nodesObject.toString();
            for (size_t i = 0; i < nodes.size(); i += 26) {
                auto data = nodes.substr(i, 26);
                assert(data.size() == 26); //< NodeId + IP + Port
                auto id = NodeId::from(data.data(), 20);
                auto addr = IPAddress::fromRaw(data.data() + 20, 4).value();
                auto port = *reinterpret_cast<const uint16_t*>(data.data() + 24);
                // Convert port to host
                port = ntohs(port);
                reply.nodes.emplace_back(id, IPEndpoint(addr, port));
            }
        }
        else if (auto &nodesObject = msg["r"]["nodes6"]; nodesObject.isString()) {
            auto &nodes = nodesObject.toString();
            for (size_t i = 0; i < nodes.size(); i += 38) {
                auto data = nodes.substr(i, 38);
                assert(data.size() == 38); //< NodeId + IP + Port
                auto id = NodeId::from(data.data(), 20);
                auto addr = IPAddress::fromRaw(data.data() + 20, 16).value();
                auto port = *reinterpret_cast<const uint16_t*>(data.data() + 36);
                // Convert port to host
                port = ntohs(port);
                reply.nodes.emplace_back(id, IPEndpoint(addr, port));
            }
        }
        else {
            // WTF?
            assert(false);
        }
        return reply;
    }
};

struct GetPeersQuery {
    std::string transId;
    NodeId   id; //< which node give this query
    NodeId   infoHash; //< target hash

    auto operator <=>(const GetPeersQuery &) const = default;

    auto toMessage() const -> BenObject {
        BenObject msg = BenObject::makeDict();
        msg["t"] = transId;
        msg["y"] = "q";
        msg["q"] = "get_peers";

        msg["a"] = BenObject::makeDict();
        msg["a"]["id"] = id.toStringView();
        msg["a"]["info_hash"] = infoHash.toStringView();
        return msg;
    }
    static auto fromMessage(const BenObject &msg) -> GetPeersQuery {
        assert(isQueryMessage(msg));

        GetPeersQuery query;
        query.transId = getMessageTransactionId(msg);
        auto id = msg["a"]["id"].toString();
        assert(id.size() == 20);
        auto infoHash = msg["a"]["info_hash"].toString();
        assert(infoHash.size() == 20);
        query.id = NodeId::from(id.data(), id.size());
        query.infoHash = NodeId::from(infoHash.data(), 20);
        return query;
    }
};

struct GetPeersReply {
    std::string transId;
    NodeId   id; //< which node give this reply
    std::string token;
    std::vector<NodeEndpoint> nodes; //< Id: IP: Port
    std::vector<IPEndpoint> values; //< Peers ip:port

    auto operator <=>(const GetPeersReply &) const = default;
    auto toMessage() const -> BenObject {
        BenObject msg = BenObject::makeDict();
        msg["t"] = transId;
        msg["y"] = "r";
        msg["r"] = BenObject::makeDict();
        msg["r"]["id"] = id.toStringView();
        msg["r"]["token"] = token;

        std::string nodesStr;
        bool v6 = false;
        for (auto &node : nodes) {
            nodesStr += node.id.toStringView();
            nodesStr += encodeIPEndpoint(node.ip);
            v6 = v6 || node.ip.family() == AF_INET6;
        }
        if (!nodesStr.empty()) {
            if (v6) {
                msg["r"]["nodes6"] = nodesStr;
            }
            else {
                msg["r"]["nodes"] = nodesStr;                
            }
        }
        std::string valueStr;
        for (auto &value : values) {
            valueStr += encodeIPEndpoint(value);
        }
        if (!valueStr.empty()) {
            msg["r"]["values"] = valueStr;
        }
        return msg;
    }
    static auto fromMessage(const BenObject &msg) -> GetPeersReply {
        assert(isReplyMessage(msg));

        GetPeersReply reply;
        reply.transId = getMessageTransactionId(msg);
        auto id = msg["r"]["id"].toString();
        assert(id.size() == 20);
        reply.id = NodeId::from(id.data(), id.size());
        reply.token = msg["r"]["token"].toString();
        auto nodesStr = msg["r"]["nodes"].toString();
        auto valuesStr = msg["r"]["values"].toString();
        if (!nodesStr.empty()) {
            reply.nodes = decodeNodes(nodesStr);
        }
        if (!valuesStr.empty()) {
            reply.values = decodeIPEndpoints(valuesStr);
        }
        return reply;
    }
};

struct ErrorReply {
    std::string transId;
    int errorCode = 0;
    std::string error;

    auto operator <=>(const ErrorReply &) const = default;
    auto toMessage() const -> BenObject {
        BenObject msg = BenObject::makeDict();
        msg["t"] = transId;
        msg["y"] = "e";
        msg["e"] = BenObject {
            errorCode,
            error
        };
        return msg;
    };
    static auto fromMessage(const BenObject &msg) -> ErrorReply {
        assert(isErrorMessage(msg));

        ErrorReply reply;
        reply.transId = getMessageTransactionId(msg);
        reply.errorCode = msg["e"][0].toInt();
        reply.error = msg["e"][1].toString();
        return reply;
    }
};

template <>
struct std::formatter<NodeEndpoint> {
    auto parse(std::format_parse_context &ctxt) const {
        return ctxt.begin();
    }

    auto format(const NodeEndpoint &endpoint, std::format_context &ctxt) const {
        return std::format_to(ctxt.out(), "{}:{}", endpoint.id, endpoint.ip);
    }
};