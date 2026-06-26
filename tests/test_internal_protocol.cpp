#include <gtest/gtest.h>

#include "obscuragateway/internal_protocol.hpp"

TEST(InternalProtocolTest, SerializeDeserializeRequest) {
    obscuragateway::InternalMessage msg;
    msg.type = obscuragateway::MessageType::Request;
    msg.corr_id_or_stream_id = 42;
    msg.opcode = 0x1001;
    msg.payload = {0x01, 0x02, 0x03};

    auto data = obscuragateway::serialize_message(msg);
    auto result = obscuragateway::deserialize_message(data);

    EXPECT_EQ(result.type, msg.type);
    EXPECT_EQ(result.corr_id_or_stream_id, msg.corr_id_or_stream_id);
    EXPECT_EQ(result.opcode, msg.opcode);
    EXPECT_EQ(result.payload, msg.payload);
}

TEST(InternalProtocolTest, SerializeDeserializeData) {
    obscuragateway::InternalMessage msg;
    msg.type = obscuragateway::MessageType::Data;
    msg.opcode = 0x00FF;

    auto data = obscuragateway::serialize_message(msg);
    auto result = obscuragateway::deserialize_message(data);

    EXPECT_EQ(result.type, obscuragateway::MessageType::Data);
    EXPECT_EQ(result.opcode, 0x00FF);
}
