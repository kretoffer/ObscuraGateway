#include "obscuragateway/config.hpp"

#include <yaml-cpp/yaml.h>

#include <stdexcept>

#include "obscuragateway/router.hpp"

namespace obscuragateway {

    static LBStrategy parse_strategy(const std::string& s) {
        if (s == "round-robin")
            return LBStrategy::RoundRobin;
        if (s == "least-connections")
            return LBStrategy::LeastConnections;
        if (s == "random")
            return LBStrategy::Random;
        if (s == "weighted")
            return LBStrategy::Weighted;
        if (s == "consistent-hash")
            return LBStrategy::ConsistentHash;
        throw std::invalid_argument("Unknown strategy: " + s);
    }

    static SecurityMode parse_security(const std::string& s) {
        if (s == "plain")
            return SecurityMode::Plain;
        if (s == "encrypted")
            return SecurityMode::Encrypted;
        throw std::invalid_argument("Unknown security mode: " + s);
    }

    static SessionType parse_session(const std::string& s) {
        if (s == "anonymous")
            return SessionType::Anonymous;
        if (s == "authenticated")
            return SessionType::Authenticated;
        if (s == "any")
            return SessionType::Any;
        throw std::invalid_argument("Unknown session type: " + s);
    }

    static HashKey parse_hash_key(const std::string& s) {
        if (s == "client_id")
            return HashKey::ClientId;
        if (s == "client_ip")
            return HashKey::ClientIp;
        if (s == "opcode")
            return HashKey::Opcode;
        throw std::invalid_argument("Unknown hash key: " + s);
    }

    GatewayConfig parse_config(const std::string& path) {
        YAML::Node root = YAML::LoadFile(path);
        auto gw = root["gateway"];

        GatewayConfig config;

        auto listen = gw["listen"];
        config.listen.clients = listen["clients"].as<std::string>("0.0.0.0:8443");
        config.listen.backends = listen["backends"].as<std::string>("0.0.0.0:9444");

        for (const auto& pool_node : gw["backend_pools"]) {
            BackendPoolConfig pool;
            pool.name = pool_node["name"].as<std::string>();
            pool.strategy = parse_strategy(pool_node["strategy"].as<std::string>("round-robin"));
            pool.security = parse_security(pool_node["security"].as<std::string>("plain"));

            if (pool_node["hash_key"]) {
                pool.hash_key = parse_hash_key(pool_node["hash_key"].as<std::string>());
            }

            for (const auto& backend_node : pool_node["backends"]) {
                BackendConfig backend;
                backend.id = backend_node["id"].as<std::string>();
                backend.public_key = backend_node["public_key"].as<std::string>();
                backend.weight = backend_node["weight"].as<uint32_t>(100);
                pool.backends.push_back(std::move(backend));
            }

            config.pools.push_back(std::move(pool));
        }

        for (const auto& route_node : gw["routes"]) {
            RouteConfig route;
            route.opcodes = Router::parse_opcodes(route_node["opcodes"].as<std::string>());
            route.request_backend = route_node["backend"].as<std::string>();
            if (route_node["response_backend"]) {
                route.response_backend = route_node["response_backend"].as<std::string>();
            }
            route.session = parse_session(route_node["session"].as<std::string>("any"));
            config.routes.push_back(std::move(route));
        }

        return config;
    }

}
