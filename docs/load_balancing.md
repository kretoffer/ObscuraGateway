# Load Balancing in ObscuraGateway

## Overview

Each backend pool (`backend_pool`) has a load balancing strategy that determines how Gateway selects a backend to handle a request.

```yaml
backend_pools:
  - name: "auth"
    strategy: "least-connections"
    backends: [...]
```

## Strategies

---

### 1. Round-robin

**When to use:** Homogeneous requests, equal-capacity backends.

Requests are distributed in a circular order:

```
Request 1 → backend-1
Request 2 → backend-2
Request 3 → backend-3
Request 4 → backend-1
Request 5 → backend-2
...
```

**Implementation:**

```cpp
BackendNode* round_robin() {
    size_t idx = rr_counter_++ % nodes_.size();
    return nodes_[idx];
}
```

**Pros:** Simple implementation, predictable distribution.
**Cons:** Does not account for backend load, unsuitable for variable-complexity requests.

---

### 2. Least-connections

**When to use:** Variable-complexity requests, different-capacity backends, peak loads.

Gateway sends the request to the backend with the fewest active requests:

```
backend-1: 12 active requests
backend-2:  3 active requests  ← new request goes here
backend-3:  9 active requests
```

**Active request counter:**

| Event | Action |
|---|---|
| Gateway sent REQUEST | `node.active_requests++` |
| Gateway received RESPONSE | `node.active_requests--` |
| Request timeout | `node.active_requests--` |
| Connection drop | All `active_requests` reset |

**Implementation:**

```cpp
BackendNode* least_connections() {
    return *std::min_element(nodes_.begin(), nodes_.end(),
        [](auto& a, auto& b) {
            return a.active_requests() < b.active_requests();
        });
}
```

**Pros:** Adapts to actual load, effective under variable load.
**Cons:** Requires counter synchronization, does not account for backend weight.

---

### 3. Random

**When to use:** Prototypes, tests, when even distribution is not critical.

```cpp
BackendNode* random() {
    std::uniform_int_distribution<size_t> dist(0, nodes_.size() - 1);
    return nodes_[dist(gen)];
}
```

**Pros:** Zero overhead, no state required.
**Cons:** No distribution guarantees, possible "bursts" on one backend.

---

### 4. Weighted

**When to use:** Heterogeneous clusters, canary deployments, gradual rollouts.

Distribution proportional to backend weight:

```yaml
backends:
  - id: "node-1"   # weight 200 → 50% traffic
    weight: 200
  - id: "node-2"   # weight 100 → 25% traffic
    weight: 100
  - id: "node-3"   # weight 100 → 25% traffic
    weight: 100
```

**Algorithm: Smooth Weighted Round-Robin (nginx)**

Each backend has:
- `weight` — configured weight (from config)
- `current_weight` — changes on each step

Algorithm step:
1. For each backend: `current_weight += weight`
2. Select the backend with the highest `current_weight`
3. For the selected one: `current_weight -= total_weight`

**Example with weights 200, 100, 100:**

| Step | node-1 (200) | node-2 (100) | node-3 (100) | Selected |
|---|---|---|---|---|
| 1 | **+200 → 200** | +100 → 100 | +100 → 100 | node-1 (-400 → **-200**) |
| 2 | +200 → **0** | +100 → **200** | +100 → 200 | node-2 (-400 → **-200**) |
| 3 | +200 → **-100** | +100 → **-100** | +100 → **300** | node-3 (-400 → **-100**) |
| 4 | +200 → **100** | +100 → **0** | +100 → **0** | node-1 (-400 → **-300**) |
| 5 | +200 → **-100** | +100 → **100** | +100 → **100** | node-2 (-400 → **-300**) |
| 6 | +200 → **100** | +100 → **-200** | +100 → **200** | node-1 (-400 → **-300**) |

Result: `N1, N2, N3, N1, N2, N1` — ideal proportion 3:2:2 over 6 requests = 50%:33%:17% (for weights 200:100:100 it would be 50:25:25 in an infinite sample).

**Implementation:**

```cpp
BackendNode* weighted() {
    uint32_t total = sum(nodes_.weight);
    uint32_t pick = random(0, total - 1);
    for (auto& node : nodes_) {
        if (pick < node.weight()) return node;
        pick -= node.weight();
    }
    return nodes_.back();
}
```

---

### 5. Consistent-hash

**When to use:** Caching backends, session data, sticky sessions.

Requests are directed to a backend based on a hash of the key:

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

**Hash key:**

| Value | Hashed | Effect |
|---|---|---|
| `client_id` | Client's Ed25519 pubkey | All requests from one client → one backend |
| `client_ip` | Client IP | All requests from one IP → one backend |
| `opcode` | Request opcode | Specific operation → one backend |

**Algorithm: Ketama consistent hashing**

```
virtual_nodes = 160 per backend

for each backend:
    for i in 0..159:
        hash = hash("backend.id:i")
        ring.insert(hash, backend)

for each request:
    key = hash(request.hash_key)
    backend = ring.next(key)  # next on the ring
```

**Benefits of consistent-hash:**

When a backend is added or removed, only **1/N** of requests are redistributed (where N is the number of backends). For comparison:

| Operation | Round-robin | Consistent-hash |
|---|---|---|
| Adding backend-4 | 100% of requests redistributed | ~25% of requests redistributed |
| Removing backend-2 | 100% | ~25% |

**Simplified implementation:**

```cpp
BackendNode* consistent_hash(opcode, client_id, client_ip) {
    std::string key;
    switch (hash_key) {
        case ClientId: key = client_id; break;
        case ClientIp: key = client_ip; break;
        case Opcode:   key = std::to_string(opcode); break;
    }
    
    size_t idx = std::hash<std::string>(key) % nodes_.size();
    return nodes_[idx];
}
```

**For production**, a full implementation with a ring and virtual nodes is required (160 virtual nodes per backend, as in libketama).

---

## Strategy Selection

| Scenario | Recommended Strategy |
|---|---|
| All backends identical, simple requests | `round-robin` |
| Variable-complexity requests | `least-connections` |
| Different-capacity servers | `weighted` |
| Caching / sessions | `consistent-hash` |
| Tests / prototypes | `random` |
| Canary deployment (2% traffic) | `weighted` (weight=2 vs 98) |

## Combining with response_pool

Request and Response can use different pools with different strategies:

```yaml
backend_pools:
  - name: "workers"
    strategy: "least-connections"  # balance worker load
    backends: [...]

  - name: "notifiers"
    strategy: "round-robin"        # round-robin notifications
    backends: [...]

routes:
  - opcodes: "0x0100-0x01FF"
    backend: "workers"
    response_backend: "notifiers"
```
