#pragma once

#include <atomic>
#include <memory>
#include <obscuraproto/ws_common.hpp>
#include <string>

#include "config.hpp"
#include "internal_protocol.hpp"

namespace ObscuraProto {
    namespace net {
        class WsServerWrapper;
    }
}

namespace obscuragateway {

    class BackendNode {
    public:
        BackendNode(const BackendConfig& config, ObscuraProto::net::WsConnectionHdl hdl);

        void send_message(ObscuraProto::net::WsServerWrapper& server, const InternalMessage& msg);

        const std::string& id() const {
            return config_.id;
        }
        uint32_t weight() const {
            return config_.weight;
        }
        uint32_t active_requests() const {
            return active_requests_.load();
        }
        void inc_requests() {
            active_requests_.fetch_add(1);
        }
        void dec_requests() {
            active_requests_.fetch_sub(1);
        }

        ObscuraProto::net::WsConnectionHdl hdl() const {
            return hdl_;
        }

    private:
        BackendConfig config_;
        ObscuraProto::net::WsConnectionHdl hdl_;
        std::atomic<uint32_t> active_requests_{0};
    };

}
