#include "src/dht.hpp"
#include <gtest/gtest.h>

TEST(Bencode, Parse) {
    auto str = BenObject::Parse("1:a");
    ASSERT_TRUE(str.is_string());
    ASSERT_EQ(str.to_string(), "a");
}
TEST(Bencode, Encode) {
    auto object = BenObject::NewDictionary();
    object["a"] = 1;
    object["b"] = "b";
    object["c"] = BenObject::NewList();
    object["c"].push_back(2);
    object["c"].push_back(3);

    ASSERT_EQ(object.encode(), "d1:ai1e1:b1:b1:cli2ei3eee");
    
    object = BenObject::NewDictionary();
    ASSERT_TRUE(object.encode().empty());
}
TEST(Kad, ID) {
    auto a = KID::Zero();
    auto b = KID::Zero();

    a[19] = 1;
    b[0]  = 1;

    ASSERT_TRUE(a < b);
    ASSERT_EQ(b.count_leading_zero(), 7);
    ASSERT_EQ((a ^ b).count_leading_zero(), 7);
}
TEST(Kad, Table) {
    KTable table;
    table.set_local_id(KID::Random());

    auto a = KID::Random();
    auto b = KID::Zero();

    a[19] = 1;
    b[0]  = 1;

    table.alloc_node(a);
    table.alloc_node(b);
    table.find_closest_node(KID::Random());
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}