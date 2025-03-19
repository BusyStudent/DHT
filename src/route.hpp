#pragma once

#include "net.hpp"
#include "nodeid.hpp"
#include "krpc.hpp"
#include <algorithm>
#include <vector>
#include <chrono>
#include <format>
#include <deque>
#include <set>

// https://www.bittorrent.org/beps/bep_0005.html

constexpr size_t KBUCKET_SIZE = 8;

struct Node {
    enum State {
        Good,
        Questionable,
        Bad,
    };
    std::chrono::steady_clock::time_point lastSeen;
    NodeEndpoint endpoint;
    State state;
};

struct KBucket {
    std::chrono::steady_clock::time_point lastUpdate;
    std::vector<Node> nodes; //< The nodes in the bucket
    std::deque<Node> pending; //< The nodes that are pending to be added
};

/**
 * @brief The routing table
 * 
 */
class RoutingTable {
public:
    enum Status {
        Updated, // The node already exists and updated
        Added,   // The node is added to the bucket
        Pending, // The bucket is full, the node added to pending list
    };

    RoutingTable(const NodeId &id);
    RoutingTable(const RoutingTable &) = delete;
    ~RoutingTable();

    auto findBucketIndex(const NodeId &id) const -> size_t;

    /**
     * @brief Update or add a node in the routing table
     * 
     * @param node The endpoint of the node to be updated or added
     */
    auto updateNode(const NodeEndpoint &node) -> Status;

    /**
     * @brief Mark a node as bad, such as don't respond
     * 
     * @param node 
     */
    auto markBadNode(const NodeEndpoint &node) -> void;

    /**
     * @brief Find the closest node if us
     * 
     * 
     * @param id The NodeId to find the closest nodes to
     * @param max The max number of nodes to return
     * @return std::vector<NodeEndpoint> The list of closest nodes
     */
    auto findClosestNodes(const NodeId &id, size_t max = 1) const -> std::vector<NodeEndpoint>;

    /**
     * @brief Get the next node endpoint to refresh
     * 
     * @return std::optional<NodeEndpoint> 
     */
    auto nextRefresh() const -> std::optional<NodeEndpoint>;

    /**
     * @brief Dump the routing table information to the console
     * 
     */
    auto dumpInfo() const -> void;
private:
    auto translateTimepoint(std::chrono::steady_clock::time_point) const -> std::chrono::system_clock::time_point;

    NodeId mId; //< The id of us
    std::array<KBucket, 160> mBuckets; //< The buckets [0] is the closest
    std::set<IPEndpoint> mIps;

    // The time when the routing table is initialized, used to translate steady clock to system clock
    std::chrono::steady_clock::time_point mInitTime = std::chrono::steady_clock::now();
    std::chrono::system_clock::time_point mInitTimeSystem = std::chrono::system_clock::now();
};

template <>
struct std::formatter<Node::State> : std::formatter<std::string> {
    constexpr auto parse(std::format_parse_context &ctxt) const {
        return ctxt.begin();
    }
    auto format(const Node::State &state, auto &ctxt) const {
        std::string_view text = "Unknown";
        switch (state) {
            case Node::State::Good:
                text = "Good";
                break;
            case Node::State::Questionable:
                text = "Questionable";
                break;
            case Node::State::Bad:
                text = "Bad";
                break;
        }
        return std::format_to(ctxt.out(), "{}",text);
    }
};