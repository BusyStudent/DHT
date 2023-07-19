#include <optional>
#include <charconv>
#include <iostream>
#include <format>
#include "dht.hpp"
#include "sha1.h"

static std::pair<const char*, uint16_t> bootstrap_nodes [] = {
    {"router.bittorrent.com", 6881},
    {"dht.transmissionbt.com", 6881},
    {"router.utorrent.com", 6881}
};

static std::optional<int> parse_int(std::string_view text) {
    int value;
    auto [ptr, err] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (err == std::errc{ }) {
        return value;
    }
    return std::nullopt;
}
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
static std::string random_node_id() {
    return (random_string(20));
}
static std::string random_tid() {
    return random_string(2);
}

static constexpr auto tid_len = 2;
static constexpr auto node_id_len = 2;

std::string BenObject::encode() const {
    std::string ret;
    if (!encode_impl(ret)) {
        ret.clear();
    }
    return ret;
}
bool        BenObject::encode_impl(String &buf) const {
    if (is_null()) {
        return false;
    }
    if (is_integer()) {
        auto target = std::back_inserter(buf);
        std::format_to(target, "i{}e", to_integer());
        return true;
    }
    if (is_string()) {
        const auto &str = to_string();
        if (str.empty()) {
            return false;
        }

        auto target = std::back_inserter(buf);
        std::format_to(target, "{}:{}", str.size(), str);
        return true;
    }
    if (is_list()) {
        buf += 'l';
        const auto &list = to_list();
        if (list.empty()) {
            return false;
        }
        for (const auto &item : list) {
            if (!item.encode_impl(buf)) {
                return false;
            }
        }
        buf += 'e';
        return true;
    }
    if (is_dictionary()) {
        buf += 'd';
        const auto &dict = to_dictionary();
        if (dict.empty()) {
            return false;
        }
        for (const auto &[key, item] : dict) {
            auto target = std::back_inserter(buf);
            if (key.empty()) {
                return false;
            }
            std::format_to(target, "{}:{}", key.size(), key);
            if (!item.encode_impl(buf)) {
                return false;
            }
        }
        buf += 'e';
        return true;
    }
    ::abort();
}
BenObject BenObject::ParseImpl(std::string_view &text) {
    if (text.empty() || text.length() <= 2) {
        return BenObject();
    }
    else if (auto spos = text.find(':'); std::isdigit(text[0]) && spos != text.npos) {
        // String n:data
        auto val = parse_int(text.substr(0, spos));
        if (!val) {
            return BenObject();
        }
        auto n = val.value_or(0);
        if (text.length() < n + spos + 1) {
            return BenObject();
        }
        auto object = BenObject(String(text.substr(spos + 1, n)));
        // Move forward
        text = text.substr(n + spos + 1);
        return object;
    }
    else if (auto epos = text.find('e'); text.starts_with('i') && epos != text.npos) {
        // Int i123e
        auto num = text.substr(1, epos - 1);
        auto val = parse_int(num);
        if (!val) {
            return BenObject();
        }
        auto object = BenObject(val.value_or(0));
        // Move
        text = text.substr(epos + 1);
        return object;
    }
    else if (text.starts_with('l')) {
        // List lxxxe
        auto list = BenObject::NewList();
        // Move 1 for drop l
        text = text.substr(1);

        // Decode each elems
        while (!text.empty() && text[0] != 'e') {
            auto ret = ParseImpl(text);
            if (ret.is_null()) {
                // Error
                return BenObject();
            }
            list.push_back(std::move(ret));
        }
        if (list.empty()) {
            // Bencode didnot allow empty
            return BenObject();
        }
        // Move 1 for drop e
        text = text.substr(1);
        return list;
    }
    else if (text.starts_with('d')) {
        // Dict dxxxe
        auto dict = BenObject::NewDictionary();
        // Move 1 for drop d
        text = text.substr(1);

        // Decode each elems [str, value]
        while (!text.empty() && text[0] != 'e') {
            auto key = ParseImpl(text);
            if (!key.is_string()) {
                // Error
                return BenObject();
            }
            auto value = ParseImpl(text);
            if (key.is_null()) {
                // Error
                return BenObject();
            }
            dict[key.to_string()] = std::move(value);
        }
        if (dict.empty()) {
            // Bencode didnot allow empty
            return BenObject();
        }
        // Move 1 for drop e
        text = text.substr(1);
        return dict;
    }
    return BenObject();
}
BenObject BenObject::Parse(std::string_view text) {
    return ParseImpl(text);
}
void      BenObject::dump_impl(std::ostream &os, int depth) const {
    // TODO :
}



// DHT Parts
DHTClient::DHTClient() {
    sock = Socket(AF_INET, SOCK_DGRAM);
    if (sock.bad()) {
        std::cerr << "Failed to create socket" << std::endl;
    }
    if (!sock.bind(IPEndpoint("0.0.0.0", 11453))) {
        std::cerr << "Failed to bind socket" << std::endl;
    }
    if (!sock.setblocking(false)) {
        std::cerr << "Failed to ioctl socket" << std::endl;
    }

    local_endpoint = sock.local_endpoint();

    pfd.fd = sock.fd();
    pfd.events = POLLIN;
    pfd.revents = 0;

    // local_id = "ukqxfyewiicoybhfvmuw";
    local_id = "ukqxfyewiicoybhfvmuw";


    std::cout << "Listen on " << local_endpoint.to_string() << std::endl;
    std::cout << "Local id " << tohex(local_id) << std::endl;
}
DHTClient::~DHTClient() {

}
void DHTClient::run() {
    bootstrap();
    while (true) {
        auto n = Illias::Poll(&pfd, 1);
        if (n == -1) {
            // Error
            return;
        }
        if (pfd.revents & POLLIN) {
            recv_data();
        }
    }
}
void DHTClient::bootstrap() {
    for (const auto [domain, port] : bootstrap_nodes) {
        send_find_node(IPEndpoint::Parse(domain, port), local_id, random_node_id());
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
    auto t = message["t"].to_string();
    auto y = message["y"].to_string();
    if (y == "r") {
        // Reponse
        std::cout << "Response from node " << tohex(message["r"]["id"].to_string()) << " " << ep.to_string() << std::endl;
        if (message["r"].contains("nodes")) {
            on_find_node_response(message, ep);
        }
        else if (message["r"].contains("token")) {
            // Get peer
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
        std::cout << "Error " << message["e"][0].to_integer() << ":" <<  message["e"][1].to_string()
                  << " from " << ep.to_string() << std::endl;
    }
    else {
        // Unknown
        std::cout << "Got a Unknown message from " << ep.to_string() << std::endl;
    }
}
void DHTClient::on_find_node_response(const BenObject &message, const IPEndpoint &ep) {
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
        KNode node {nid, endpoint};

        std::cout << "Add Node " << tohex(nid) << " " << endpoint.to_string() << std::endl;

        knodes[nid] = node;

        send_ping(node);
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
    std::cout << "Ping from node " << message["a"]["id"].to_string() << " " << ep.to_string() << std::endl;

    auto msg = BenObject::NewDictionary();
    msg["t"] = message["t"];
    msg["y"] = "r";
    msg["r"] = BenObject::NewDictionary();
    msg["r"]["id"] = local_id;

    send_krpc(msg, ep);
}
void DHTClient::send_krpc(const BenObject &message, const IPEndpoint &ep) {
    std::cout << "Sending KPRC to " << ep.to_string() << std::endl; 

    auto data = message.encode();
    auto buf = data.c_str();
    ssize_t left = data.size();

    while (left > 0) {
        auto ret = sock.sendto(buf, left, 0, ep);
        if (ret == -1) {
            // Error
            std::cerr << "Failed to sendto " << ep.to_string() << std::endl;
            return;
        }
        left -= ret;
        buf += ret;
    }
}
void DHTClient::send_find_node(const IPEndpoint &endpoint, const String &id, const String &target) {
    // Make Find node request
    auto msg = BenObject::NewDictionary();
    msg["t"] = random_tid();
    msg["y"] = "q";
    msg["q"] = "find_node";
    msg["a"] = BenObject::NewDictionary();

    // Fill body
    msg["a"]["id"] = id;
    msg["a"]["target"] = target;

    send_krpc(msg, endpoint);
}
void DHTClient::send_ping(const KNode &node) {
    auto msg = BenObject::NewDictionary();
    msg["t"] = random_tid();
    msg["y"] = "q";
    msg["q"] = "ping";
    msg["a"] = BenObject::NewDictionary();

    // Fill body
    msg["a"]["id"] = local_id; //< This is is local

    send_krpc(msg, node.endpoint);
}