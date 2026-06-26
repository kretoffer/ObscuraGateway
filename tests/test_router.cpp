#include <gtest/gtest.h>

#include "obscuragateway/router.hpp"

TEST(RouterTest, ParseSingleOpcode) {
    auto opcodes = obscuragateway::Router::parse_opcodes("0x00FF");
    EXPECT_EQ(opcodes.size(), 1);
    EXPECT_TRUE(opcodes.count(0x00FF));
}

TEST(RouterTest, ParseRange) {
    auto opcodes = obscuragateway::Router::parse_opcodes("0x0010-0x0013");
    EXPECT_EQ(opcodes.size(), 4);
    EXPECT_TRUE(opcodes.count(0x0010));
    EXPECT_TRUE(opcodes.count(0x0013));
}

TEST(RouterTest, ParseMixed) {
    auto opcodes = obscuragateway::Router::parse_opcodes("0x0001-0x0003,0x000A,0x00FF");
    EXPECT_EQ(opcodes.size(), 5);
}
