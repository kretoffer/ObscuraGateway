#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "config.hpp"

namespace obscuragateway {

    class BackendPool;

    struct RouteResult {
        std::shared_ptr<BackendPool> request_pool;
        std::shared_ptr<BackendPool> response_pool;
    };

    class Router {
    public:
        Router() = default;

        void add_route(uint16_t opcode,
                       std::shared_ptr<BackendPool> request_pool,
                       std::shared_ptr<BackendPool> response_pool,
                       SessionType session);

        RouteResult find_route(uint16_t opcode, SessionType session) const;

        static std::set<uint16_t> parse_opcodes(const std::string& spec);

    private:
        struct RouteEntry {
            std::shared_ptr<BackendPool> request_pool;
            std::shared_ptr<BackendPool> response_pool;
            SessionType session;
        };

        std::map<uint16_t, RouteEntry> routes_;
    };

}
