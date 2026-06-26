#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <obscuraproto/keys.hpp>
#include <obscuraproto/ws_common.hpp>
#include <obscuraproto/ws_server.hpp>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "backend_pool.hpp"
#include "config.hpp"
#include "correlation.hpp"
#include "router.hpp"
#include "stream_manager.hpp"

namespace obscuragateway {

    class Gateway {
    public:
        explicit Gateway(const GatewayConfig& config);
        ~Gateway();

        void start();
        void stop();
        void wait();

    private:
        void setup_client_server();
        void setup_backend_server();
        void setup_routes();

        void on_client_payload(ObscuraProto::net::WsConnectionHdl hdl, ObscuraProto::Payload payload);
        void on_backend_payload(ObscuraProto::net::WsConnectionHdl hdl, ObscuraProto::Payload payload);

        void forward_to_backend(std::shared_ptr<BackendNode> node,
                                uint16_t opcode,
                                const ObscuraProto::byte_vector& params,
                                bool needs_response,
                                ObscuraProto::net::WsConnectionHdl client_hdl,
                                uint32_t original_req_id,
                                const std::string& response_pool);

        GatewayConfig config_;
        Router router_;
        ObscuraProto::KeyPair server_keypair_;

        std::unique_ptr<ObscuraProto::net::WsServerWrapper> client_server_;
        std::unique_ptr<ObscuraProto::net::WsServerWrapper> backend_server_;

        std::vector<std::shared_ptr<BackendPool>> pools_;
        std::unordered_map<std::string, std::shared_ptr<BackendPool>> pool_by_name_;

        std::map<ObscuraProto::net::WsConnectionHdl,
                 std::shared_ptr<BackendNode>,
                 std::owner_less<ObscuraProto::net::WsConnectionHdl>>
            backend_nodes_;
        std::mutex backend_nodes_mutex_;

        CorrelationManager correlation_;
        StreamManager stream_manager_;

        std::thread client_thread_;
        std::thread backend_thread_;
    };

}
