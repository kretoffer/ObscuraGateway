# ObscuraGateway Configuration

Configuration is specified in a YAML file and passed to the gateway at startup:

```bash
obscuragatewayd /path/to/config.yml
```

## Config Structure

```yaml
gateway:
  listen:
    clients: "0.0.0.0:8443"
    backends: "0.0.0.0:9444"

  backend_pools:
    - name: "..."
      strategy: "..."
      security: "..."
      hash_key: "..."        # consistent-hash only
      backends:
        - id: "..."
          public_key: "..."
          weight: 100        # weighted only

  routes:
    - opcodes: "..."
      backend: "..."
      response_backend: "..."  # optional
      session: "..."
```

---

## listen

```yaml
listen:
  clients: "0.0.0.0:8443"     # address:port for clients
  backends: "0.0.0.0:9444"    # address:port for backends
```

| Field | Default | Description |
|---|---|---|
| `clients` | `0.0.0.0:8443` | Interface and port for client connections |
| `backends` | `0.0.0.0:9444` | Interface and port for backend connections |

---

## backend_pools

List of backend server pools. Each pool is a group of servers with a shared load balancing strategy.

```yaml
backend_pools:
  - name: "auth"
    strategy: "least-connections"
    security: "encrypted"
    backends: [...]

  - name: "cache"
    strategy: "consistent-hash"
    hash_key: "client_id"
    backends: [...]
```

### Pool Parameters

| Field | Required | Default | Description |
|---|---|---|---|
| `name` | yes | — | Unique pool name |
| `strategy` | no | `round-robin` | Load balancing strategy |
| `security` | no | `plain` | Encryption mode |
| `hash_key` | no | `client_id` | Hashing key (for `consistent-hash`) |
| `backends` | yes | — | List of backends |

### strategy

| Value | Description |
|---|---|
| `round-robin` | Circular distribution |
| `least-connections` | Fewest active requests |
| `random` | Random selection |
| `weighted` | Proportional to weight |
| `consistent-hash` | Key-based hash |

### security

| Value | Description |
|---|---|
| `plain` | No encryption (maximum performance) |
| `encrypted` | Full ObscuraProto handshake + ChaCha20-Poly1305 |

### hash_key

For `strategy: consistent-hash`:

| Value | Source | Effect |
|---|---|---|
| `client_id` | Client's Ed25519 public key | Client always goes to the same backend |
| `client_ip` | Client IP address | IP always goes to the same backend |
| `opcode` | Request opcode | Operation always goes to the same backend |

### backends

List of servers in the pool:

```yaml
backends:
  - id: "node-1"               # unique identifier
    public_key: "e61a..."      # Ed25519 public key (64 hex chars)
    weight: 200                # weight (strategy: weighted only)
```

| Field | Required | Default | Description |
|---|---|---|---|
| `id` | yes | — | Unique backend ID within the pool |
| `public_key` | yes | — | Ed25519 public key (64 hex, 32 bytes) |
| `weight` | no | `100` | Load balancing weight (`weighted` only) |

---

## routes

Opcode → pool mapping. May contain multiple routes.

```yaml
routes:
  - opcodes: "0x0001-0x0005,0x000A"
    backend: "auth"
    session: "anonymous"

  - opcodes: "0x0006-0x00FF"
    backend: "auth-processor"
    response_backend: "auth-responder"
    session: "authenticated"
```

### Route Parameters

| Field | Required | Default | Description |
|---|---|---|---|
| `opcodes` | yes | — | Opcode list (see format below) |
| `backend` | yes | — | Pool name for sending the request |
| `response_backend` | no | same as `backend` | Pool name for receiving the response |
| `session` | no | `any` | Session type |

### Opcode Format

A comma-separated string of opcodes:

| Format | Example | Description |
|---|---|---|
| Single | `0x0001` | Hexadecimal number |
| Range | `0x0001-0x00FF` | Inclusive range |
| Combination | `0x0001,0x0005-0x000A,0x1000` | Mixed list |

### session

| Value | Description |
|---|---|
| `anonymous` | Unauthenticated clients only |
| `authenticated` | Authenticated clients only (Ed25519 identity) |
| `any` | Any clients |

### response_backend

Allows specifying a different pool for receiving the response:

```yaml
routes:
  - opcodes: "0x0100-0x01FF"
    backend: "workers"          # request → workers
    response_backend: "gateway" # response → another service
```

If not specified, Gateway expects the response from the same pool.

---

## Full Example

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
      strategy: "consistent-hash"
      hash_key: "client_id"
      security: "plain"
      backends:
        - id: "store-node-1"
          public_key: "deadbeef0123456789abcdef0123456789abcdef0123456789abcdef01234567"
        - id: "store-node-2"
          public_key: "cafebabe0123456789abcdef0123456789abcdef0123456789abcdef01234567"

  routes:
    - opcodes: "0x0001-0x0005,0x000A"
      backend: "auth-processor"
      response_backend: "auth-responder"
      session: "anonymous"

    - opcodes: "0x0006-0x00FF"
      backend: "auth-processor"
      response_backend: "auth-responder"
      session: "authenticated"

    - opcodes: "0x0100-0x01FF"
      backend: "storage"
      session: "any"
```
