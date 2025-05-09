#pragma once

#include <vector>
#include <bitset>
#include <sstream>
#include <iomanip> // For std::setw, std::setfill, std::hex, std::fixed, std::setprecision

#include <ilias/net/address.hpp>

#include "sha1.h"

template <std::size_t K = 2, std::size_t M = 8 * 256>
class BEP33BloomFilter {
    static_assert(M > 0, "Bitset size M must be greater than 0");
    static_assert(M % 8 == 0, "Bitset size M must be a multiple of 8 for byte-wise hex conversion.");
    static_assert(M >= K * 2, "Bloom filter size M must be at least twice the number of hash functions K.");
    static_assert(K > 0, "Bloom filter hash function count K must be greater than 0");

public:
    BEP33BloomFilter() = default;
    BEP33BloomFilter(const std::bitset<M> &bloomfilter);
    BEP33BloomFilter(std::bitset<M> &&bloomfilter);
    BEP33BloomFilter(const std::string &str);
    BEP33BloomFilter(std::span<std::byte> bytes);

    static auto fromHexString(const std::string &hex) -> BEP33BloomFilter<K, M>;
    static auto fromBinaryString(const std::string &binary) -> BEP33BloomFilter<K, M>;

    void insertIP(const ilias::IPAddress &ip);
    void insert(std::span<const std::byte> data);
    auto calculateEstimatedSize() const -> double;
    auto testIP(const ilias::IPAddress &ip) const -> bool;
    auto test(std::span<const std::byte> data) const -> bool;

    auto toHexString(int bytes_per_space_group = 0) const -> std::string;
    auto toString() const -> std::string;
    auto toBytes() const -> std::vector<std::byte>;

    auto count() const -> std::size_t { return mBloom.count(); }
    auto size() const -> std::size_t { return mBloom.size(); }
    auto any() const -> bool { return mBloom.any(); }
    auto all() const -> bool { return mBloom.all(); }
    auto none() const -> bool { return mBloom.none(); }
    auto flip() -> void { mBloom.flip(); }
    auto flip(std::size_t index) -> void { mBloom.flip(index); }
    auto set() -> void { mBloom.set(); }
    auto set(std::size_t index) -> void { mBloom.set(index); }
    auto reset() -> void { mBloom.reset(); }
    auto reset(std::size_t index) -> void { mBloom.reset(index); }

    auto  operator[](std::size_t index) const { return mBloom[index]; }
    auto &operator[](std::size_t index) { return mBloom[index]; }
    auto  operator=(const BEP33BloomFilter<K, M> &other) -> BEP33BloomFilter<K, M>  &{
        mBloom = other.mBloom;
        return *this;
    }
    auto operator=(BEP33BloomFilter<K, M> &&other) -> BEP33BloomFilter<K, M> & {
        mBloom = std::move(other.mBloom);
        return *this;
    }
    auto operator==(const BEP33BloomFilter<K, M> &other) const -> bool { return mBloom == other.mBloom; }
    auto operator!=(const BEP33BloomFilter<K, M> &other) const -> bool { return mBloom != other.mBloom; }
    auto operator&(const BEP33BloomFilter<K, M> &other) const -> BEP33BloomFilter<K, M> {
        BEP33BloomFilter<K, M> result;
        result.mBloom = mBloom & other.mBloom;
        return result;
    }
    auto operator&=(const BEP33BloomFilter<K, M> &other) const -> BEP33BloomFilter<K, M> {
        mBloom &= other.mBloom;
        return *this;
    }
    auto operator|(const BEP33BloomFilter<K, M> &other) const -> BEP33BloomFilter<K, M> {
        BEP33BloomFilter<K, M> result;
        result.mBloom = mBloom | other.mBloom;
        return result;
    }
    auto operator|=(const BEP33BloomFilter<K, M> &other) const -> BEP33BloomFilter<K, M> {
        mBloom |= other.mBloom;
        return *this;
    }
    auto operator~() const -> BEP33BloomFilter<K, M> {
        BEP33BloomFilter<K, M> result;
        result.mBloom = ~mBloom;
        return result;
    }
    auto operator^(const BEP33BloomFilter<K, M> &other) const -> BEP33BloomFilter<K, M> {
        BEP33BloomFilter<K, M> result;
        result.mBloom = mBloom ^ other.mBloom;
        return result;
    }
    auto operator^=(const BEP33BloomFilter<K, M> &other) const -> BEP33BloomFilter<K, M> {
        mBloom ^= other.mBloom;
        return *this;
    }
    auto operator<<(std::size_t shift) const -> BEP33BloomFilter<K, M> {
        BEP33BloomFilter<K, M> result;
        result.mBloom = mBloom << shift;
        return result;
    }
    auto operator<<=(std::size_t shift) const -> BEP33BloomFilter<K, M> {
        mBloom <<= shift;
        return *this;
    }
    auto operator>>(std::size_t shift) const -> BEP33BloomFilter<K, M> {
        BEP33BloomFilter<K, M> result;
        result.mBloom = mBloom >> shift;
        return result;
    }
    auto operator>>=(std::size_t shift) const -> BEP33BloomFilter<K, M> {
        mBloom >>= shift;
        return *this;
    }

private:
    std::bitset<M> mBloom;
};

template <std::size_t K, std::size_t M>
BEP33BloomFilter<K, M>::BEP33BloomFilter(std::span<std::byte> bytes) {
    if (bytes.size() != M / 8) {
        throw std::invalid_argument("Invalid byte size for the bitset size. Expected " + std::to_string(M / 8) +
                                    " bytes, got " + std::to_string(bytes.size()));
    }
    for (std::size_t i = 0; i < M / 8; ++i) {
        mBloom[i * 8 + 7] = (std::bit_cast<uint8_t>(bytes[i]) & 0x80) != 0;
        mBloom[i * 8 + 6] = (std::bit_cast<uint8_t>(bytes[i]) & 0x40) != 0;
        mBloom[i * 8 + 5] = (std::bit_cast<uint8_t>(bytes[i]) & 0x20) != 0;
        mBloom[i * 8 + 4] = (std::bit_cast<uint8_t>(bytes[i]) & 0x10) != 0;
        mBloom[i * 8 + 3] = (std::bit_cast<uint8_t>(bytes[i]) & 0x08) != 0;
        mBloom[i * 8 + 2] = (std::bit_cast<uint8_t>(bytes[i]) & 0x04) != 0;
        mBloom[i * 8 + 1] = (std::bit_cast<uint8_t>(bytes[i]) & 0x02) != 0;
        mBloom[i * 8 + 0] = (std::bit_cast<uint8_t>(bytes[i]) & 0x01) != 0;
    }
}

template <std::size_t K, std::size_t M>
BEP33BloomFilter<K, M>::BEP33BloomFilter(const std::bitset<M> &mBloom) : mBloom(mBloom) {
}
template <std::size_t K, std::size_t M>
BEP33BloomFilter<K, M>::BEP33BloomFilter(std::bitset<M> &&mBloom) : mBloom(mBloom) {
}

template <std::size_t K, std::size_t M>
BEP33BloomFilter<K, M>::BEP33BloomFilter(const std::string &str) : mBloom(str) {
}

template <std::size_t K, std::size_t M>
auto BEP33BloomFilter<K, M>::fromHexString(const std::string &hex) -> BEP33BloomFilter<K, M> {
    std::string clean_hex = hex;
    // 1. 移除所有空格
    clean_hex.erase(std::remove_if(clean_hex.begin(), clean_hex.end(), [](unsigned char c) { return std::isspace(c); }),
                    clean_hex.end());

    // 2. 验证长度
    // 每个字节需要2个十六进制字符。M / 8 是字节数。
    if (clean_hex.length() != (M / 8) * 2) {
        throw std::invalid_argument("Hex string length is invalid for the bitset size. Expected " +
                                    std::to_string((M / 8) * 2) + " hex characters (excluding spaces), got " +
                                    std::to_string(clean_hex.length()));
    }

    BEP33BloomFilter<K, M> bloomfilter;

    for (std::size_t byte_idx = 0; byte_idx < M / 8; ++byte_idx) {
        // 从清理后的字符串中提取代表一个字节的两个十六进制字符
        std::string   byte_hex_str = clean_hex.substr(byte_idx * 2, 2);
        unsigned char current_byte_value;
        try {
            // 使用 stringstream 将两个十六进制字符转换为一个字节值
            std::stringstream ss_byte;
            ss_byte << std::hex << byte_hex_str;
            int byte_val_int;
            ss_byte >> byte_val_int;
            if (ss_byte.fail() || byte_val_int < 0 || byte_val_int > 255) {
                throw std::runtime_error("Invalid hex characters in string: " + byte_hex_str);
            }
            current_byte_value = static_cast<unsigned char>(byte_val_int);
        } catch (const std::exception &e) { // Catches std::invalid_argument from stoul or custom error
            throw std::runtime_error("Failed to parse hex byte '" + byte_hex_str + "': " + e.what());
        }

        // 将这个字节的位设置到 bitset 中
        // `std::bitset` 的索引 `i` 对应第 `i` 位。
        // 我们需要将 `current_byte_value` 的 LSB 设置到 `mBloom[byte_idx * 8 + 0]`，
        // MSB 设置到 `mBloom[byte_idx * 8 + 7]`。
        for (int bit_in_byte_idx = 0; bit_in_byte_idx < 8; ++bit_in_byte_idx) {
            if ((current_byte_value >> bit_in_byte_idx) & 0x01) {
                bloomfilter.mBloom[byte_idx * 8 + bit_in_byte_idx] = 1;
            }
        }
    }
    return bloomfilter;
}

template <std::size_t K, std::size_t M>
auto BEP33BloomFilter<K, M>::fromBinaryString(const std::string &binary) -> BEP33BloomFilter<K, M> {
    return BEP33BloomFilter<K, M>(binary);
}

template <std::size_t K, std::size_t M>
void BEP33BloomFilter<K, M>::insertIP(const ilias::IPAddress &ip) {
    insert(ip.span());
}

template <std::size_t K, std::size_t M>
void BEP33BloomFilter<K, M>::insert(std::span<const std::byte> data) {
    unsigned char hash[20] = {0};
    ::SHA1(hash, reinterpret_cast<const unsigned char *>(data.data()), data.size());
    for (std::size_t i = 0; i < K * 2; i += 2) {
        auto index    = hash[i] | hash[i + 1] << 8;
        index         = index % M;
        mBloom[index] = 1;
    }
}

template <std::size_t K, std::size_t M>
auto BEP33BloomFilter<K, M>::testIP(const ilias::IPAddress &ip) const -> bool {
    return test(ip.span());
}

template <std::size_t K, std::size_t M>
auto BEP33BloomFilter<K, M>::test(std::span<const std::byte> data) const -> bool {
    unsigned char hash[20] = {0};
    ::SHA1(hash, reinterpret_cast<const unsigned char *>(data.data()), data.size());
    for (std::size_t i = 0; i < K * 2; i += 2) {
        auto index = hash[i] | hash[i + 1] << 8;
        index      = index % M;
        if (mBloom[index] == 0) {
            return false;
        }
    }
    return true;
}

template <std::size_t K, std::size_t M>
double BEP33BloomFilter<K, M>::calculateEstimatedSize() const { // 注意 const 和返回类型
    double M_double = static_cast<double>(M);
    double K_double = static_cast<double>(K);
    double c_double = static_cast<double>(
        std::min(static_cast<std::size_t>(M - 1), M - mBloom.count())); // mBloom.size() - mBloom.count() 是 0 的数量

    if (c_double < 1.0) { // 如果mbloom中所有位都是1，则c_double = 0，log(0)未定义，且结果没有意义。
        return std::numeric_limits<double>::infinity(); // 或者其他表示饱和的值
    }
    if (mBloom.count() == 0) { // 如果过滤器为空
        return 0;
    }

    double term_c_div_m         = c_double / M_double;
    double term_1_minus_1_div_m = 1.0 - (1.0 / M_double);

    double numerator   = std::log(term_c_div_m);
    double denominator = K_double * std::log(term_1_minus_1_div_m);

    if (std::abs(denominator) < 1e-9) {                 // 避免除以非常接近零的数
        return std::numeric_limits<double>::infinity(); // 或者表示饱和
    }
    return numerator / denominator;
}

template <std::size_t K, std::size_t M>
std::string BEP33BloomFilter<K, M>::toHexString(int bytes_per_space_group) const {
    std::stringstream ss;
    ss << std::hex << std::uppercase << std::setfill('0');

    for (std::size_t byte_idx = 0; byte_idx < M / 8; ++byte_idx) {
        unsigned char current_byte = 0;
        for (int bit_in_byte_idx = 0; bit_in_byte_idx < 8; ++bit_in_byte_idx) {
            // mBloom[byte_idx * 8 + bit_in_byte_idx] 是当前字节的第 bit_in_byte_idx 位 (LSB 0-indexed)
            if (mBloom[byte_idx * 8 + bit_in_byte_idx]) {
                current_byte |= (1 << bit_in_byte_idx);
            }
        }
        ss << std::setw(2) << static_cast<int>(current_byte);

        if (bytes_per_space_group > 0 && ((byte_idx + 1) % bytes_per_space_group == 0) && (byte_idx + 1 < M / 8)) {
            ss << " ";
        }
    }
    return ss.str();
}

template <std::size_t K, std::size_t M>
auto BEP33BloomFilter<K, M>::toString() const -> std::string {
    return mBloom.to_string();
}

template <std::size_t K, std::size_t M>
auto BEP33BloomFilter<K, M>::toBytes() const -> std::vector<std::byte> {
    std::vector<std::byte> ret;
    for (std::size_t byte_idx = 0; byte_idx < M / 8; ++byte_idx) {
        unsigned char current_byte_value = 0;
        for (int bit_in_byte_idx = 0; bit_in_byte_idx < 8; ++bit_in_byte_idx) {
            if (mBloom[byte_idx * 8 + bit_in_byte_idx]) {
                current_byte_value |= (1 << bit_in_byte_idx);
            }
        }
        ret.push_back(static_cast<std::byte>(current_byte_value));
    }
    return ret;
}