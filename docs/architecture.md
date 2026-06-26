# ObscuraGateway Architecture

## Overview Diagram

```
┌──────────────────┐   ObscuraProto    ┌──────────────────┐
│    Backend A     │◄────── WS ───────►│                  │
│  (auth-processor)│   :backends_port  │                  │
└──────────────────┘                   │                  │
┌──────────────────┐                   │    Gateway       │   ObscuraProto   ┌──────────┐
│    Backend B     │◄────── WS ───────►│  (WsServer)      │◄───── WS ───────►│  Client  │
│  (auth-responder)│   :backends_port  │  :clients_port   │   :clients_port  └──────────┘
└──────────────────┘                   │                  │
┌──────────────────┐                   │                  │
│    Backend C     │◄────── WS ───────►│                  │
│   (storage)      │   :backends_port  └──────────────────┘
└──────────────────┘
```

## Key Decisions

### 1. Backend → Gateway (reverse connection)

Backends establish the connection to Gateway, not the other way around.

**Advantages:**
- Gateway does not need to know backend addresses in advance
- Backends can dynamically connect and disconnect (auto-scaling)
- Gateway does not waste resources on reconnection when a backend goes down
- Simpler configuration: all backends only need the gateway address

### 2. Two Ports

Gateway listens on two ports (configurable in the config):

| Port | Purpose | Authentication |
|---|---|---|
| `clients` (8443) | External clients | Full ObscuraProto handshake |
| `backends` (9444) | Internal backends | Ed25519 public key from config |

Separating ports allows:
- Blocking the backend port from external access via firewall
- Using different encryption settings
- Logging and monitoring ports separately

### 3. Encryption Termination at Gateway

Gateway decrypts client requests and forwards them to backends.

**Why not end-to-end encryption:**
- Gateway must inspect the opcode for routing
- Backends can be simplified (without full crypto stack)
- Enables async processing (request and response on different backends)

### 4. Connection Pooling

Gateway maintains persistent WebSocket connections to backends. Different clients share these connections.

**Advantages:**
- No handshake overhead per request
- Efficient resource utilization
- Backends serve N clients through a single connection

## Components

### Gateway (main class)

```cpp
class Gateway {
    WsServerWrapper client_server_;   // client handling
    WsServerWrapper backend_server_;  // backend handling
    Router router_;                   // routing
    std::vector<BackendPool> pools_;  // backend pools
    CorrelationManager correlation_;  // correlation_id tracking
    StreamManager stream_manager_;    // streams
};
```

### Frontend Listener

WsServerWrapper from ObscuraProto, registering handlers that forward requests to the Router.

Handles:
- Anonymous requests (before authentication)
- Authenticated requests
- Fire-and-forget, request-response, streaming

### Backend Listener

A second WsServerWrapper that accepts connections from backends.

On each connect:
1. Receives Ed25519 public key from the handshake
2. Looks up the key in the config
3. Adds the backend to the corresponding pool
4. Registers a handler for internal messages

### Router

Stores opcode → Route mapping:

```cpp
struct RouteEntry {
    BackendPool* request_pool;   // pool for sending the request
    BackendPool* response_pool;  // pool from which to expect the response
    SessionType session;
};
```

Supports:
- Individual opcodes
- Opcode ranges
- Session type (anonymous / authenticated / any)

### BackendPool

Manages a group of backend connections:

```cpp
class BackendPool {
    std::vector<BackendNode> nodes_;
    LBStrategy strategy_;  // round-robin / least-connections / ...
    
    BackendNode* select(opcode, client_id, client_ip);
    void add_node(BackendNode);
    void remove_node(id);
};
```

### BackendNode

A single WebSocket connection to a backend:

```cpp
class BackendNode {
    WsClientWrapper client_;    // ObscuraProto client
    std::string id_;            // from config
    std::atomic<uint32_t> active_requests_;
    
    void send_data(opcode, params);       // fire-and-forget
    void send_request(corr_id, opcode, params);  // request-response
    void send_response(corr_id, opcode, params);
    void send_stream_start(stream_id, opcode, params);
    void send_stream_data(stream_id, data);
    void send_stream_end(stream_id);
    void send_stream_cancel(stream_id);
};
```

### CorrelationManager

Maps backend responses to waiting clients:

```cpp
struct PendingRequest {
    WsConnectionHdl client_hdl;
    uint32_t original_req_id;
    std::string response_pool;  // which pool is authorized to respond
    TimePoint created;
};

class CorrelationManager {
    uint32_t register_request(client_hdl, req_id, response_pool);
    PendingRequest resolve(corr_id);
    void cleanup_expired(timeout);
};
```

### StreamManager

Binds a client stream to a specific backend connection:

```cpp
class StreamManager {
    void register(client_stream_id, node, backend_stream_id, opcode);
    void resolve(client_stream_id) → {node, opcode};
    void remove(client_stream_id);
};
```

## Request Lifecycle

```
1. Backend auth-processor connects to gateway:9444
2. Gateway: handshake → Ed25519 pubkey → looks up in config → adds to "auth-processor" pool
3. Backend auth-responder connects → added to "auth-responder" pool
4. Client connects to gateway:8443
5. Client → Gateway: opcode=0x0003, params=[login, password]
6. Gateway: find_route(0x0003, authenticated) → {request_pool="auth-processor", response_pool="auth-responder"}
7. Gateway: pool("auth-processor").select() → proc-node-1
8. Gateway → proc-node-1: REQUEST corr_id=42, opcode=0x0003, params
9. Gateway: correlation_.register(client_hdl, req_id, "auth-responder") → corr_id=42
10. Backend processes and responds
11. resp-node-1 → Gateway: RESPONSE corr_id=42, opcode=0x0003, params
12. Gateway: resolve(42) → PendingRequest{client_hdl, req_id, "auth-responder"}
13. Gateway: find_pool(resp-node-1) → "auth-responder" ✓ matches
14. Gateway → Client: RESPONSE original_req_id, params
```

## Threading Model

```
┌──────────────────────────────────────────────────┐
│  Main Thread                                     │
│  ├── Starts client_server_run() in a separate    │
│  │   thread                                      │
│  ├── Starts backend_server_() in a separate      │
│  │   thread                                      │
│  └── Waits for signal (SIGINT/SIGTERM)           │
└──────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────┐
│  Client Server Thread (websocketpp event loop)   │
│  ├── Accepts client connections                  │
│  ├── Handshake                                   │
│  ├── Receives messages                           │
│  ├── Calls callbacks (synchronously)             │
│  └── Sends responses                             │
└──────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────┐
│  Backend Server Thread (websocketpp event loop)  │
│  ├── Accepts backend connections                 │
│  ├── Handshake + Ed25519 verification            │
│  ├── Receives internal messages                  │
│  ├── Correlation resolve                         │
│  └── Sends responses to clients                  │
└──────────────────────────────────────────────────┘
```

Client and backend server callbacks run in their respective event loop threads. CorrelationManager, Router, and StreamManager are protected by mutexes.
