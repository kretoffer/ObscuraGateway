#include "obscuragateway/correlation.hpp"

#include <chrono>
#include <mutex>

namespace obscuragateway {

    uint32_t CorrelationManager::register_request(ObscuraProto::net::WsConnectionHdl client_hdl,
                                                  uint32_t original_req_id,
                                                  const std::string& response_pool) {
        std::lock_guard<std::mutex> lock(mutex_);
        uint32_t corr_id = next_corr_id_++;
        pending_[corr_id] = {std::move(client_hdl), original_req_id, response_pool, std::chrono::steady_clock::now()};
        return corr_id;
    }

    PendingRequest CorrelationManager::resolve(uint32_t corr_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = pending_.find(corr_id);
        if (it == pending_.end())
            return {};
        auto req = std::move(it->second);
        pending_.erase(it);
        return req;
    }

    void CorrelationManager::remove(uint32_t corr_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_.erase(corr_id);
    }

    void CorrelationManager::cleanup_expired(std::chrono::seconds timeout) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto now = std::chrono::steady_clock::now();
        for (auto it = pending_.begin(); it != pending_.end();) {
            if (now - it->second.created > timeout) {
                it = pending_.erase(it);
            } else {
                ++it;
            }
        }
    }

}
