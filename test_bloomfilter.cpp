#include <gtest/gtest.h>

#include <string>
#include <cstdint>
#include <array>
#include <string>
#include <cstdint>

#include "src/bloomfilter.hpp"

TEST(BloomFilterTest, TestBloomFilter) {
    // Test case 1: Test the basic functionality of the Bloom filter
    const char *expected_filter_hex_bep33_with_spaces =
        "F6C3F5EA A07FFD91 BDE89F77 7F26FB2B FF37BDB8 FB2BBAA2 FD3DDDE7 BACFFF75 "
        "EE7CCBAE FE5EEDB1 FBFAFF67 F6ABFF5E 43DDBCA3 FD9B9FFD F4FFD3E9 DFF12D1B "
        "DF59DB53 DBE9FA5B 7FF3B8FD FCDE1AFB 8BEDD7BE 2F3EE71E BBBFE93B CDEEFE14 "
        "8246C2BC 5DBFF7E7 EFDCF24F D8DC7ADF FD8FFFDF DDFFF7A4 BBEEDF5C B95CE81F "
        "C7FCFF1F F4FFFFDF E5F7FDCB B7FD79B3 FA1FC77B FE07FFF9 05B7B7FF C7FEFEFF "
        "E0B8370B B0CD3F5B 7F2BD93F EB4386CF DD6F7FD5 BFAF2E9E BFFFFEEC D67ADBF7 "
        "C67F17EF D5D75EBA 6FFEBA7F FF47A91E B1BFBB53 E8ABFB57 62ABE8FF 237279BF "
        "EFBFEEF5 FFC5FEBF DFE5ADFF ADFEE1FB 737FFFFB FD9F6AEF FEEE76B6 FD8F72EF";
    BEP33BloomFilter         bf1;
    std::array<std::byte, 4> ip4 = {std::byte {192}, std::byte {0}, std::byte {2}, std::byte {0}};
    for (int i = 0; i <= 255; i++) {
        ip4[3] = std::byte(i);
        bf1.insert(ip4);
    }

    std::array<std::byte, 16> ip6 {std::byte {0x20}, std::byte {0x01}, std::byte {0x0d}, std::byte {0xb8},
                                   std::byte {0x00}, std::byte {0x00}, std::byte {0x00}, std::byte {0x00},
                                   std::byte {0x00}, std::byte {0x00}, std::byte {0x00}, std::byte {0x00},
                                   std::byte {0x00}, std::byte {0x00}, std::byte {0x00}, std::byte {0x00}};
    for (uint32_t i = 0; i <= 0x3e7; i++) {
        ip6[14] = std::byte((i >> 8) & 0xff);
        ip6[15] = std::byte(i & 0xff);
        bf1.insert(ip6);
    }

    auto bf1_hex = bf1.toHexString(4);

    EXPECT_STREQ(bf1_hex.c_str(), expected_filter_hex_bep33_with_spaces);
    // EXPECT_EQ((int)bf1.calculateEstimatedSize(), 256 + 0x3e8);
    EXPECT_TRUE(bf1.testIP(ilias::IPAddress4::fromUint8Array({192, 0, 2, 1})));
    EXPECT_TRUE(bf1.testIP(ilias::IPAddress4::fromUint8Array({192, 0, 2, 100})));
    EXPECT_FALSE(bf1.testIP(ilias::IPAddress4::fromUint8Array({192, 0, 3, 0})));
    EXPECT_FALSE(bf1.testIP(ilias::IPAddress4::fromUint8Array({192, 0, 1, 255})));
    EXPECT_TRUE(bf1.testIP(ilias::IPAddress6::fromUint8Array(
        {0x20, 0x01, 0x0d, 0xb8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01})));
    EXPECT_TRUE(bf1.testIP(ilias::IPAddress6::fromUint8Array(
        {0x20, 0x01, 0x0d, 0xb8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3d, 0x65})));
    EXPECT_TRUE(bf1.testIP(ilias::IPAddress6::fromUint8Array(
        {0x20, 0x01, 0x0d, 0xb8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0xd7})));

    auto bytes = bf1.toBytes();

    auto bf2 = BEP33BloomFilter(bytes);
    EXPECT_EQ(bf1, bf2);
}

int main() {
    testing::InitGoogleTest();
    return RUN_ALL_TESTS();
}