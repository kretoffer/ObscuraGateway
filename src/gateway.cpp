#include "obscuragateway/gateway.hpp"

#include <cstdlib>
#include <iostream>
#include <obscuraproto/crypto.hpp>

#include "obscuragateway/internal_protocol.hpp"

namespace obscuragateway {

    static uint16_t parse_port(const std::string& addr) {
        auto colon = addr.rfind(':');
        if (colon == std::string::npos)
            return 8443;
        return static_cast<uint16_t>(std::stoul(addr.substr(colon + 1)));
    }

    static ObscuraProto::PublicKey hex_to_public_key(const std::string& hex) {
        ObscuraProto::PublicKey pk;
        for (size_t i = 0; i < hex.size(); i += 2) {
            auto byte = static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16));
            pk.data.push_back(byte);
        }
        return pk;
    }

    Gateway::Gateway(const GatewayConfig& config)
        : config_(config), server_keypair_(ObscuraProto::Crypto::generate_sign_keypair()) {
    }

    Gateway::~Gateway() {
        stop();
    }

    void Gateway::start() {
        setup_routes();
        setup_client_server();
        setup_backend_server();

        std::cout << "Gateway started. Clients: " << config_.listen.clients << ", Backends: " << config_.listen.backends
                  << std::endl;
    }

    void Gateway::stop() {
        if (client_server_)
            client_server_->stop();
        if (backend_server_)
            backend_server_->stop();
    }

    void Gateway::wait() {
        if (client_thread_.joinable())
            client_thread_.join();
        if (backend_thread_.joinable())
            backend_thread_.join();
    }

    void Gateway::setup_client_server() {
        auto config = ObscuraProto::Config::with_defaults();
        client_server_ = std::make_unique<ObscuraProto::net::WsServerWrapper>(server_keypair_, config);

        client_server_->set_anon_default_payload_handler(
            [this](auto hdl, auto payload) { on_client_payload(hdl, payload); });

        auto port = parse_port(config_.listen.clients);
        client_thread_ = std::thread([this, port] { client_server_->run(port); });
    }

    void Gateway::setup_backend_server() {
        auto config = ObscuraProto::Config::with_defaults();
        backend_server_ = std::make_unique<ObscuraProto::net::WsServerWrapper>(server_keypair_, config);

        // Строим карту: публичный ключ → (pool_name, backend_id, BackendConfig)
        struct BackendInfo {
            std::string pool_name;
            BackendConfig be_config;
        };
        std::map<ObscuraProto::PublicKey, BackendInfo> known_backends;
        for (const auto& pool_cfg : config_.pools) {
            for (const auto& be : pool_cfg.backends) {
                known_backends[hex_to_public_key(be.public_key)] = {pool_cfg.name, be};
            }
        }

        backend_server_->set_client_identity_handler(
            [this, known_backends](ObscuraProto::net::WsConnectionHdl hdl, ObscuraProto::PublicKey pk) -> bool {
                auto it = known_backends.find(pk);
                if (it == known_backends.end()) {
                    std::cerr << "Backend rejected: unknown public key" << std::endl;
                    return false;
                }

                std::lock_guard<std::mutex> lock(backend_nodes_mutex_);
                auto node = std::make_shared<BackendNode>(it->second.be_config, hdl);
                backend_nodes_[hdl] = node;

                auto pool_it = pool_by_name_.find(it->second.pool_name);
                if (pool_it != pool_by_name_.end()) {
                    pool_it->second->add_node(node);
                }

                std::cout << "Backend '" << it->second.be_config.id << "' connected (pool: " << it->second.pool_name
                          << ")" << std::endl;
                return true;
            });

        backend_server_->set_default_payload_handler(
            [this](auto hdl, auto payload) { on_backend_payload(hdl, payload); });

        auto port = parse_port(config_.listen.backends);
        backend_thread_ = std::thread([this, port] { backend_server_->run(port); });
    }

    void Gateway::setup_routes() {
        for (const auto& pool_cfg : config_.pools) {
            auto pool = std::make_shared<BackendPool>(pool_cfg);
            pool_by_name_[pool_cfg.name] = pool;
            pools_.push_back(std::move(pool));
        }

        for (const auto& route_cfg : config_.routes) {
            auto req_it = pool_by_name_.find(route_cfg.request_backend);
            if (req_it == pool_by_name_.end()) {
                std::cerr << "Route: request pool '" << route_cfg.request_backend << "' not found" << std::endl;
                continue;
            }

            std::shared_ptr<BackendPool> resp_pool;
            if (route_cfg.response_backend.empty()) {
                resp_pool = req_it->second;
            } else {
                auto resp_it = pool_by_name_.find(route_cfg.response_backend);
                if (resp_it == pool_by_name_.end()) {
                    std::cerr << "Route: response pool '" << route_cfg.response_backend << "' not found" << std::endl;
                    continue;
                }
                resp_pool = resp_it->second;
            }

            for (uint16_t opcode : route_cfg.opcodes) {
                router_.add_route(opcode, req_it->second, resp_pool, route_cfg.session);
            }
        }
    }

    void Gateway::on_client_payload(ObscuraProto::net::WsConnectionHdl hdl, ObscuraProto::Payload payload) {
        auto route = router_.find_route(payload.op_code, SessionType::Anonymous);
        if (!route.request_pool)
            return;

        auto node = route.request_pool->select_node(payload.op_code, "", "");
        if (!node)
            return;

        forward_to_backend(node, payload.op_code, payload.parameters, false, hdl, 0, route.response_pool->name());
    }

    void Gateway::on_backend_payload(ObscuraProto::net::WsConnectionHdl hdl, ObscuraProto::Payload payload) {
        auto msg = deserialize_message(payload.parameters);

        switch (msg.type) {
            case MessageType::Response: {
                auto pending = correlation_.resolve(msg.corr_id_or_stream_id);
                if (pending.client_hdl.expired())
                    return;

                ObscuraProto::Payload resp_payload;
                resp_payload.op_code = ObscuraProto::OpCode::RESPONSE;
                resp_payload.parameters = std::move(msg.payload);
                client_server_->send_response(pending.client_hdl, pending.original_req_id, resp_payload);
                break;
            }
            default:
                break;
        }
    }

    void Gateway::forward_to_backend(std::shared_ptr<BackendNode> node,
                                     uint16_t opcode,
                                     const ObscuraProto::byte_vector& params,
                                     bool needs_response,
                                     ObscuraProto::net::WsConnectionHdl client_hdl,
                                     uint32_t original_req_id,
                                     const std::string& response_pool) {
        if (needs_response) {
            uint32_t corr_id = correlation_.register_request(client_hdl, original_req_id, response_pool);
            node->inc_requests();
            InternalMessage msg{MessageType::Request, corr_id, opcode, params};
            node->send_message(*backend_server_, msg);
        } else {
            InternalMessage msg{MessageType::Data, 0, opcode, params};
            node->send_message(*backend_server_, msg);
        }
    }

}
