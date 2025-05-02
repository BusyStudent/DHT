#pragma once

#include "bencode.hpp"
#include "nodeid.hpp"
#include "net.hpp"
#include "log.hpp"
#include <cassert>
#include <optional>
#include <format>

enum class MessageType {
    Query,
    Reply,
    Error,
    Unknown,
};

template <typename T>
inline auto toCharArray(const T &t) -> std::array<char, sizeof(T)> {
    return std::bit_cast<std::array<char, sizeof(T)> >(t);
}

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
    DHT_LOG("Unknown message type: {}, from msg {}", y, msg);
    return MessageType::Unknown;
}

inline auto encodeIPEndpoint(const IPEndpoint &endpoint) -> std::string {
    std::string ret;
    switch (endpoint.family()) {
        case AF_INET: {
            auto addr = endpoint.address4();
            auto str = toCharArray(addr);
            ret.append(str.data(), str.size());
            break;
        }
        case AF_INET6: {
            auto addr = endpoint.address6();
            auto str = toCharArray(addr);
            ret.append(str.data(), str.size());
            break;
        }
        default: {
            assert(false);
        }
    }
    auto port = ::htons(endpoint.port());
    auto str = toCharArray(port);
    ret.append(str.begin(), str.end());
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

inline auto decodeNodes(std::string_view nodes) -> std::optional<std::vector<NodeEndpoint> > {
    // Check is V6 or v4
    if (nodes.size() % 26 == 0) {
        // IPv4
        std::vector<NodeEndpoint> ret;
        for (size_t i = 0; i < nodes.size(); i += 26) {
            auto data = nodes.substr(i, 26);
            if (data.size() != 26) {
                return std::nullopt;
            }
            auto id = NodeId::from(data.data(), 20);
            auto addr = IPAddress::fromRaw(data.data() + 20, 4);
            if (!addr) return std::nullopt;
            auto port = *reinterpret_cast<const uint16_t*>(data.data() + 24);
            port = ::ntohs(port);
            ret.emplace_back(id, IPEndpoint(addr.value(), port));
        }
        return ret;
    }
    else if (nodes.size() % 38 == 0) {
        // IPv6
        std::vector<NodeEndpoint> ret;
        for (size_t i = 0; i < nodes.size(); i += 38) {
            auto data = nodes.substr(i, 38);
            if (data.size() != 38) {
                return std::nullopt;
            }
            auto id = NodeId::from(data.data(), 20);
            auto addr = IPAddress::fromRaw(data.data() + 20, 16);
            if (!addr) return std::nullopt;
            auto port = *reinterpret_cast<const uint16_t*>(data.data() + 36);
            port = ::ntohs(port);
            ret.emplace_back(id, IPEndpoint(addr.value(), port));
        }
        return ret;
    }
    else {
        return std::nullopt;
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
    auto &t = msg["t"];
    if (!t.isString()) {
        DHT_LOG("Failed to get transaction id from message: {}", msg);
        return "";
    }
    return t.toString();
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
    static auto fromMessage(const BenObject &msg) -> std::optional<PingQuery> {
        try {
            if (!isQueryMessage(msg)) {
                return std::nullopt;
            }    
            auto id = msg["a"]["id"].toString();
            if (id.size() != 20) {
                return std::nullopt;
            }
            auto nId = NodeId::from(id.data(), id.size());
    
            return PingQuery {
                .transId = getMessageTransactionId(msg),
                .id = nId
            };    
        }
        catch (const std::exception &e) {
            DHT_LOG("Failed to parse ping query: {}, msg: {}", e.what(), msg);
            return std::nullopt;
        }
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
    static auto fromMessage(const BenObject &msg) -> std::optional<PingReply> {
        try {
            if (!isReplyMessage(msg)) {
                return std::nullopt;
            }
            auto id = msg["r"]["id"].toString();
            if (id.size() != 20) {
                return std::nullopt;
            }
            auto nId = NodeId::from(id.data(), id.size());
    
            return PingReply {
                .transId = getMessageTransactionId(msg),
                .id = nId
            };    
        }
        catch (const std::exception &e) {
            DHT_LOG("Failed to parse ping query: {}, msg: {}", e.what(), msg);
            return std::nullopt;
        }
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
    static auto fromMessage(const BenObject &msg) -> std::optional<FindNodeQuery> {
        try {
            if (!isQueryMessage(msg)) {
                return std::nullopt;
            }
            auto id = msg["a"]["id"].toString();
            if (id.size() != 20) {
                return std::nullopt;
            }
            auto targetId = msg["a"]["target"].toString();
            if (targetId.size() != 20) {
                return std::nullopt;
            }
            auto nId = NodeId::from(id.data(), id.size());
            auto nTargetId = NodeId::from(targetId.data(), targetId.size());
            return FindNodeQuery {
                .transId = getMessageTransactionId(msg),
                .id = nId,
                .targetId = nTargetId
            };
        }
        catch (const std::exception &e) {
            DHT_LOG("Failed to parse find_node query: {}, msg: {}", e.what(), msg);
            return std::nullopt;
        }
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
    static auto fromMessage(const BenObject &msg) -> std::optional<FindNodeReply> {
        try {
            if (!isReplyMessage(msg)) {
                return std::nullopt;
            }
            FindNodeReply reply;
            reply.transId = getMessageTransactionId(msg);
            auto id = msg["r"]["id"].toString();
            if (id.size() != 20) {
                return std::nullopt;
            }
            reply.id = NodeId::from(id.data(), id.size());

            if (auto &nodesObject = msg["r"]["nodes"]; nodesObject.isString()) {
                auto nodes = decodeNodes(nodesObject.toString());
                if (!nodes) {
                    return std::nullopt;
                }
                reply.nodes = std::move(*nodes);
            }
            else if (auto &nodesObject = msg["r"]["nodes6"]; nodesObject.isString()) {
                auto nodes = decodeNodes(nodesObject.toString());
                if (!nodes) {
                    return std::nullopt;
                }
                reply.nodes = std::move(*nodes);
            }
            return reply;
        }
        catch (const std::exception &e) {
            DHT_LOG("Failed to parse find_node reply: {}, msg: {}", e.what(), msg);
            return std::nullopt;
        }
    }
};

struct GetPeersQuery {
    std::string transId;
    NodeId   id; //< which node give this query
    InfoHash infoHash; //< target hash

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
    static auto fromMessage(const BenObject &msg) -> std::optional<GetPeersQuery> {
        try {
            if (!isQueryMessage(msg)) {
                return std::nullopt;
            }
            GetPeersQuery query;
            query.transId = getMessageTransactionId(msg);
            auto id = msg["a"]["id"].toString();
            auto infoHash = msg["a"]["info_hash"].toString();
            if (id.size() != 20 || infoHash.size() != 20) {
                return std::nullopt;
            }
            query.id = NodeId::from(id.data(), id.size());
            query.infoHash = NodeId::from(infoHash.data(), 20);
            return query;
        }
        catch (const std::exception &e) {
            DHT_LOG("Failed to parse get_peers query: {}, msg: {}", e.what(), msg);
            return std::nullopt;
        }
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
        if (!values.empty()) {
            auto list = BenObject::makeList();
            for (auto &value : values) {
                list.append(encodeIPEndpoint(value));
            }
            msg["r"]["values"] = list;
        }
        return msg;
    }
    static auto fromMessage(const BenObject &msg) -> std::optional<GetPeersReply> {
        try {
            if (!isReplyMessage(msg)) {
                return std::nullopt;
            }
            GetPeersReply reply;
            reply.transId = getMessageTransactionId(msg);
            auto id = msg["r"]["id"].toString();
            if (id.size() != 20) {
                return std::nullopt;
            }
            reply.id = NodeId::from(id.data(), id.size());
            reply.token = msg["r"]["token"].toString();
            if (auto &nodes = msg["r"]["nodes"]; nodes.isString()) {
                auto res = decodeNodes(nodes.toString());
                if (!res) return std::nullopt;
                reply.nodes = std::move(*res);
            }
            else if (auto &nodes = msg["r"]["nodes6"]; nodes.isString()) {
                auto res = decodeNodes(nodes.toString());
                if (!res) return std::nullopt;
                reply.nodes = std::move(*res);
            }
            else {
                // WTF?
            }
            if (auto &values = msg["r"]["values"]; values.isList()) {
                for (auto &value : values.toList()) {
                    reply.values.push_back(decodeIPEndpoint(value.toString()));
                }
            }
            return reply;
        }
        catch (const std::exception &e) {
            DHT_LOG("Failed to parse get_peers reply: {}, msg: {}", e.what(), msg);
            return std::nullopt;
        }
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
    static auto fromMessage(const BenObject &msg) -> std::optional<ErrorReply> {
        try {
            if (!isErrorMessage(msg)) {
                return std::nullopt;
            }
            ErrorReply reply;
            reply.transId = getMessageTransactionId(msg);
            reply.errorCode = msg["e"][0].toInt();
            reply.error = msg["e"][1].toString();
            return reply;
        }
        catch (const std::exception &e) {
            DHT_LOG("Failed to parse error reply: {}, msg: {}", e.what(), msg);
            return std::nullopt;
        }
    }
};

struct AnnouncePeerQuery {
    std::string transId;
    NodeId id;
    InfoHash infoHash;
    std::string token;
    uint16_t port = 0;
    bool impliedPort = true;

    auto toMessage() const -> BenObject {
        BenObject msg = BenObject::makeDict();
        msg["t"] = transId;
        msg["y"] = "q";
        msg["q"] = "announce_peer";
        msg["a"] = BenObject::makeDict();
        msg["a"]["id"] = id.toStringView();
        msg["a"]["info_hash"] = infoHash.toStringView();
        msg["a"]["token"] = token;
        msg["a"]["port"] = port;
        msg["a"]["implied_port"] = int(impliedPort);
        return msg;
    }

    static auto fromMessage(const BenObject &msg) -> std::optional<AnnouncePeerQuery> {
        try {
            if (!isQueryMessage(msg)) {
                return std::nullopt;
            }
            AnnouncePeerQuery query;
            query.transId = getMessageTransactionId(msg);
            auto id = msg["a"]["id"].toString();
            auto hash = msg["a"]["info_hash"].toString();
            if (id.size() != 20 || hash.size() != 20) {
                return std::nullopt;
            }
            query.id = NodeId::from(id.data(), id.size());
            query.infoHash = InfoHash::from(hash.data(), hash.size());
            query.token = msg["a"]["token"].toString();
            query.port = msg["a"]["port"].toInt();
            if (auto &impliedPort = msg["a"]["implied_port"]; impliedPort.isInt()) {
                query.impliedPort = (impliedPort.toInt() != 0);
            }
            return query;
        }
        catch (const std::exception &e) {
            DHT_LOG("Failed to parse announce_peer query: {}, msg: {}", e.what(), msg);
            return std::nullopt;
        }
    }
};

struct AnnouncePeerReply {
    std::string transId;
    NodeId id;

    auto toMessage() const -> BenObject {
        BenObject msg = BenObject::makeDict();
        msg["t"] = transId;
        msg["y"] = "r";
        msg["r"] = BenObject::makeDict();
        msg["r"]["id"] = id.toStringView();
        return msg;
    }
    static auto fromMessage(const BenObject &msg) -> std::optional<AnnouncePeerReply> {
        try {
            if (!isReplyMessage(msg)) {
                return std::nullopt;
            }
            AnnouncePeerReply reply;
            reply.transId = getMessageTransactionId(msg);
            auto id = msg["r"]["id"].toString();
            if (id.size() != 20) {
                return std::nullopt;
            }
            reply.id = NodeId::from(id.data(), id.size());
            return reply;
        }
        catch (const std::exception &e) {
            DHT_LOG("Failed to parse announce_peer reply: {}, msg: {}", e.what(), msg);
            return std::nullopt;
        }
    }
};

struct SampleInfoHashesQuery {
    std::string transId;
    NodeId id;
    NodeId target; // For forward compatibility (see BEP-0051) if the client node doesn support this, it should treat it as a find_node query, it has same meaning on find_node

    auto toMessage() const -> BenObject {
        BenObject msg = BenObject::makeDict();
        msg["t"] = transId;
        msg["y"] = "q";
        msg["q"] = "sample_infohashes";
        msg["a"] = BenObject::makeDict();
        msg["a"]["id"] = id.toStringView();
        msg["a"]["target"] = target.toStringView();
        return msg;
    }

    static auto fromMessage(const BenObject &msg) -> std::optional<SampleInfoHashesQuery> {
        try {
            if (!isQueryMessage(msg)) {
                return std::nullopt;
            }
            SampleInfoHashesQuery query;
            query.transId = getMessageTransactionId(msg);
            auto id = msg["a"]["id"].toString();
            auto target = msg["a"]["target"].toString();
            if (id.size() != 20 || target.size() != 20) {
                return std::nullopt;
            }
            query.id = NodeId::from(id.data(), id.size());
            query.target = NodeId::from(target.data(), target.size());
            return query;
        }
        catch (const std::exception &e) {
            DHT_LOG("Failed to parse sample_infohashes query: {}, msg: {}", e.what(), msg);
            return std::nullopt;
        }
    }
};

struct SampleInfoHashesReply {
    std::string transId;
    NodeId id;
    int interval; // The time in seconds the client should wait between sending queries (on seconds)
    std::vector<NodeEndpoint> nodes; // The closest nodes to the target
    int num; // The number of samples it have
    std::vector<InfoHash> samples; // The infohashes we sampled

    auto toMessage() const -> BenObject {
        BenObject msg = BenObject::makeDict();
        msg["t"] = transId;
        msg["y"] = "r";
        msg["r"] = BenObject::makeDict();
        msg["r"]["id"] = id.toStringView();
        msg["r"]["interval"] = interval;
        
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

        std::string samplesStr;
        for (auto &hash : samples) {
            samplesStr += hash.toStringView();
        }
        msg["r"]["samples"] = samplesStr;
        msg["r"]["num"] = num;
        return msg;
    }

    static auto fromMessage(const BenObject &msg) -> std::optional<SampleInfoHashesReply> {
        try {
            if (!isReplyMessage(msg)) {
                return std::nullopt;
            }
            SampleInfoHashesReply reply;
            
            // Commmon fields
            reply.transId = getMessageTransactionId(msg);
            auto id = msg["r"]["id"].toString();
            if (id.size() != 20) {
                return std::nullopt;
            }
            reply.id = NodeId::from(id.data(), id.size());
            if (auto &nodes = msg["r"]["nodes"]; nodes.isString()) {
                reply.nodes = decodeNodes(nodes.toString()).value();                
            }
            else if (auto &nodes = msg["r"]["nodes6"]; nodes.isString()) {
                reply.nodes = decodeNodes(nodes.toString()).value();
            }
            else {
                // WTF?
            }

            if (auto &interval = msg["r"]["interval"]; interval.isInt()) { // The peer understand this extension
                reply.interval = interval.toInt();
                auto num = msg["r"]["num"].toInt();
                auto samples = msg["r"]["samples"].toString();
                if (samples.size() % 20 != 0) { // Invalid number of samples
                    DHT_LOG("Invalid length of samples: {}", samples.size());
                    return std::nullopt;
                }
                for (size_t i = 0; i < samples.size() / 20; ++i) {
                    auto slice = std::string_view(samples).substr(i * 20, 20);
                    reply.samples.push_back(InfoHash::from(slice.data(), slice.size()));
                }
                reply.num = num;
            }
            return reply;
        }
        catch (const std::exception &e) {
            DHT_LOG("Failed to parse sample_infohashes reply: {}, msg: {}", e.what(), msg);
            return std::nullopt;
        }
    }
};
