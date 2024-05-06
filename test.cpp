#include "src/bencode.hpp"
#include "src/nodeid.hpp"
#include <gtest/gtest.h>

TEST(Bencode, decode) {
    auto str = BenObject::decode("1:a");
    ASSERT_TRUE(str.isString());
    ASSERT_EQ(str, "a");
    
    auto intval = BenObject::decode("i123e");
    ASSERT_EQ(intval, 123);

    // List
    auto list = BenObject::decode("l1:a1:be");
    ASSERT_TRUE(list.isList());
    ASSERT_EQ(list.size(), 2);
    ASSERT_EQ(list[0], "a");
    ASSERT_EQ(list[1], "b");

    // Dict
    auto dict = BenObject::decode("d1:ai1e1:b1:b1:cli2ei3eee");
    ASSERT_TRUE(dict.isDict());
    ASSERT_EQ(dict["a"], 1);
    ASSERT_EQ(dict["b"], "b");
    ASSERT_EQ(dict["c"][0], 2);
    ASSERT_EQ(dict["c"][1], 3);
    
    // Invalid
    ASSERT_EQ(BenObject::decode("i123"), BenObject());
    ASSERT_EQ(BenObject::decode("l1:a1:b"), BenObject());
    ASSERT_EQ(BenObject::decode("d1:ai1e1:b1:b1:cli2ei3ee"), BenObject());
}
TEST(Bencode, encode) {
    auto object = BenDict();
    object["a"] = 1;
    object["b"] = "b";
    object["c"] = {
        2, 3
    };
    ASSERT_EQ(object.encode(), "d1:ai1e1:b1:b1:cli2ei3eee");

    // KRPC Request
    auto request = BenDict();
    request["t"] = "abcdefghij0123456789";  // Transaction ID
    request["y"] = "q";                     // Query
    request["q"] = "ping";                  // Method

    request["a"] = BenDict();
    request["a"]["id"] = "mnopqrstuvwxyz123456";  // Node ID


    // Encoding
    std::string encodedRequest = request.encode();

    // Assertion
    std::string expectedEncodedRequest = "d1:ad2:id20:mnopqrstuvwxyz123456e1:q4:ping1:t20:abcdefghij01234567891:y1:qe";
    ASSERT_EQ(encodedRequest, expectedEncodedRequest);
}
TEST(Kad, ID) {
    ASSERT_EQ(NodeId::zero(), NodeId::zero());

    for (size_t i = 0; i < 10; i++) {
        auto rand = NodeId::rand();
        auto hex = rand.toHex();
        std::cout << hex << std::endl;
        ASSERT_EQ(NodeId::fromHex(hex), rand);
    }
}
// TEST(Kad, Table) {
//     KTable table;
//     table.set_local_id(KID::Random());

//     auto a = KID::Random();
//     auto b = KID::Zero();

//     a[19] = 1;
//     b[0]  = 1;

//     table.alloc_node(a);
//     table.alloc_node(b);
//     table.find_closest_node(KID::Random());
// }

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}