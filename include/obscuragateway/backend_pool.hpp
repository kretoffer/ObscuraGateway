#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <obscuraproto/packet.hpp>
#include <string>
#include <vector>

#include "backend_node.hpp"
#include "config.hpp"

namespace obscuragateway {

    class BackendPool {
    public:
        using IncomingHandler = std::function<void(BackendNode&, const ObscuraProto::Payload&)>;

        explicit BackendPool(const BackendPoolConfig& config);

        void add_node(std::shared_ptr<BackendNode> node);
        void remove_node(const std::string& id);
        std::shared_ptr<BackendNode> select_node(uint16_t opcode,
                                                 const std::string& client_id,
                                                 const std::string& client_ip);
        size_t size() const;

        const std::string& name() const {
            return config_.name;
        }
        LBStrategy strategy() const {
            return config_.strategy;
        }

    private:
        std::shared_ptr<BackendNode> round_robin();
        std::shared_ptr<BackendNode> least_connections();
        std::shared_ptr<BackendNode> random();
        std::shared_ptr<BackendNode> weighted();
        std::shared_ptr<BackendNode> consistent_hash(uint16_t opcode,
                                                     const std::string& client_id,
                                                     const std::string& client_ip);

        BackendPoolConfig config_;
        std::vector<std::shared_ptr<BackendNode>> nodes_;
        mutable std::mutex mutex_;
        size_t rr_counter_ = 0;
    };

}
