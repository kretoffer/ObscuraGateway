#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <obscuraproto/ws_common.hpp>
#include <unordered_map>

namespace obscuragateway {

    class BackendNode;

    struct StreamMapping {
        std::shared_ptr<BackendNode> backend_node;
        uint16_t opcode;
    };

    class StreamManager {
    public:
        void register_stream(uint32_t client_stream_id,
                             ObscuraProto::net::WsConnectionHdl client_hdl,
                             std::shared_ptr<BackendNode> node,
                             uint32_t backend_stream_id,
                             uint16_t opcode);
        StreamMapping resolve(uint32_t client_stream_id, ObscuraProto::net::WsConnectionHdl client_hdl);
        void remove_stream(uint32_t client_stream_id, ObscuraProto::net::WsConnectionHdl client_hdl);

    private:
        struct Entry {
            StreamMapping mapping;
            uint32_t backend_stream_id;
            ObscuraProto::net::WsConnectionHdl client_hdl;
        };

        std::unordered_map<uint32_t, Entry> streams_;
        std::mutex mutex_;
    };

}
