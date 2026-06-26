#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <obscuraproto/ws_common.hpp>
#include <string>
#include <unordered_map>

namespace obscuragateway {

    class BackendNode;

    struct PendingRequest {
        ObscuraProto::net::WsConnectionHdl client_hdl;
        uint32_t original_req_id;
        std::string response_pool;
        std::chrono::steady_clock::time_point created;
    };

    class CorrelationManager {
    public:
        uint32_t register_request(ObscuraProto::net::WsConnectionHdl client_hdl,
                                  uint32_t original_req_id,
                                  const std::string& response_pool);
        PendingRequest resolve(uint32_t corr_id);
        void remove(uint32_t corr_id);
        void cleanup_expired(std::chrono::seconds timeout);

    private:
        std::unordered_map<uint32_t, PendingRequest> pending_;
        uint32_t next_corr_id_ = 1;
        std::mutex mutex_;
    };

}
