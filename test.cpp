#include "src/bencode.hpp"
#include "src/nodeid.hpp"
#include "src/route.hpp"
#include "src/krpc.hpp"
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
    auto object = BenObject::makeDict();
    object["a"] = 1;
    object["b"] = "b";
    object["c"] = {
        2, 3
    };
    ASSERT_EQ(object.encode(), "d1:ai1e1:b1:b1:cli2ei3eee");

    // KRPC Request
    auto request = BenObject::makeDict();
    request["t"] = "abcdefghij0123456789";  // Transaction ID
    request["y"] = "q";                     // Query
    request["q"] = "ping";                  // Method

    request["a"] = BenObject::makeDict();
    request["a"]["id"] = "mnopqrstuvwxyz123456";  // Node ID


    // Encoding
    std::string encodedRequest = request.encode();

    // Assertion
    std::string expectedEncodedRequest = "d1:ad2:id20:mnopqrstuvwxyz123456e1:q4:ping1:t20:abcdefghij01234567891:y1:qe";
    ASSERT_EQ(encodedRequest, expectedEncodedRequest);

    // Try show it
    std::cout << std::format("{}", request) << std::endl;
}

TEST(Bencode, make) {
    BenObject list {
        1, "Hello", {
            "A", 2
        }
    };
    ASSERT_EQ(list.isList(), true);
    std::cout << list.encode() << std::endl;
    ASSERT_EQ(list[0], 1);
    ASSERT_EQ(list[1], "Hello");
    ASSERT_EQ(list[2][0], "A");
    ASSERT_EQ(list[2][1], 2);
}

TEST(Kad, ID) {
    ASSERT_EQ(NodeId::zero(), NodeId::zero());

    for (size_t i = 0; i < 10; i++) {
        auto rand = NodeId::rand();
        auto hex = rand.toHex();
        std::cout << hex << std::endl;
        ASSERT_EQ(NodeId::fromHex(hex), rand);
    }
    auto a = NodeId::fromHex("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF");
    auto zero = NodeId::zero();
    std::cout << (a ^ a).toHex() << std::endl;
    std::cout << (a ^ zero).clz() << std::endl;

    ASSERT_EQ((a ^ a).clz(), 160);
    ASSERT_EQ((a ^ zero).clz(), 0);

    // Self distance is 0
    auto rand = NodeId::rand();
    ASSERT_EQ(rand.distanceExp(rand), 0);

    // Random distance
    for (int i = 160; i >= 1; i--) {
        auto id = rand.randWithDistance(i);
        // ASSERT_EQ(id.distance(rand), i);
        std::cout << id.distanceExp(rand) << std::endl;
    }

    // Check a distance b eq b distance a
    auto a1 = NodeId::rand();
    auto b1 = NodeId::rand();
    ASSERT_EQ(a1.distance(b1), b1.distance(a1));
}

TEST(Kad, Rpc) {
    auto query = PingQuery{
        .transId = "aa",
        .id = NodeId::fromHex("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF")
    };
    auto msg = query.toMessage();
    auto q = PingQuery::fromMessage(msg);

    ASSERT_EQ(isQueryMessage(msg), true);
    ASSERT_EQ(query, q);
    std::cout << msg.encode() << std::endl;

    // Parse for ping reply and request
    auto pingQueryEncoded = "d1:ad2:id20:abcdefghij0123456789e1:q4:ping1:t2:aa1:y1:qe";
    auto pingQuery = PingQuery::fromMessage(BenObject::decode(pingQueryEncoded)).value();
    ASSERT_EQ(pingQuery.toMessage().encode(), pingQueryEncoded);
    ASSERT_EQ(pingQuery.id, NodeId::from("abcdefghij0123456789"));

    auto pingReplyEncoded = "d1:rd2:id20:mnopqrstuvwxyz123456e1:t2:aa1:y1:re";
    auto pingReply = PingReply::fromMessage(BenObject::decode(pingReplyEncoded)).value();
    ASSERT_EQ(pingReply.toMessage().encode(), pingReplyEncoded);
    ASSERT_EQ(pingReply.id, NodeId::from("mnopqrstuvwxyz123456"));

    // Parse for error reply
    auto errorEncoded = ("d1:eli201e23:A Generic Error Ocurrede1:t2:aa1:y1:ee");
    auto errorReply = ErrorReply::fromMessage(BenObject::decode(errorEncoded)).value();
    std::cout << errorReply.error << std::endl;
    ASSERT_EQ(errorReply.error, "A Generic Error Ocurred");
    ASSERT_EQ(errorReply.errorCode, 201);

    ASSERT_EQ(errorReply.toMessage().encode(), errorEncoded);
}

TEST(Kad, Route) {
    auto id = NodeId::rand();
    RoutingTable table(id);
    for (size_t i = 0; i < 160; i++) {
        for (size_t n = 0; n < 10; n++) {
            table.updateNode({id.randWithDistance(i), "127.0.0.1:10"});
        }
    }
    auto targetId = id.randWithDistance(100);
    auto nodes = table.findClosestNodes(targetId, 20);
    std::cout << "Search result" << std::endl;
    for (auto &node : nodes) {
        std::cout << node.id.toHex() << std::endl;
        std::cout << node.id.distanceExp(targetId) << std::endl;
    }
    auto nodes2 = table.findClosestNodes(id, 20);
    std::cout << "Search result" << std::endl;
    for (auto &node : nodes2) {
        std::cout << node.id.toHex() << std::endl;
        std::cout << node.id.distanceExp(id) << std::endl;
    }
}

TEST(Kad, Distance) {
    auto id1 = NodeId::fromHex("0019c6bcd5ebd44b91b768fcd94c5ff8b80dab14");
    auto id2 = NodeId::fromHex("0000013aa3b5a4def0df03e27646f3b2666a8e85");
    ASSERT_TRUE(id1 > id2);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}