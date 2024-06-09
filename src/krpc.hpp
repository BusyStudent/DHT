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
 * @return uint16_t 
 */
inline auto GetMessageTransactionId(const BenObject &msg) -> uint16_t {
    auto id = msg["t"].toString();
    assert(id.size() == 2);
    auto val = *reinterpret_cast<const uint16_t*>(id.data());
    return val;
}
inline auto FillMessageTransactionId(BenObject &msg, uint16_t id) -> void {
    auto idStr = BenObject::fromRawAsString(&id, sizeof(id));
    msg["t"] = idStr;
}

/**
 * @brief For ping query
 * 
 */
struct PingQuery {
    uint16_t transId = 0;
    NodeId   selfId;

    auto operator <=>(const PingQuery &) const = default;
    /**
     * @brief Make the query to the message
     * 
     * @return BenObject 
     */
    auto toMessage() const -> BenObject {
        BenObject msg = BenDict();
        msg["t"] = BenObject::fromRawAsString(&transId, sizeof(transId));
        msg["y"] = "q";
        msg["q"] = "ping";

        msg["a"] = BenDict();
        msg["a"]["id"] = selfId.toStringView();
        return msg;
    }
    static auto fromMessage(const BenObject &msg) -> PingQuery {
        assert(IsQueryMessage(msg));
        auto id = msg["a"]["id"].toString();
        assert(id.size() == 20);
        auto nId = NodeId::from(id.data(), id.size());

        return {
            .transId = GetMessageTransactionId(msg),
            .selfId = nId
        };
    }
};
/**
 * @brief The Ping Reply
 * 
 */
struct PingReply {
    uint16_t transId = 0;
    NodeId   peerId;

    auto operator <=>(const PingReply &) const = default;
    auto toMessage() const -> BenObject {
        BenObject msg = BenDict();
        msg["t"] = BenObject::fromRawAsString(&transId, sizeof(transId));
        msg["y"] = "r";
        msg["r"] = BenDict();
        msg["r"]["id"] = peerId.toStringView();
        return msg;
    }
    static auto fromMessage(const BenObject &msg) -> PingReply {
        assert(IsReplyMessage(msg));
        auto id = msg["r"]["id"].toString();
        assert(id.size() == 20);
        auto nId = NodeId::from(id.data(), id.size());

        return {
            .transId = GetMessageTransactionId(msg),
            .peerId = nId
        };
    }
};

struct FindNodeQuery {
    uint16_t transId = 0;
    NodeId   selfId;
    NodeId   targetId; //< Target NodeId

    auto operator <=>(const FindNodeQuery &) const = default;
    auto toMessage() const -> BenObject {
        BenObject msg = BenDict();
        msg["t"] = BenObject::fromRawAsString(&transId, sizeof(transId));
        msg["y"] = "q";
        msg["q"] = "find_node";

        msg["a"] = BenDict();
        msg["a"]["id"] = selfId.toStringView();
        msg["a"]["target"] = targetId.toStringView();
        return msg;
    };
};

struct FindNodeReply {
    uint16_t transId = 0;
    NodeId   targetId;
    std::vector<std::pair<NodeId, IPEndpoint> > nodes; //< Id: IP: Port

    auto operator <=>(const FindNodeReply &) const = default;
    static auto fromMessage(const BenObject &msg) -> FindNodeReply {
        assert(IsReplyMessage(msg));

        FindNodeReply reply;
        reply.transId = GetMessageTransactionId(msg);
        auto targetId = msg["r"]["id"].toString();
        assert(targetId.size() == 20);
        reply.targetId = NodeId::from(targetId.data(), targetId.size());

        // Parse response nodes
        for (auto &node : msg["r"]["nodes"].toList()) {
            auto &data = node.toString();
            assert(data.size() == 26); //< NodeId + IP + Port
            auto id = NodeId::from(data.data(), 20);
            auto addr = IPAddress::fromRaw(data.data() + 20, 4);
            auto port = *reinterpret_cast<const uint16_t*>(data.data() + 24);
            // Convert port to host
            port = ntohs(port);
            reply.nodes.emplace_back(id, IPEndpoint(addr, port));
        }
        return reply;
    }
};

struct ErrorReply {
    uint16_t transId = 0;
    int errorCode = 0;
    std::string error;

    auto operator <=>(const ErrorReply &) const = default;
    auto toMessage() const -> BenObject {
        BenObject msg = BenDict();
        msg["t"] = BenObject::fromRawAsString(&transId, sizeof(transId));
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