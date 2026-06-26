# Internal Protocol (Gateway ↔ Backend)

Protocol for communication between Gateway and Backend servers. Runs over WebSocket binary frames.

## Message Format

```
┌─────────────────────────────────────────────────┐
│   4 bytes    │   4 bytes    │  2 bytes  │  ...  │
│  message_type│ corr_id /    │  opcode   │ params│
│              │ stream_id    │           │       │
└─────────────────────────────────────────────────┘
```

All multi-byte values are in **network byte order (big-endian)**.

## Message Types

### DATA (1) — Fire-and-forget

Direction: Gateway → Backend

```
[type:4] [opcode:2] [params...]
```

| Field | Size | Description |
|---|---|---|
| `type` | 4 | `0x00000001` |
| `opcode` | 2 | Request opcode |
| `params` | remainder | Parameters (serialized via ObscuraProto Payload) |

Gateway does not wait for a response. Used for logs, analytics, notifications.

---

### REQUEST (2) — Request with expected response

Direction: Gateway → Backend

```
[type:4] [corr_id:4] [opcode:2] [params...]
```

| Field | Size | Description |
|---|---|---|
| `type` | 4 | `0x00000002` |
| `corr_id` | 4 | Unique request ID (assigned by Gateway) |
| `opcode` | 2 | Request opcode |
| `params` | remainder | Request parameters |

Backend **must** respond with `RESPONSE` using the same `corr_id`.

---

### RESPONSE (3) — Response to a request

Direction: Backend → Gateway

```
[type:4] [corr_id:4] [opcode:2] [params...]
```

| Field | Size | Description |
|---|---|---|
| `type` | 4 | `0x00000003` |
| `corr_id` | 4 | Same ID as in the REQUEST |
| `opcode` | 2 | Response opcode (usually the same as in the request) |
| `params` | remainder | Response parameters |

Gateway verifies the `corr_id`, finds the waiting client, and checks that the backend belongs to the authorized `response_pool`.

---

### STREAM_START (4) — Stream start

Direction: Both directions

```
[type:4] [stream_id:4] [opcode:2] [params...]
```

| Field | Size | Description |
|---|---|---|
| `type` | 4 | `0x00000004` |
| `stream_id` | 4 | Stream ID (assigned by the initiator) |
| `opcode` | 2 | Stream opcode |
| `params` | remainder | Initial stream parameters |

The initiator assigns the `stream_id`. Gateway forwards all subsequent packets with this `stream_id` to the same backend.

---

### STREAM_DATA (5) — Stream data

Direction: Both directions

```
[type:4] [stream_id:4] [data...]
```

| Field | Size | Description |
|---|---|---|
| `type` | 4 | `0x00000005` |
| `stream_id` | 4 | Stream ID |
| `data` | remainder | Data fragment |

---

### STREAM_END (6) — Stream end

Direction: Both directions

```
[type:4] [stream_id:4]
```

| Field | Size | Description |
|---|---|---|
| `type` | 4 | `0x00000006` |
| `stream_id` | 4 | Stream ID |

After STREAM_END, Gateway removes the stream mapping.

---

### STREAM_CANCEL (7) — Stream cancel

Direction: Both directions

```
[type:4] [stream_id:4]
```

| Field | Size | Description |
|---|---|---|
| `type` | 4 | `0x00000007` |
| `stream_id` | 4 | Stream ID |

Cancellation without completion (e.g., on timeout or error).

---

## Message Handling

### Gateway (receiving from backend)

```python
def on_backend_message(hdl, data):
    msg = deserialize(data)
    
    if msg.type == RESPONSE:
        pending = correlation.resolve(msg.corr_id)
        if not pending:
            return  # timeout or unknown corr_id
        
        sender_pool = find_pool(hdl)
        if sender_pool != pending.response_pool:
            log("Rejected: sender %s != expected %s" % (sender_pool, pending.response_pool))
            return  # unauthorized response
        
        client_server.send_response(pending.client_hdl, pending.original_req_id, msg.params)
    
    elif msg.type in (STREAM_DATA, STREAM_END, STREAM_CANCEL):
        mapping = stream_manager.resolve(msg.stream_id)
        if mapping:
            client_server.send(hdl, mapping.opcode, msg)  # forward to client
    
    elif msg.type == STREAM_START:
        # New stream from backend to client
        client = find_client_for_stream(msg.stream_id)
        if client:
            client_server.send(client, msg.opcode, msg)  # forward to client
```

### Backend (receiving from Gateway)

```python
def on_gateway_message(data):
    msg = deserialize(data)
    
    if msg.type == REQUEST:
        corr_id = msg.corr_id
        opcode = msg.opcode
        params = msg.params
        
        result = handle_request(opcode, params)
        
        send(RESPONSE, corr_id, opcode, result)
    
    elif msg.type == DATA:
        handle_fire_and_forget(msg.opcode, msg.params)
    
    elif msg.type == STREAM_START:
        handle_stream_start(msg.stream_id, msg.opcode, msg.params)
    
    elif msg.type == STREAM_DATA:
        handle_stream_data(msg.stream_id, msg.data)
    
    elif msg.type == STREAM_END:
        handle_stream_end(msg.stream_id)
    
    elif msg.type == STREAM_CANCEL:
        handle_stream_cancel(msg.stream_id)
```

## Message Flow (Request-Response)

```
Gateway                          Backend A              Backend B
  │                                │                      │
  │  REQUEST corr_id=42            │                      │
  │  opcode=0x0001                 │                      │
  │  params=[user,pass]            │                      │
  │ ─────────────────────────────► │                      │
  │                                │                      │
  │                                │  (processing...)     │
  │                                │                      │
  │                    RESPONSE corr_id=42                │
  │                    opcode=0x0001                      │
  │                    params=[token]                     │
  │ ◄───────────────────────────────────────────────────  │
  │                                │                      │
  │ ✓ sender auth-responder ==     │                      │
  │   expected auth-responder      │                      │
  │                                │                      │
  │ RESPONSE to client             │                      │
  │ original_req_id, params        │                      │
```

## Message Flow (Streaming)

```
Gateway                          Backend
  │                                │
  │  STREAM_START stream_id=1      │
  │  opcode=0x0100                 │
  │  params=[filename]             │
  │ ─────────────────────────────► │
  │                                │
  │  STREAM_DATA stream_id=1       │
  │  data=[chunk_1]                │
  │ ─────────────────────────────► │
  │                                │
  │  STREAM_DATA stream_id=1       │
  │  data=[chunk_2]                │
  │ ─────────────────────────────► │
  │                                │
  │  STREAM_DATA stream_id=1       │
  │  data=[result_1]               │
  │ ◄───────────────────────────── │
  │                                │
  │  STREAM_END stream_id=1        │
  │ ─────────────────────────────► │
  │                                │
  │  STREAM_END stream_id=1        │
  │ ◄───────────────────────────── │
```

## Notes

1. **Big-endian** — all multi-byte fields are in network byte order
2. **Correlation ID** — 32-bit unsigned integer, assigned monotonically by Gateway
3. **Stream ID** — 32-bit unsigned integer, assigned by the stream initiator
4. **Params** — serialized via ObscuraProto Payload (length-prefixed parameters)
5. **Timeout** — if no response arrives within `request_timeout`, Gateway removes the pending request
6. **Response pool verification** — Gateway rejects RESPONSE if the sender is not from the authorized pool
