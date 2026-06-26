#include "obscuragateway/router.hpp"

#include <algorithm>
#include <sstream>

#include "obscuragateway/backend_pool.hpp"

namespace obscuragateway {

    void Router::add_route(uint16_t opcode,
                           std::shared_ptr<BackendPool> request_pool,
                           std::shared_ptr<BackendPool> response_pool,
                           SessionType session) {
        routes_[opcode] = {std::move(request_pool), std::move(response_pool), session};
    }

    RouteResult Router::find_route(uint16_t opcode, SessionType session) const {
        auto it = routes_.find(opcode);
        if (it == routes_.end())
            return {nullptr, nullptr};

        if (it->second.session != SessionType::Any && it->second.session != session) {
            return {nullptr, nullptr};
        }

        return {it->second.request_pool, it->second.response_pool};
    }

    std::set<uint16_t> Router::parse_opcodes(const std::string& spec) {
        std::set<uint16_t> result;
        std::stringstream ss(spec);
        std::string token;

        while (std::getline(ss, token, ',')) {
            token.erase(0, token.find_first_not_of(" \t"));
            token.erase(token.find_last_not_of(" \t") + 1);
            if (token.empty())
                continue;

            auto dash_pos = token.find('-');
            if (dash_pos != std::string::npos) {
                uint16_t start = static_cast<uint16_t>(std::stoul(token.substr(0, dash_pos), nullptr, 16));
                uint16_t end = static_cast<uint16_t>(std::stoul(token.substr(dash_pos + 1), nullptr, 16));
                for (uint16_t i = start; i <= end; ++i) {
                    result.insert(i);
                }
            } else {
                uint16_t val = static_cast<uint16_t>(std::stoul(token, nullptr, 16));
                result.insert(val);
            }
        }

        return result;
    }

}
