# ObscuraGateway

**ObscuraGateway** — a high-performance Gateway for [ObscuraProto](https://github.com/kretoffer/ObscuraProto) that serves as an entry point, load balancer, and request router by opcode between clients and internal backend servers.

---

## Architecture

### Connection Diagram

```
Backend (auth-node-1) ──ObscuraProto──►┐
(Ed25519: 0xA...)                      │
Backend (auth-node-2) ──ObscuraProto──►├─── Gateway ────◄── Client
(Ed25519: 0xB...)                      │   :clients_port        │
Backend (storage-1) ──ObscuraProto──►┘   :backends_port        │
(Ed25519: 0xC...)                                     ObscuraProto
                                                       (full crypto)
```

Ports are configured in the config file (`listen.clients` and `listen.backends`).

### Components

```
Gateway
├── Frontend Listener (port 8443)
│   └── ObscuraProto WsServerWrapper
│       ├── Anonymous handlers (registered dynamically via Router)
│       └── Authenticated handlers (registered dynamically via Router)
│
├── Backend Listener (port 9444)
│   └── ObscuraProto WsServerWrapper
│       ├── Accepts backend server connections
│       └── Verifies Ed25519 public key against config
│
├── Router
│   ├── Reads routes from config
│   ├── Mapping: opcode + session_type → (RequestPool, ResponsePool)
│   └── Supports ranges and individual opcodes
│
├── BackendPool (per backend pool)
│   ├── List of active backend connections
│   └── Load balancing strategy (RoundRobin / LeastConnections)
│
├── CorrelationManager
│   ├── Request-Response: correlation_id → (client, original_req_id) map
│   └── Timeout-based cleanup
│
├── StreamManager
│   ├── Binds client stream_id to a backend connection
│   └── Routes STREAM_DATA / STREAM_END / STREAM_CANCEL
│
└── InternalProtocol
    └── Serialization of messages between Gateway and Backend
```

---

## Request Flow

```
Client                         Gateway                   Backend A              Backend B
  │                              │                       (processor)            (responder)
  │                              │                        │                        │
  │  ──── connect :8443 ───────► │                        │                        │
  │  ◄─── ObscuraProto ────────► │                        │                        │
  │       handshake              │                        │                        │
  │                              │                        │                        │
  │                              │  ◄──── connect :9444 ────────────────────────── │
  │                              │  ◄─── ObscuraProto ──────────────────────────── │
  │                              │      handshake         │                        │
  │                              │                        │                        │
  │                              │  ◄──── connect :9444 ───────────────────────────│
  │                              │  ◄─── ObscuraProto ──────────────────────────── │
  │                              │      handshake         │                        │
  │                              │                        │                        │
  │  ──── opcode=0x0003 ───────► │                        │                        │
  │       payload="login"        │                        │                        │
  │                              │                        │                        │
  │                              │  Router: 0x0003        │                        │
  │                              │  → Request: "processor"│                        │
  │                              │  → Response: "responder│                        │
  │                              │                        │                        │
  │                              │  ──── REQUEST ───────────────────────────────►  │
  │                              │       corr_id=42       │                        │
  │                              │       opcode=0x0003    │                        │
  │                              │       payload="login"  │                        │
  │                              │                        │                        │
  │                              │  ◄─── RESPONSE ───────────────────────────────  │
  │                              │       corr_id=42       │                        │
  │                              │       opcode=0x0003    │                        │
  │                              │       payload="ok"     │                        │
  │                              │                        │                        │
  │                              │  ✓ sender = "responder"│                        │
  │                              │ ✓ matches response_pool│                        │
  │                              │                        │                        │
  │  ◄─── RESPONSE ───────────── │                        │                        │
  │       req_id=...             │                        │                        │
  │       payload="ok"           │                        │                        │
```

---

## Configuration

### config.yml

```yaml
gateway:
  listen:
    clients: "0.0.0.0:8443"
    backends: "0.0.0.0:9444"

  backend_pools:
    - name: "auth-processor"
      strategy: "least-connections"
      security: "encrypted"
      backends:
        - id: "proc-node-1"
          public_key: "e61a2c8b9d0f3e7a5b4d2c1f6a8e0d3b7c9f1e4a5b2d8c0f3e7a6b4d9c1f0e"
        - id: "proc-node-2"
          public_key: "a1b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6e7f8a9b0c1d2e3f4a5b6c7d8e9f0a1"

    - name: "auth-responder"
      strategy: "round-robin"
      security: "encrypted"
      backends:
        - id: "resp-node-1"
          public_key: "f0e1d2c3b4a5968778695a4b3c2d1e0f1a2b3c4d5e6f7a8b9c0d1e2f3a4b5c6"

    - name: "storage"
      strategy: "round-robin"
      security: "plain"
      backends:
        - id: "store-node-1"
          public_key: "deadbeef0123456789abcdef0123456789abcdef0123456789abcdef01234567"

  routes:
    - opcodes: "0x0001-0x00FF"
      backend: "auth-processor"
      response_backend: "auth-responder"
      session: "authenticated"

    - opcodes: "0x0100-0x01FF"
      backend: "storage"
      session: "any"
```

### Opcode Format

Each `opcodes` entry is a comma-separated list. Items:

| Format | Example | Description |
|---|---|---|
| Single opcode | `0x0001` | A specific opcode |
| Range | `0x0001-0x00FF` | All opcodes from 0x0001 to 0x00FF inclusive |
| Combination | `0x0001,0x0005-0x000A,0x1000` | Mixed list |

### Session Parameter

| Value | Description |
|---|---|
| `anonymous` | Unauthenticated sessions only |
| `authenticated` | Authenticated sessions only (with Ed25519 identity) |
| `any` | Any session |

### Request/Response Separation

Each route can specify different pools for request processing and response delivery:

```yaml
routes:
  - opcodes: "0x0001-0x00FF"
    backend: "auth-processor"       # request goes here
    response_backend: "auth-responder"  # response comes from here
```

The `response_backend` parameter is optional. If not specified, Gateway expects the response from the same pool as the request.

**When this is useful:**

| Scenario | Request pool | Response pool | Description |
|---|---|---|---|
| Async processing | workers | notifiers | Request is queued, response comes from notification server |
| Separation of concerns | validators | executors | Request validation by one server, execution and response by another |
| Fan-out | dispatcher | collector | Request is broadcast, response is collected by an aggregator |

**Security:** Gateway verifies that the response comes from the pool specified in `response_backend`. If the response comes from a different pool, it is rejected:

```
[Gateway] Request routed to pool "auth-processor" (expects response from "auth-responder")
[Gateway] Response from "auth-processor" → rejected (not authorized to respond)
[Gateway] Response from "auth-responder" → accepted, forwarded to client
```

---

## Internal Protocol (Gateway ↔ Backend)

Messages over WebSocket binary frames. Format:

```
[4 bytes: message_type] [payload...]
```

### Message Types

| Type | Value | Direction | Format |
|---|---|---|---|
| `DATA` | 1 | Gateway→Backend | `[type][opcode:2][params...]` |
| `REQUEST` | 2 | Gateway→Backend | `[type][corr_id:4][opcode:2][params...]` |
| `RESPONSE` | 3 | Backend→Gateway | `[type][corr_id:4][opcode:2][params...]` |
| `STREAM_START` | 4 | Both directions | `[type][stream_id:4][opcode:2][params...]` |
| `STREAM_DATA` | 5 | Both directions | `[type][stream_id:4][data...]` |
| `STREAM_END` | 6 | Both directions | `[type][stream_id:4]` |
| `STREAM_CANCEL` | 7 | Both directions | `[type][stream_id:4]` |

All multi-byte values are in **network byte order (big-endian)**.

### Message Handling

- **DATA** — fire-and-forget. Gateway does not wait for a response, simply forwards.
- **REQUEST** — a request requiring a response. Backend must return RESPONSE with the same `corr_id`.
- **RESPONSE** — response to a request. Gateway maps `corr_id` back to the client.
- **STREAM_*** — streaming. `stream_id` is assigned by the initiator (with even/odd parity, as in ObscuraProto). Gateway forwards all packets of one stream to the same backend.

---

## Security

### Client-to-Gateway Encryption

Uses the full ObscuraProto stack:
- **Key Exchange:** X25519 ECDH
- **Identity:** Ed25519 signatures (optional for clients)
- **AEAD:** ChaCha20-Poly1305 (IETF variant)
- **Replay Protection:** Counter + nonce (AEAD associated data)

### Gateway-to-Backend Encryption

Two modes (configurable in config):

| Mode | Security | CPU overhead | Overhead size |
|---|---|---|---|
| `plain` | None (trusted network) | ~0% | 0 |
| `encrypted` | Full ObscuraProto handshake + session | ~3-5% per core | 28 bytes/message |

**Recommendations:**
- `plain` — gateway and backends on the same host, in a Kubernetes pod, or in an isolated VLAN
- `encrypted` — over public/semi-public networks, or when compliance is required (PCI DSS, SOC2, etc.)

**Handshake** (X25519 + Ed25519) takes ~0.5-2ms, but is performed once per connection lifetime (hours/days), so overhead is negligible.

---

## Load Balancing

All strategies are configured in the config via the pool's `strategy` field.

### Round-robin

Requests are distributed in a circular order:

```
backend-1 → backend-2 → backend-3 → backend-1 → ...
```

Suitable for:
- Homogeneous requests
- Equal-capacity backends
- Simplest distribution

### Least-connections

Requests are sent to the backend with the fewest active requests at the moment:

```
backend-1: 12 active requests
backend-2:  3 active requests  ← new request goes here
backend-3:  9 active requests
```

Gateway tracks `active_requests` per backend connection: +1 on REQUEST send, -1 on RESPONSE receipt.

Suitable for:
- Variable-complexity requests
- Equal-capacity backends
- Peak loads

### Random

Random backend selection from the pool. The simplest stateless strategy:

```
pick = rand() % pool.size()
```

**Pros:** no counters needed, minimal overhead.
**Cons:** no distribution guarantees.

Suitable for:
- Prototypes and tests
- When even distribution is not critical

### Weighted

Distribution proportional to backend weight. Weight is set in the config per backend (default 100):

```yaml
backends:
  - id: "node-1"
    public_key: "..."
    weight: 200    # receives twice as much traffic
  - id: "node-2"
    public_key: "..."
    weight: 100    # base unit
  - id: "node-3"
    public_key: "..."
    weight: 100
```

Final proportion: 200/400 : 100/400 : 100/400 = 50% : 25% : 25%.

Implemented via **smooth weighted round-robin** (nginx algorithm):

| Step | node-1 (200) | node-2 (100) | node-3 (100) | Selected |
|---|---|---|---|---|
| 1 | +200 → 200 ✓ | +100 → 100 | +100 → 100 | node-1 (-400 → -200) |
| 2 | +200 → 0 | +100 → 200 ✓ | +100 → 200 | node-2 (-400 → -200) |
| 3 | +200 → -100 | +100 → -100 | +100 → 300 ✓ | node-3 (-400 → -100) |
| ... | | | | |

This provides precise distribution without "bursts" of requests to one backend.

Suitable for:
- Heterogeneous clusters (different CPU/RAM)
- Canary deployments (one backend gets 1% of traffic)
- Gradual rollouts

### Consistent-hash

Requests are directed to a backend based on a hash of the key (`hash_key`). The same key always goes to the same backend as long as the pool composition does not change.

Configuration:

```yaml
backend_pools:
  - name: "cache"
    strategy: "consistent-hash"
    hash_key: "client_id"     # client_id | client_ip | opcode
    backends:
      - id: "cache-1"
        public_key: "..."
      - id: "cache-2"
        public_key: "..."
      - id: "cache-3"
        public_key: "..."
```

Possible `hash_key` values:

| Key | Source | Effect |
|---|---|---|
| `client_id` | Ed25519 pubkey of the client | All requests from one client → one backend |
| `client_ip` | Client IP address | All requests from one IP → one backend |
| `opcode` | Request opcode | A specific operation always goes to the same backend |

Uses the **ketama** algorithm (consistent hashing with virtual nodes):

```
hash("cache-1:0"), hash("cache-1:1"), ..., hash("cache-1:159")
hash("cache-2:0"), hash("cache-2:1"), ..., hash("cache-2:159")
...
→ sort the hash ring
→ for a request: hash(key) → find the next entry on the ring
```

When a backend is added or removed, only **1/N** of requests are redistributed (vs 100% for round-robin).

Suitable for:
- Caching backends (in-memory cache)
- Session data (sticky sessions)
- Client-bound state

---

## Backend Authentication

Gateway identifies backends by their **Ed25519 public key**:

1. Backend connects to Gateway (`:9444`)
2. ObscuraProto handshake is performed
3. Gateway receives the backend's public key
4. Gateway looks up the key in the config (`backend_pools[].backends[].public_key`)
5. If found, the backend is added to the corresponding pool
6. If not found, the connection is rejected

---

## Message Patterns

### Fire-and-forget (DATA)

Client → Gateway → Backend. No response is expected.

```
Client: opcode=0x0200, params=[event_data]  ──►  Gateway  ──►  Backend
```

### Request-Response (REQUEST / RESPONSE)

Client sends a request and expects a response. Gateway correlates via `correlation_id`.

```
Client: opcode=0x0003, params=[req_id, ...]  ──►  Gateway
                                                     │
                                            REQUEST corr_id=42
                                                     │
                                                     ├──► Backend
                                                     │◄── RESPONSE corr_id=42
                                                     │
Client: RESPONSE, params=[req_id, ...]      ◄──  Gateway
```

### Streaming (STREAM_*)

Bidirectional data stream between client and backend.

```
Client: STREAM_START, opcode, params  ──►  Gateway  ──►  Backend
Client: STREAM_DATA, data             ──►  Gateway  ──►  Backend
Client: STREAM_END                    ──►  Gateway  ──►  Backend
Backend: STREAM_DATA, data            ──►  Gateway  ──►  Client
...
```

Gateway guarantees that all packets of one stream go to the same backend.

---

## Project

### Structure

```
ObscuraGateway/
├── CMakeLists.txt
├── config.yml                          # Example configuration
├── include/obscuragateway/
│   ├── gateway.hpp                     # Main Gateway class
│   ├── config.hpp                      # Configuration parser
│   ├── router.hpp                      # Opcode → pool routing
│   ├── backend_pool.hpp                # Backend pool + load balancing
│   ├── backend_node.hpp                # Single backend connection
│   ├── correlation.hpp                 # Request-ID correlation
│   ├── stream_manager.hpp              # Stream management
│   └── internal_protocol.hpp           # Internal protocol
├── src/
│   ├── main.cpp                        # Entry point
│   ├── gateway.cpp
│   ├── config.cpp
│   ├── router.cpp
│   ├── backend_pool.cpp
│   ├── backend_node.cpp
│   ├── correlation.cpp
│   ├── stream_manager.cpp
│   └── internal_protocol.cpp
├── tests/
└── examples/
```

### Dependencies

| Library | Purpose | Source |
|---|---|---|
| obscuraproto | WsServerWrapper, Payload, Session, Crypto | [ObscuraProto](https://github.com/kretoffer/ObscuraProto) |
| yaml-cpp | YAML config parsing | External / vcpkg |
| websocketpp | WebSocket transport | Transitively via ObscuraProto |
| libsodium | Cryptography | Transitively via ObscuraProto |

### Build

```bash
git clone https://github.com/kretoffer/ObscuraGateway.git
cd ObscuraGateway
cmake -B build
cmake --build build
```

---

## Backend Library (separate repository)

A separate library will be created to simplify backend development. It will provide:

- Lightweight WebSocket client (without full crypto stack if `security: plain`)
- Convenient API for registering opcode handlers
- Automatic reconnection to Gateway on disconnection
- Support for all three patterns (fire-and-forget, request-response, streaming)

This allows backend developers to focus on business logic without delving into protocol details.

---

## License

[License](/LICENSE)
