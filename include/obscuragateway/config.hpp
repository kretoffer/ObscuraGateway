#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace obscuragateway {

    enum class LBStrategy { RoundRobin, LeastConnections, Random, Weighted, ConsistentHash };

    enum class SecurityMode { Plain, Encrypted };

    enum class SessionType { Anonymous, Authenticated, Any };

    enum class HashKey { ClientId, ClientIp, Opcode };

    struct BackendConfig {
        std::string id;
        std::string public_key;
        uint32_t weight = 100;
    };

    struct BackendPoolConfig {
        std::string name;
        LBStrategy strategy = LBStrategy::RoundRobin;
        HashKey hash_key = HashKey::ClientId;
        SecurityMode security = SecurityMode::Plain;
        std::vector<BackendConfig> backends;
    };

    struct RouteConfig {
        std::set<uint16_t> opcodes;
        std::string request_backend;
        std::string response_backend;  // пусто = то же, что request_backend
        SessionType session = SessionType::Any;
    };

    struct ListenConfig {
        std::string clients = "0.0.0.0:8443";
        std::string backends = "0.0.0.0:9444";
    };

    struct GatewayConfig {
        ListenConfig listen;
        std::vector<BackendPoolConfig> pools;
        std::vector<RouteConfig> routes;
    };

    GatewayConfig parse_config(const std::string& path);

}
