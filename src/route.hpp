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
     * @brief Dump the routing table information to the console
     * 
     */
    auto dumpInfo() const -> void;
private:
    NodeId mId; //< The id of us
    std::array<KBucket, 160> mBuckets; //< The buckets [0] is the closest
    std::set<IPEndpoint> mIps;
};