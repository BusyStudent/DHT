#include "route.hpp"

RoutingTable::RoutingTable(const NodeId &id) : mId(id) {

}

RoutingTable::~RoutingTable() {

}

auto RoutingTable::findBucketIndex(const NodeId &id) const -> size_t {
    size_t distance = mId.distance(id);
    return std::clamp<size_t>(distance, 0, 159); // Clamp the distance to the range of the buckets
}

auto RoutingTable::updateNode(const NodeEndpoint &endpoint) -> Status {
    Node node {
        .lastSeen = std::chrono::steady_clock::now(),
        .endpoint = endpoint,
        .state = Node::Good,
    };
    size_t idx = findBucketIndex(endpoint.id);
    auto &bucket = mBuckets[idx];
    auto &pending = bucket.pending;
    auto &nodes = bucket.nodes;
    auto it = std::find_if(nodes.begin(), nodes.end(), [&](const Node &n) {
        return n.endpoint == endpoint;
    });
    if (it == nodes.end() && nodes.size() >= KBUCKET_SIZE) {
        // Not Found and The bucket is full, check pending list
        if (pending.size() >= KBUCKET_SIZE) {
            // The pending list is full, drop the first one
            pending.pop_front();
        }
        pending.emplace_back(std::move(node));
        return Status::Pending;
    }
    bucket.lastUpdate = std::chrono::steady_clock::now();
    if (it != nodes.end()) {
        // The node already exists, update it
        it->lastSeen = node.lastSeen;
        return Status::Updated;

    }
    nodes.emplace_back(std::move(node));
    return Status::Added;
}

auto RoutingTable::findClosestNodes(const NodeId &id, size_t max) const -> std::vector<NodeEndpoint> {
    std::vector<NodeEndpoint> vec;

#if 1
    int64_t idx = findBucketIndex(id);
    int64_t step = idx > 80 ? -1 : 1; // Split the search to two parts
    while (vec.size() < max) {
        auto &bucket = mBuckets[idx];
        for (auto &node : bucket.nodes) {
            vec.push_back(node.endpoint);
            if (vec.size() >= max) {
                break;
            }
        }
        idx += step;
        if (idx < 0 || idx > 159) { //< End of the search
            break;
        }
    }
#else
    std::vector<std::pair<size_t, NodeEndpoint>> collected;
    for (auto &bucket : mBuckets) {
        for (auto &node : bucket.nodes) {
            collected.emplace_back(node.endpoint.id.distance(id), node.endpoint);
        }
    }
    std::sort(collected.begin(), collected.end(), [](const auto &a, const auto &b) {
        return a.first < b.first;
    });
    for (auto &node : collected) {
        vec.push_back(node.second);
        if (vec.size() >= max) {
            break;
        }
    }
#endif

    return vec;
}

auto RoutingTable::dumpInfo() -> void {

#if defined(__cpp_lib_format)
    std::string text;

    std::format_to(std::back_inserter(text), "Routing Table Info:\n");
    for (size_t i = 0; i < mBuckets.size(); ++i) {
        auto &bucket = mBuckets[i];
        if (!bucket.nodes.empty()) {
            std::format_to(std::back_inserter(text), "Bucket: idx {}, nodes: {}\n", i, bucket.nodes.size());
        }
        for (auto &node : bucket.nodes) {
            std::format_to(std::back_inserter(text), "  Node: {}\n", node.endpoint);
            // std::format_to(std::back_inserter(text), "  Last Seen: {}\n", node.lastSeen);
        }
        if (!bucket.pending.empty()) {
            std::format_to(std::back_inserter(text), "  Pending: {}\n", bucket.pending.size());
        }
        for (auto &node : bucket.pending) {
            std::format_to(std::back_inserter(text), "    Node: {}\n", node.endpoint);
        }
        // std::format_to(std::back_inserter(text), "Last Update: {}\n", bucket.lastUpdate);
    }

    ::fprintf(stderr, "%s", text.c_str());
    ::fflush(stderr);
#endif

}
