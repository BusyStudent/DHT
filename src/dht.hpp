#pragma once

#include "bencode.hpp"
#include "illias.hpp"
#include "timer.hpp"
#include <functional>
#include <ostream>
#include <deque>
#include <array>
#include <bit>

using namespace Illias;

/**
 * @brief Kadilima ID, in network order
 * 
 */
class KID {
    public:
        KID() = default;
        KID(std::string_view v) {
            if (v.size() != 20) {
                data.fill(0);
                return;
            }
            ::memcpy(data.data(), v.data(), 20);
        }
        KID(const std::string &sv) : KID(std::string_view(sv)) { }
        KID(const char        *sv) : KID(std::string_view(sv)) { }
        KID(const KID &) = default;
        ~KID() = default;

        auto begin() {
            return std::begin(data);
        }
        auto end() {
            return std::end(data);
        }
        auto to_string() const {
            return std::string(
                reinterpret_cast<const char*>(data.data()), 
                20
            );
        }
        auto xor_result(const KID &other) const {
            KID ret;
            for (int i = 0; i < 20; i++) {
                ret[i] = data[i] ^ other[i];
            }
            return ret;
        }
        auto make_neighbor(const KID &target, int n = 15) const {
            KID ret;
            ::memcpy(ret.data.data(), target.data.data(), n);
            ::memcpy(ret.data.data() + (20 - n), data.data() + n, (20 - n));
            return ret;
        }
        int count_leading_zero() const {
            static_assert(5 * sizeof(uint32_t) == 20 * sizeof(uint8_t));
            auto a = reinterpret_cast<const uint32_t*>(data.data());

            for (int i = 0; i < 5; i++) {
                auto v = Illias::NetworkToHost(a[i]);

                if (v == 0) {
                    continue;
                }
                return i * 32 + std::countl_zero(v);
            }
            return 5 * 32;
        }
        uint8_t &operator [](size_t idx) {
            return data[idx];
        }
        const uint8_t &operator [](size_t idx) const {
            return data[idx];
        }
        KID      operator ^(const KID &other) const {
            return xor_result(other);
        }
        bool     operator <(const KID &other) const {
            static_assert(5 * sizeof(uint32_t) == 20 * sizeof(uint8_t));
            
            auto a = reinterpret_cast<const uint32_t*>(data.data());
            auto b = reinterpret_cast<const uint32_t*>(other.data.data());
            for (int i = 0; i < 5; i++) {
                auto lhs = Illias::NetworkToHost(a[i]);
                auto rhs = Illias::NetworkToHost(b[i]);

                if (lhs < rhs) {
                    return true;
                }
                if (lhs > rhs) {
                    return false;
                }
            }
            return false;
        }
        bool      operator ==(const KID &other) const noexcept {
            return ::memcmp(data.data(), other.data.data(), 20) == 0;
        }
        bool      operator !=(const KID &other) const noexcept {
            return ::memcmp(data.data(), other.data.data(), 20) != 0;
        }
        operator std::string_view() const {
            return std::string_view(
                reinterpret_cast<const char*>(data.data()), 
                20
            );
        }

        static KID Zero() {
            KID id;
            id.data.fill(0);
            return id;
        }
        static KID Random();
    private:
        std::array<uint8_t, 20> data; //< KAD ID is 160 bits
};

class KNode {
    public:
        KID         id; //< NodeID
        IPEndpoint  endpoint; //< Endpoint  
};
class KBucket {
    public:
        auto begin() noexcept {
            return std::begin(nodes);
        }
        auto end() noexcept {
            return std::end(nodes);
        }
        auto empty() const noexcept {
            return nodes.empty();
        }

        std::deque<KNode> nodes;
        int               max_node_count = 50; //< MAX 50 Nodes
};
class KTable {
    public:
        KTable() = default;
        ~KTable() = default;

        auto begin() noexcept {
            return std::begin(buckets);
        }
        auto end() noexcept {
            return std::end(buckets);
        }
        void set_local_id(const KID &id) {
            local_id = id;
        }
        bool insert(const KID &id, const IPEndpoint &endpoint) {
            auto n = alloc_node(id);
            if (!n) {
                return false;
            }
            n->endpoint = endpoint;
            return true;
        }
        size_t size() const noexcept {
            return nodes_count;
        }
        KBucket *find_bucket(const KID & target_id);
        KNode *find_closest_node(const KID & target_id);
        KNode *find_node(const KID &target_id);
        KNode *alloc_node(const KID &id);
        bool   remove_node(const KID &id);

        bool   contains(const KID &target_id) {
            return find_node(target_id);
        }
        void   log_info() const;
    private:
        KBucket buckets[160]; //< From 0 to max , from closest to farest
        KID local_id; //< ID In local
        size_t nodes_count = 0;
};

class Task {
    public:
        using OnHandler = std::function<void(const BenObject &message, const IPEndpoint &endpoint)>;
        using TimeoutHandler = std::function<void()>;

        OnHandler      process;
        TimeoutHandler timeout;

        int            left_ms = 5000;
};

class RpcManager {
    public:
        RpcManager(TimerService &s);
        ~RpcManager();

        bool process(const BenObject &message, const IPEndpoint &endpoint);

        std::pair<std::string, Task*>
        alloc_task();
    private:
        void check_timeout(int minus_ms);

        TimerService &service;
        std::map<std::string, Task> tasks;
        uint8_t cur_tid[2] = {0, 0}; //< Current TID 
};

class DHTClient {
    public:
        DHTClient();
        ~DHTClient();

        void run();
    private:
        using String = std::string;

        BenObject make_ping_message(const String &task_id) const;

        void bootstrap();
        void recv_data();
        void on_message(const BenObject &message, const IPEndpoint &endpoint);
        void on_find_node_response(const BenObject &message, const IPEndpoint &endpoint);
        void on_get_peers_request(const BenObject &message, const IPEndpoint &endpoint);
        void on_announce_peer_request(const BenObject &message, const IPEndpoint &endpoint);
        void on_ping_request(const BenObject &message, const IPEndpoint &endpoint);
        void send_krpc(const BenObject &message, const IPEndpoint &endpoint);
        void send_find_node(const IPEndpoint &endpoint, const KID &id, const KID &target);
        void send_ping(const KNode &node);

        void on_periodic_search_node(); //< Timer callback
        void on_peridoic_ping_node();

        TimerService service;
        RpcManager   manager {service};

        Socket sock; //< For send and recevie data
        pollfd pfd;

        IPEndpoint local_endpoint;
        KID        local_id; //< Self id
        KTable     table;
};

inline std::ostream &operator <<(std::ostream &s, const BenObject &object) {
    object.dump_impl(s, 0);
    return s;
}