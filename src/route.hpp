#pragma once

#include "net.hpp"
#include "nodeid.hpp"
#include "krpc.hpp"
#include <print>

#ifndef DHT_LOG
#define DHT_LOG(fmt, ...) std::println(stderr, "[DHT] " fmt, __VA_ARGS__)
#endif

// https://www.bittorrent.org/beps/bep_0005.html

constexpr size_t KBUCKET_SIZE = 8;

struct Node : NodeInfo {
    std::chrono::steady_clock::time_point lastSeen;
    bool good = true;
};

struct Bucket {
    std::vector<std::unique_ptr<Node> > nodes;
    std::chrono::steady_clock::time_point lastUpdated;

    /**
     * @brief Sort the bucket node by distance to the self
     * 
     */
    auto sort(const NodeId &self) -> void {
        std::sort(nodes.begin(), nodes.end(), [&](auto &a, auto &b) {
            return a->id.distance(self) < b->id.distance(self);
        });
    }
    auto find(const NodeId &id) -> Node * {
        auto iter = std::find_if(nodes.begin(), nodes.end(), [&](auto &node) {
            return node->id == id;
        });
        if (iter == nodes.end()) {
            return nullptr;
        }
        return iter->get();
    }
    /**
     * @brief Remove the node
     * 
     * @param id 
     */
    auto remove(const NodeId &id) -> void {
        auto iter = std::find_if(nodes.begin(), nodes.end(), [&](auto &node) {
            return node->id == id;
        });
        if (iter != nodes.end()) {
            nodes.erase(iter);
        }
    }
    auto removeBadNodes() -> void {
        for (auto iter = nodes.begin(); iter != nodes.end();) {
            auto &node = **iter;
            if (node.good) {
                iter++;
            }
            else {
                DHT_LOG("Removing bad node id: {}, endpoint: {}", node.id.toHex(), node.endpoint.toString());
                iter = nodes.erase(iter);
            }
        }
    }
    auto canInsert() const -> bool {
        return nodes.size() + 1 <= KBUCKET_SIZE;
    }
};

struct RoutingTable {
    RoutingTable(const NodeId &s) : self(s) {

    }
    /**
     * @brief Locate the node 
     * 
     * @param id 
     * @return Node* 
     */
    auto find(const NodeId &id) -> Node * {
        auto distance = self.distance(id);
        auto &bucket = buckets[std::min(distance, buckets.size() - 1)];
        return bucket.find(id);
    }
    /**
     * @brief Called on a node reply, if this id doesn't exists, it will be added to the table
     * 
     * @param id 
     * @param endpoint 
     * @return auto 
     */
    auto updateGoodNode(const NodeId &id, const IPEndpoint &endpoint) -> Node * {
        auto distance = self.distance(id);
        auto &bucket = buckets[std::min(distance, buckets.size() - 1)];
        auto node = bucket.find(id);
        if (!node) {
            // Insert
            if (!bucket.canInsert()) {
                // Discard node if bad
                bucket.removeBadNodes();
            }
            // Try again ?
            if (!bucket.canInsert()) {
                // Accoreding to the BEP, we should discard it
                DHT_LOG("too much node in bucket[{}], id: {} endpoint: {} discard", distance, id.toHex(), endpoint.toString());
                return nullptr;
            }
            auto newNode = std::make_unique<Node>();
            newNode->id = id;
            newNode->endpoint = endpoint;
            newNode->lastSeen = std::chrono::steady_clock::now();
            bucket.nodes.emplace_back(std::move(newNode));
            node = bucket.nodes.back().get();
            bucket.sort(self);

            DHT_LOG("Adding node id: {} in bucket[{}], endpoint: {}", node->id.toHex(), distance, node->endpoint.toString());
        }
        DHT_LOG("Updating node id: {} in bucket[{}], endpoint: {}", node->id.toHex(), distance,node->endpoint.toString());
        node->lastSeen = std::chrono::steady_clock::now();
        bucket.lastUpdated = std::chrono::steady_clock::now();
        return node;
    }
    /**
     * @brief Called when the node doesnot give us reply
     * 
     * @param id 
     * @return auto 
     */
    auto updateBadNode(const NodeId &id) -> void {
        auto node = find(id);
        if (node) {
            node->good = false;
        }
    }
    auto findClosestNodes(const NodeId &id) -> std::vector<NodeInfo> {
        auto distance = self.distance(id);
        auto &bucket = buckets[std::min(distance, buckets.size() - 1)];

        std::vector<NodeInfo> nodes;
        for (auto &node : bucket.nodes) {
            nodes.emplace_back(*node);
        }
        return nodes;
    }

    const NodeId       &self;
    std::array<Bucket, 160> buckets;
};