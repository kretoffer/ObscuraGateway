#include "obscuragateway/stream_manager.hpp"

#include <algorithm>

namespace obscuragateway {

    void StreamManager::register_stream(uint32_t client_stream_id,
                                        ObscuraProto::net::WsConnectionHdl client_hdl,
                                        std::shared_ptr<BackendNode> node,
                                        uint32_t backend_stream_id,
                                        uint16_t opcode) {
        std::lock_guard<std::mutex> lock(mutex_);
        streams_[client_stream_id] = {{std::move(node), opcode}, backend_stream_id, std::move(client_hdl)};
    }

    StreamMapping StreamManager::resolve(uint32_t client_stream_id, ObscuraProto::net::WsConnectionHdl client_hdl) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = streams_.find(client_stream_id);
        if (it == streams_.end())
            return {nullptr, 0};
        return it->second.mapping;
    }

    void StreamManager::remove_stream(uint32_t client_stream_id, ObscuraProto::net::WsConnectionHdl client_hdl) {
        std::lock_guard<std::mutex> lock(mutex_);
        streams_.erase(client_stream_id);
    }

}
