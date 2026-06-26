#include "obscuragateway/backend_pool.hpp"

#include <algorithm>
#include <random>
#include <sstream>
#include <vector>

namespace obscuragateway {

    BackendPool::BackendPool(const BackendPoolConfig& config) : config_(config) {
    }

    void BackendPool::add_node(std::shared_ptr<BackendNode> node) {
        std::lock_guard<std::mutex> lock(mutex_);
        nodes_.push_back(std::move(node));
    }

    void BackendPool::remove_node(const std::string& id) {
        std::lock_guard<std::mutex> lock(mutex_);
        nodes_.erase(std::remove_if(nodes_.begin(), nodes_.end(), [&](const auto& n) { return n->id() == id; }),
                     nodes_.end());
    }

    std::shared_ptr<BackendNode> BackendPool::select_node(uint16_t opcode,
                                                          const std::string& client_id,
                                                          const std::string& client_ip) {
        switch (config_.strategy) {
            case LBStrategy::RoundRobin:
                return round_robin();
            case LBStrategy::LeastConnections:
                return least_connections();
            case LBStrategy::Random:
                return random();
            case LBStrategy::Weighted:
                return weighted();
            case LBStrategy::ConsistentHash:
                return consistent_hash(opcode, client_id, client_ip);
        }
        return nullptr;
    }

    size_t BackendPool::size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return nodes_.size();
    }

    std::shared_ptr<BackendNode> BackendPool::round_robin() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (nodes_.empty())
            return nullptr;
        size_t idx = rr_counter_++ % nodes_.size();
        return nodes_[idx];
    }

    std::shared_ptr<BackendNode> BackendPool::least_connections() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (nodes_.empty())
            return nullptr;
        return *std::min_element(nodes_.begin(), nodes_.end(), [](const auto& a, const auto& b) {
            return a->active_requests() < b->active_requests();
        });
    }

    std::shared_ptr<BackendNode> BackendPool::random() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (nodes_.empty())
            return nullptr;
        static thread_local std::mt19937 gen(std::random_device{}());
        std::uniform_int_distribution<size_t> dist(0, nodes_.size() - 1);
        return nodes_[dist(gen)];
    }

    std::shared_ptr<BackendNode> BackendPool::weighted() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (nodes_.empty())
            return nullptr;

        static thread_local std::mt19937 gen(std::random_device{}());
        uint32_t total_weight = 0;
        for (const auto& n : nodes_)
            total_weight += n->weight();
        std::uniform_int_distribution<uint32_t> dist(0, total_weight - 1);
        uint32_t pick = dist(gen);

        for (const auto& n : nodes_) {
            if (pick < n->weight())
                return n;
            pick -= n->weight();
        }
        return nodes_.back();
    }

    std::shared_ptr<BackendNode> BackendPool::consistent_hash(uint16_t opcode,
                                                              const std::string& client_id,
                                                              const std::string& client_ip) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (nodes_.empty())
            return nullptr;

        std::string key;
        switch (config_.hash_key) {
            case HashKey::ClientId:
                key = client_id;
                break;
            case HashKey::ClientIp:
                key = client_ip;
                break;
            case HashKey::Opcode:
                key = std::to_string(opcode);
                break;
        }

        std::hash<std::string> hasher;
        size_t idx = hasher(key) % nodes_.size();
        return nodes_[idx];
    }

}
