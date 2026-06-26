#include <gtest/gtest.h>

#include "obscuragateway/config.hpp"
#include "obscuragateway/router.hpp"

TEST(ConfigTest, ParseOpcodes) {
    auto opcodes = obscuragateway::Router::parse_opcodes("0x0001,0x0005-0x0007,0x000A");
    EXPECT_TRUE(opcodes.count(0x0001));
    EXPECT_TRUE(opcodes.count(0x0005));
    EXPECT_TRUE(opcodes.count(0x0006));
    EXPECT_TRUE(opcodes.count(0x0007));
    EXPECT_TRUE(opcodes.count(0x000A));
    EXPECT_EQ(opcodes.size(), 5);
}
