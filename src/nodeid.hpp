#pragma once

#include <cstdint>
#include <random>
#include <array>
#include <span>
#include <bit>
#include "sha1.h"

// NOTE: in (self ^ node).clz() is bigger, the node is more closer to the self

/**
 * @brief 160 bits Node Id
 * 
 */
class NodeId {
public:
    constexpr NodeId() { std::fill(mId.begin(), mId.end(), 0); }
    constexpr NodeId(const NodeId &) = default;
    constexpr NodeId(NodeId &&) = default;

    /**
     * @brief Count leading zero bits
     * 
     * @return size_t 
     */
    auto clz() const -> size_t;
    /**
     * @brief To human readable hex string
     * 
     * @return std::string 
     */
    auto toHex() const -> std::string;
    /**
     * @brief Cast to binary string view
     * 
     * @return std::string_view 
     */
    auto toStringView() const -> std::string_view;
    /**
     * @brief Calc the distance with the node
     * 
     * @param id 
     * @return size_t 0 on self, 160 on max, smaller is closer
     */
    auto distance(const NodeId &id) -> size_t;
    /**
     * @brief Assign the node id
     * 
     * @return NodeId& 
     */
    auto operator =(const NodeId &) -> NodeId & = default;
    auto operator =(NodeId &&) -> NodeId & = default;
    /**
     * @brief Xor the Node Id
     * 
     * @return NodeId 
     */
    auto operator ^(const NodeId &) const -> NodeId;
    /**
     * @brief Compare Node id
     * 
     */
    auto operator <=>(const NodeId &) const = default;
    /**
     * @brief Random a node id
     * 
     * @return NodeId 
     */
    static auto rand() -> NodeId;
    /**
     * @brief Get zero node id
     * 
     * @return NodeId 
     */
    static auto zero() -> NodeId;
    /**
     * @brief Create node id from a memory buffer
     * 
     * @param mem 
     * @param n 
     * @return NodeId 
     */
    static auto from(const void *mem, size_t n) -> NodeId;
    /**
     * @brief Create node id from a memory buffer
     * 
     * @tparam ize_t 
     * @return NodeId 
     */
    template <size_t N>
    static auto from(const char (&mem)[N]) -> NodeId;
    /**
     * @brief Create node id from hex string
     * 
     * @param hexString 
     * @return NodeId 
     */
    static auto fromHex(std::string_view hexString) -> NodeId;
    /**
     * @brief Create node id from hex string
     * 
     * @tparam N 
     * @return NodeId 
     */
    template <size_t N>
    static auto fromHex(const char (&hexString)[N]) -> NodeId;
private:
    std::array<uint8_t, 20> mId;
};

inline auto NodeId::clz() const -> size_t {
    for (size_t i = 0; i < mId.size(); i++) {
        auto num = mId[i];
        if (num == 0) {
            continue;
        }
        return i * 8 + std::countl_zero(num);
    }
    return 20 * 8;
}
inline auto NodeId::toHex() const -> std::string {
    std::string buffer(40, 0);
    for (size_t i = 0; i < mId.size(); i++) {
        ::sprintf(buffer.data() + i * 2, "%02x", mId[i]);
    }
    return buffer;
}
inline auto NodeId::distance(const NodeId &id) -> size_t {
    auto d = (*this) ^ id;
    return 160 - d.clz();
}
inline auto NodeId::toStringView() const -> std::string_view {
    return std::string_view(
        reinterpret_cast<const char*>(mId.data()),
        mId.size()
    );
}
inline auto NodeId::operator ^(const NodeId &other) const -> NodeId {
    NodeId id;
    for (size_t i = 0; i < mId.size(); i++) {
        id.mId[i] = mId[i] ^ other.mId[i];
    }
    return id;
}

inline auto NodeId::rand() -> NodeId {
    std::mt19937 gen;
    std::uniform_int_distribution<uint32_t> dis(0, 255);
    gen.seed(std::random_device()());
    
    uint8_t buffer[20];
    for (auto& i : buffer) {
        i = dis(gen);
    }
    // Sha1 it
    NodeId id;
    ::SHA1_CTX ctxt;
    ::SHA1Init(&ctxt);
    ::SHA1Update(&ctxt, buffer, 20);
    ::SHA1Final(id.mId.data(), &ctxt);
    return id;
}
inline auto NodeId::zero() -> NodeId {
    NodeId id;
    for (auto& i : id.mId) {
        i = 0;
    }
    return id;
}
inline auto NodeId::from(const void *mem, size_t n) -> NodeId {
    if (n != 20) {
        return zero();
    }
    NodeId id;
    for (size_t i = 0; i < n; i++) {
        id.mId[i] = ((const uint8_t*)mem)[i];
    }
    return id;
}
template <size_t N>
inline auto NodeId::from(const char (&mem)[N]) -> NodeId {
    static_assert(N == 21, "Node id must be 20 bytes");
    return from(mem, 20);
}
inline auto NodeId::fromHex(std::string_view hexString) -> NodeId {
    if (hexString.size() != 40) {
        return zero();
    }
    NodeId id;
    for (auto i = 0; i < 20; i++) {
        auto sub = hexString.substr(i * 2, 2);
        auto [ptr, ec] = std::from_chars(sub.data(), sub.data() + sub.size(), id.mId[i], 16);
        if (ec != std::errc()) {
            return zero();
        }
    }
    return id;
}
template <size_t N>
inline auto NodeId::fromHex(const char (&hexString)[N]) -> NodeId {
    static_assert(N == 41, "Hex string must be 40 characters");
    return fromHex(std::string_view(hexString));
}