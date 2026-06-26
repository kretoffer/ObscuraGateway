#pragma once

#include <cstdint>
#include <obscuraproto/packet.hpp>
#include <vector>

namespace obscuragateway {

    enum class MessageType : uint32_t {
        Data = 1,
        Request = 2,
        Response = 3,
        StreamStart = 4,
        StreamData = 5,
        StreamEnd = 6,
        StreamCancel = 7
    };

    struct InternalMessage {
        MessageType type;
        uint32_t corr_id_or_stream_id = 0;
        uint16_t opcode = 0;
        ObscuraProto::byte_vector payload;
    };

    ObscuraProto::byte_vector serialize_message(const InternalMessage& msg);
    InternalMessage deserialize_message(const ObscuraProto::byte_vector& data);

}
