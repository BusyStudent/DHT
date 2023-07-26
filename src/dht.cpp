#include <optional>
#include <charconv>
#include <iostream>
#include <format>
#include <bitset>
#include <bit>
#include "dht.hpp"
#include "sha1.h"


#if !defined(NDEBUG)
#define LOG(what, ...) std::cout << std::format(what, __VA_ARGS__) << std::endl;
#else
#define LOG(what, ...)
#endif

using namespace std::chrono_literals;

static constexpr std::pair<const char*, uint16_t> bootstrap_nodes [] = {
    {"router.bittorrent.com", 6881},
    {"dht.transmissionbt.com", 6881},
    {"router.utorrent.com", 6881}
};
static constexpr auto tid_len = 2;
static constexpr auto node_id_len = 20;

static std::string sha1(std::string_view text) {
    char digest[20]; 
    ::SHA1(digest, text.data(), text.size());
    return std::string(digest, 20);
}
static std::string tohex(std::string_view text) {
    std::string hex;
    hex.resize(text.length() * 2);

    for (int i = 0; i < text.length(); i++) {
        sprintf(hex.data() + (2 * i), "%02x", text[i] & 0xff);
    }
    return hex;
}
static std::string random_string(size_t n) {
    ::srand(::time(nullptr));
    std::string ret;
    for (auto i = 0; i < n; i++) {
        ret.push_back(::rand() % 26 + 'a');
    }
    return ret;
}

// K Parts
KID      KID::Random() {
    return sha1(random_string(20));
}

KBucket *KTable::find_bucket(const KID &target_id) {
    auto ret = local_id ^ target_id;
    auto distance = ret.count_leading_zero();
    // LOG("Find bucket by {}, xor disntance {}", tohex(target_id.to_string()), distance);

    if (distance == 160) {
        // AS Same as self
        return nullptr;
    }
    return &buckets[distance];
}
KNode    *KTable::find_closest_node(const KID &target_id) {
    auto bucket = find_bucket(target_id);
    if (!bucket) {
        return nullptr;
    }
    KNode *cur = nullptr;
    for (auto &node : *bucket) {
        if (cur != nullptr) {
            if ((cur->id ^ target_id) < (node.id ^ target_id)) {
                // Current is closer
                continue;
            }
        }
        cur = &node;
    }
    return cur;
}
KNode    *KTable::find_node(const KID &target_id) {
    auto bucket = find_bucket(target_id);
    if (!bucket) {
        return nullptr;
    }
    for (auto &node : *bucket) {
        if (node.id == target_id) {
            return &node;
        }
    }
    return nullptr;
}
KNode     *KTable::alloc_node(const KID &id) {
    auto bucket = find_bucket(id);
    if (!bucket) {
        return nullptr;
    }
    if (bucket->nodes.size() == bucket->max_node_count) {
        // Max remove oldest
        bucket->nodes.pop_front();
    }
    else {
        nodes_count += 1;
    }
    bucket->nodes.push_back(KNode {
        .id = id
    });
    return &bucket->nodes.back();
}
bool       KTable::remove_node(const KID &id) {
    auto bucket = find_bucket(id);
    if (!bucket) {
        return false;
    }
    for (auto iter = bucket->begin(); iter != bucket->end(); ++iter) {
        if (iter->id == id) {
            bucket->nodes.erase(iter);
            nodes_count -= 1;
            return true;
        }
    }
    return false;
}
void       KTable::log_info() const {
    int n = 0;
    for (auto &bucket : buckets) {
        if (!bucket.empty()) {
            LOG("[KTable] bucket({}), node count {}", n, bucket.nodes.size());
            for (auto &node : bucket.nodes) {
                LOG("    Node: {} endpoint {}", tohex(node.id), node.endpoint.to_string());
            }
        }
        n += 1;
    }
}

// Task parts
RpcManager::RpcManager(TimerService &s) : service(s) {
    service.add_timer(1000ms, &RpcManager::check_timeout, this, (1000ms).count());
}
RpcManager::~RpcManager() {

}
void RpcManager::check_timeout(int ms) {
    for (auto iter = tasks.begin(); iter != tasks.end(); ) {
        auto &[tid, data] = *iter;
        data.left_ms -= ms;
        if (data.left_ms <= 0) {
            // Timeout 
            if (data.timeout) {
                data.timeout();
            }
            iter = tasks.erase(iter);
        }
        else {
            ++iter;
        }
    }
}
bool RpcManager::process(const BenObject &message, const IPEndpoint &endpoint) {
    auto tid = message["t"].to_string();
    auto iter = tasks.find(tid);
    if (iter == tasks.end()) {
        return false;
    }
    auto &[t, data] = *iter;
    data.process(message, endpoint);

    // Erase 
    tasks.erase(tid);
    return true;
}
std::pair<std::string, Task*> RpcManager::alloc_task() {
    std::string id;
    do {
        id = std::string(reinterpret_cast<char*>(cur_tid), 2);
        if (cur_tid[0] == UINT8_MAX) {
            cur_tid[0] = 0;
            if (cur_tid[1] == UINT_MAX) {
                // Almost full, back
                cur_tid[1] = 0;
            }
            else {
                cur_tid[1]++;
            }
        }
        else {
            cur_tid[0]++;
        }
    }
    while (tasks.find(id) != tasks.end());

    auto task = &tasks[id];
    return std::make_pair(id, task);
}

// DHT Parts
DHTClient::DHTClient() {
    sock = Socket(AF_INET, SOCK_DGRAM);
    if (sock.bad()) {
        LOG("Failed to create socket socket {}", Illias::GetLastError().message());
    }
    if (!sock.bind(IPEndpoint("0.0.0.0", 10086))) {
        LOG("Failed to bind socket socket {}", Illias::GetLastError().message());
    }
    if (!sock.setblocking(false)) {
        LOG("Failed to ioctl socket {}", Illias::GetLastError().message());
    }
    local_endpoint = sock.local_endpoint();

    pfd.fd = sock.fd();
    pfd.events = POLLIN;
    pfd.revents = 0;

    // local_id = "ukqxfyewiicoybhfvmuw";
    local_id = KID("ukqxfyewiicoybhfvmnv");
    table.set_local_id(local_id);

    LOG("Listen on {}", local_endpoint.to_string());
    LOG("Local id  {}", local_id.to_string());
}
DHTClient::~DHTClient() {

}
void DHTClient::run() {
    bootstrap();
    service.single_shot(5s, &DHTClient::on_periodic_search_node, this);
    service.add_timer(5min, &DHTClient::on_peridoic_ping_node, this);
    while (true) {
        auto n = Illias::Poll(&pfd, 1, 10); //< Wait for 10MS
        if (n == -1) {
            // Error
            return;
        }
        if (n == 0) {
            // Process timer callback
            service.process();
            continue;
        }
        if (pfd.revents & POLLIN) {
            recv_data();
        }
    }
}
void DHTClient::bootstrap() {
    for (const auto [domain, port] : bootstrap_nodes) {
        send_find_node(IPEndpoint::Parse(domain, port), local_id, KID::Random());
    }
}
void DHTClient::recv_data() {
    char buf[1024 * 12];
    while (true) {
        auto [num, endpoint] = sock.recvform(buf, sizeof(buf));
        if (num < 0) {
            // No data
            break;
        }
        auto object = BenObject::Parse(std::string_view(buf, num));
        if (!object.is_dictionary()) {
            break;
        }
        on_message(object, endpoint);
    }
}
void DHTClient::on_message(const BenObject &message, const IPEndpoint &ep) {
    if (!message.contains("t") || !message.contains("y")) {
        return;
    }
    auto t = message["t"].to_string();
    auto y = message["y"].to_string();
    if (y == "r") {
        // Reponse
        LOG("Response from node {}, ip {}, t {}", tohex(message["r"]["id"].to_string()), ep.to_string(), tohex(t));
        if (!manager.process(message, ep)) {
            LOG("Unknown tid {}", tohex(t));
        }
    }
    else if (y == "q") {
        // Request
        auto q = message["q"].to_string();
        if (q == "get_peers") {
            on_get_peers_request(message, ep);
        }
        else if (q == "announce_peer") {
            on_announce_peer_request(message, ep);
        }
        else if (q == "ping") {
            on_ping_request(message, ep);
        } 
    }
    else if (y == "e") {
        // Error
        // switch (message["e"].to_integer()) {

        // }
        LOG("Error {}:{} from ip {}", message["e"][0].to_integer(), message["e"][1].to_string(), ep.to_string());
    }
    else {
        // Unknown
        LOG("Unknown message from ip {}", ep.to_string());
    }
}
void DHTClient::on_find_node_response(const BenObject &message, const IPEndpoint &ep) {
    if (!message["r"].contains("nodes")) {
        LOG("Bad find node response from {}", ep.to_string());
        return;
    }
    auto nodes = message["r"]["nodes"].to_string();
    if (nodes.size() % 26 != 0) {
        return;
    }
    // node_id :in_addr : uinr16 port
    static_assert(sizeof(::in_addr) + sizeof(uint16_t) + 20 == 26);
    for (int i = 0;i <  nodes.size() / 26; i++) {
        std::string part = nodes.substr(i * 26, 26);
        std::string nid  = part.substr(0, 20); //< 
        std::string addr = part.substr(20, 4);
        std::string port = part.substr(24, 2);

        IPEndpoint endpoint(*reinterpret_cast<::in_addr*>(addr.data()),
                            ::ntohs(*reinterpret_cast<::uint16_t*>(port.data()))
                    );
        // Try find any exist node
        KID nodeid = nid;
        if (table.contains(nodeid)) {
            // LOG("Already contains node {}, skip", tohex(nid));
            continue;
        }

#if     1
        // We try to ping it, ok for puting it to idx
        auto [tid, task] = manager.alloc_task();
        auto msg = make_ping_message(tid);
        send_krpc(msg, endpoint);

        task->process = [this, nodeid, endpoint](const BenObject &, const IPEndpoint &) {
            table.insert(nodeid, endpoint);
        };
#else
        table.insert(nodeid, endpoint);
#endif

    }
}
void DHTClient::on_get_peers_request(const BenObject &message, const IPEndpoint &ep) {

}
void DHTClient::on_announce_peer_request(const BenObject &message, const IPEndpoint &ep) {
    auto infohash = message["a"]["infohash"].to_string();
    auto id = message["a"]["id"].to_string();

    std::cout << "Torrent: " << infohash << " from" << tohex(id) << std::endl;
}
void DHTClient::on_ping_request(const BenObject &message, const IPEndpoint &ep) {
    LOG("[Client] Ping from node {} ip {}", tohex(message["a"]["id"].to_string()), ep.to_string());

    auto msg = BenObject::NewDictionary();
    msg["t"] = message["t"];
    msg["y"] = "r";
    msg["r"] = BenObject::NewDictionary();
    msg["r"]["id"] = local_id.to_string();

    send_krpc(msg, ep);
}
void DHTClient::send_krpc(const BenObject &message, const IPEndpoint &ep) {
    LOG("[Client] Sending KRPC to {} tid: {}", ep.to_string(), tohex(message["t"].to_string()));
    auto data = message.encode();
    auto buf = data.c_str();
    ssize_t left = data.size();

    while (left > 0) {
        auto ret = sock.sendto(buf, left, 0, ep);
        if (ret == -1) {
            // Error
            LOG("Failed to send to {}, reason {}", ep.to_string(), Illias::GetLastError().message());
            return;
        }
        left -= ret;
        buf += ret;
    }
}
void DHTClient::send_find_node(const IPEndpoint &endpoint, const KID &id, const KID &target) {
    // Make Find node request
    auto msg = BenObject::NewDictionary();
    auto [tid, task] = manager.alloc_task();

    msg["t"] = tid;
    msg["y"] = "q";
    msg["q"] = "find_node";
    msg["a"] = BenObject::NewDictionary();

    // Fill body
    msg["a"]["id"] = id.to_string();
    msg["a"]["target"] = target.to_string();

    send_krpc(msg, endpoint);

    task->timeout = [this, id, endpoint]() {
        LOG("[Client] Send find node to {}, timeout", endpoint.to_string());
    };
    task->process = [this](const BenObject &msg, const IPEndpoint &endpoint) {
        on_find_node_response(msg, endpoint);
    };
}
void DHTClient::send_ping(const KNode &node) {
    auto [tid, task] = manager.alloc_task();

    send_krpc(make_ping_message(tid), node.endpoint);

    task->process = [this](const BenObject &msg, const IPEndpoint &endpoint) {
        // on_ping_response(msg, endpoint);
        // Nothing, just OK
    };
    task->timeout = [this, id = node.id]() {
        LOG("Ping {} failed, removeing", tohex(id));
        table.remove_node(id);
    };
}
void DHTClient::on_periodic_search_node() {
    if (table.size() == 0) {
        // Current does not has node,
        bootstrap();
        service.single_shot(5s, &DHTClient::on_periodic_search_node, this);
        return;
    }
    for (auto &bucket : table) {
        for (auto &node : bucket) {
            send_find_node(node.endpoint, local_id.make_neighbor(node.id), KID::Random());
        }
    }
    service.single_shot(std::chrono::seconds(std::min<int>(table.size(), 60)), &DHTClient::on_periodic_search_node, this);
}
void DHTClient::on_peridoic_ping_node() {
    table.log_info();
    for (auto &bucket : table) {
        for (auto &node : bucket) {
            send_ping(node);
        }
    }
}

BenObject DHTClient::make_ping_message(const String &tid) const {
    auto msg = BenObject::NewDictionary();
    msg["t"] = tid;
    msg["y"] = "q";
    msg["q"] = "ping";
    msg["a"] = BenObject::NewDictionary();

    // Fill body
    msg["a"]["id"] = local_id.to_string(); //< This is is local
    return msg;
}