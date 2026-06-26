#include "obscuragateway/backend_node.hpp"

#include <obscuraproto/ws_server.hpp>

#include "obscuragateway/internal_protocol.hpp"

namespace obscuragateway {

    BackendNode::BackendNode(const BackendConfig& config, ObscuraProto::net::WsConnectionHdl hdl)
        : config_(config), hdl_(std::move(hdl)) {
    }

    void BackendNode::send_message(ObscuraProto::net::WsServerWrapper& server, const InternalMessage& msg) {
        auto data = serialize_message(msg);
        ObscuraProto::Payload payload;
        payload.op_code = 0;  // internal protocol marker
        payload.parameters = std::move(data);
        server.send(hdl_, payload);
    }

}
