#include "obscuragateway/internal_protocol.hpp"

#include <arpa/inet.h>

#include <cstring>
#include <stdexcept>

namespace obscuragateway {

    ObscuraProto::byte_vector serialize_message(const InternalMessage& msg) {
        ObscuraProto::byte_vector data(10);
        data.reserve(10 + msg.payload.size());

        uint32_t type_val = htonl(static_cast<uint32_t>(msg.type));
        uint32_t id_val = htonl(msg.corr_id_or_stream_id);
        uint16_t opcode_val = htons(msg.opcode);

        std::memcpy(data.data(), &type_val, 4);
        std::memcpy(data.data() + 4, &id_val, 4);
        std::memcpy(data.data() + 8, &opcode_val, 2);
        data.insert(data.end(), msg.payload.begin(), msg.payload.end());

        return data;
    }

    InternalMessage deserialize_message(const ObscuraProto::byte_vector& data) {
        if (data.size() < 10) {
            throw std::runtime_error("message too short");
        }

        InternalMessage msg;
        uint32_t type_val, id_val;
        uint16_t opcode_val;

        std::memcpy(&type_val, data.data(), 4);
        std::memcpy(&id_val, data.data() + 4, 4);
        std::memcpy(&opcode_val, data.data() + 8, 2);

        msg.type = static_cast<MessageType>(ntohl(type_val));
        msg.corr_id_or_stream_id = ntohl(id_val);
        msg.opcode = ntohs(opcode_val);
        msg.payload.assign(data.begin() + 10, data.end());

        return msg;
    }

}
