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
struct NodeInfo {
    NodeId id;
    IPEndpoint endpoint;
};

inline auto GetMessageType(const BenObject &msg) -> MessageType {
    auto y = msg["y"];
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

/**
 * @brief Check this message is query
 * 
 * @param msg 
 * @return BenObject 
 */
inline auto IsQueryMessage(const BenObject &msg) -> bool {
    return GetMessageType(msg) == MessageType::Query;
}
/**
 * @brief Check this message is reply
 * 
 * @param msg 
 * @return true 
 * @return false 
 */
inline auto IsReplyMessage(const BenObject &msg) -> bool {
    return GetMessageType(msg) == MessageType::Reply;
}
inline auto IsErrorMessage(const BenObject &msg) -> bool {
    return GetMessageType(msg) == MessageType::Error;
}

/**
 * @brief Get the any message Transaction Id object
 * 
 * @param msg 
 * @return std::string 
 */
inline auto GetMessageTransactionId(const BenObject &msg) -> std::string {
    return msg["t"].toString();
}
template <typename T>
inline auto GetMessageTransactionId(const BenObject &msg) -> T {
    auto str = msg["t"].toString();
    assert(str.size() == sizeof(T));
    return *reinterpret_cast<const T*>(str.c_str());
}

inline auto FillMessageTransactionId(BenObject &msg, std::string_view idStr) -> void {
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
        BenObject msg = BenDict();
        msg["t"] = transId;
        msg["y"] = "q";
        msg["q"] = "ping";

        msg["a"] = BenDict();
        msg["a"]["id"] = id.toStringView();
        return msg;
    }
    static auto fromMessage(const BenObject &msg) -> PingQuery {
        assert(IsQueryMessage(msg));
        auto id = msg["a"]["id"].toString();
        assert(id.size() == 20);
        auto nId = NodeId::from(id.data(), id.size());

        return {
            .transId = GetMessageTransactionId(msg),
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
        BenObject msg = BenDict();
        msg["t"] = transId;
        msg["y"] = "r";
        msg["r"] = BenDict();
        msg["r"]["id"] = id.toStringView();
        return msg;
    }
    static auto fromMessage(const BenObject &msg) -> PingReply {
        assert(IsReplyMessage(msg));
        auto id = msg["r"]["id"].toString();
        assert(id.size() == 20);
        auto nId = NodeId::from(id.data(), id.size());

        return {
            .transId = GetMessageTransactionId(msg),
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
        BenObject msg = BenDict();
        msg["t"] = transId;
        msg["y"] = "q";
        msg["q"] = "find_node";

        msg["a"] = BenDict();
        msg["a"]["id"] = id.toStringView();
        msg["a"]["target"] = targetId.toStringView();
        return msg;
    };
    static auto fromMessage(const BenObject &msg) -> FindNodeQuery {
        assert(IsQueryMessage(msg));
        auto id = msg["a"]["id"].toString();
        assert(id.size() == 20);
        auto nId = NodeId::from(id.data(), id.size());

        auto targetId = msg["a"]["target"].toString();
        assert(targetId.size() == 20);
        auto nTargetId = NodeId::from(targetId.data(), targetId.size());
        return {
            .transId = GetMessageTransactionId(msg),
            .id = nId,
            .targetId = nTargetId
        };
    }
};

struct FindNodeReply {
    std::string transId;
    NodeId   id; //< witch node give this reply
    std::vector<NodeInfo> nodes; //< Id: IP: Port

    auto operator <=>(const FindNodeReply &) const = default;

    auto toMessage() const -> BenObject {
        BenObject msg = BenDict();
        msg["t"] = transId;
        msg["y"] = "r";
        msg["r"] = BenDict();
        msg["r"]["id"] = id.toStringView();

        std::string nodesStr;
        for (auto &node : nodes) {
            nodesStr += node.id.toStringView();
            switch (node.endpoint.family()) {
                case AF_INET: {
                    auto addr4 = node.endpoint.address4();
                    nodesStr += reinterpret_cast<const char*>(&addr4);
                    break;
                }
                case AF_INET6: {
                    auto addr6 = node.endpoint.address6();
                    nodesStr += reinterpret_cast<const char*>(&addr6);
                    break;
                }
            }
            auto port = ::htons(node.endpoint.port());
            nodesStr += reinterpret_cast<const char*>(&port);
        }
        if (!nodesStr.empty()) {
            msg["r"]["nodes"] = nodesStr;
        }
        return msg;
    }
    static auto fromMessage(const BenObject &msg) -> FindNodeReply {
        assert(IsReplyMessage(msg));

        FindNodeReply reply;
        reply.transId = GetMessageTransactionId(msg);
        auto id = msg["r"]["id"].toString();
        assert(id.size() == 20);
        reply.id = NodeId::from(id.data(), id.size());

        // Parse response nodes
        auto &nodesObject = msg["r"]["nodes"];
        if (nodesObject.isString()) {
            auto &nodes = nodesObject.toString();
            for (size_t i = 0; i < nodes.size(); i += 26) {
                auto data = nodes.substr(i, 26);
                assert(data.size() == 26); //< NodeId + IP + Port
                auto id = NodeId::from(data.data(), 20);
                auto addr = IPAddress::fromRaw(data.data() + 20, 4);
                auto port = *reinterpret_cast<const uint16_t*>(data.data() + 24);
                // Convert port to host
                port = ntohs(port);
                reply.nodes.emplace_back(id, IPEndpoint(addr, port));
            }
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
        BenObject msg = BenDict();
        msg["t"] = transId;
        msg["y"] = "e";
        msg["e"] = BenObject {
            errorCode,
            error
        };
        return msg;
    };
    static auto fromMessage(const BenObject &msg) -> ErrorReply {
        assert(IsErrorMessage(msg));

        ErrorReply reply;
        reply.transId = GetMessageTransactionId(msg);
        reply.errorCode = msg["e"][0].toInt();
        reply.error = msg["e"][1].toString();
        return reply;
    }
};