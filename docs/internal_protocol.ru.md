# Внутренний протокол (Gateway ↔ Backend)

Протокол для связи между Gateway и Backend-серверами. Работает поверх WebSocket binary frames.

## Формат сообщения

```
┌─────────────────────────────────────────────────┐
│   4 байта    │   4 байта    │  2 байта  │  ...  │
│  message_type│ corr_id /    │  opcode   │ params│
│              │ stream_id    │           │       │
└─────────────────────────────────────────────────┘
```

Все многобайтовые значения — **network byte order (big-endian)**.

## Типы сообщений

### DATA (1) — Fire-and-forget

Направление: Gateway → Backend

```
[type:4] [opcode:2] [params...]
```

| Поле | Размер | Описание |
|---|---|---|
| `type` | 4 | `0x00000001` |
| `opcode` | 2 | Opcode запроса |
| `params` | остаток | Параметры (сериализованные через ObscuraProto Payload) |

Gateway не ждёт ответа. Используется для логов, аналитики, уведомлений.

---

### REQUEST (2) — Запрос с ожиданием ответа

Направление: Gateway → Backend

```
[type:4] [corr_id:4] [opcode:2] [params...]
```

| Поле | Размер | Описание |
|---|---|---|
| `type` | 4 | `0x00000002` |
| `corr_id` | 4 | Уникальный ID запроса (назначается Gateway) |
| `opcode` | 2 | Opcode запроса |
| `params` | остаток | Параметры запроса |

Backend **обязан** ответить `RESPONSE` с тем же `corr_id`.

---

### RESPONSE (3) — Ответ на запрос

Направление: Backend → Gateway

```
[type:4] [corr_id:4] [opcode:2] [params...]
```

| Поле | Размер | Описание |
|---|---|---|
| `type` | 4 | `0x00000003` |
| `corr_id` | 4 | Тот же ID, что был в REQUEST |
| `opcode` | 2 | Opcode ответа (обычно тот же, что в запросе) |
| `params` | остаток | Параметры ответа |

Gateway сверяет `corr_id`, находит ожидающего клиента и проверяет, что бэкенд принадлежит разрешённому `response_pool`.

---

### STREAM_START (4) — Начало потока

Направление: Оба направления

```
[type:4] [stream_id:4] [opcode:2] [params...]
```

| Поле | Размер | Описание |
|---|---|---|
| `type` | 4 | `0x00000004` |
| `stream_id` | 4 | ID потока (назначается инициатором) |
| `opcode` | 2 | Opcode потока |
| `params` | остаток | Начальные параметры потока |

Инициатор назначает `stream_id`. Gateway форвардит все последующие пакеты с этим `stream_id` на тот же бэкенд.

---

### STREAM_DATA (5) — Данные потока

Направление: Оба направления

```
[type:4] [stream_id:4] [data...]
```

| Поле | Размер | Описание |
|---|---|---|
| `type` | 4 | `0x00000005` |
| `stream_id` | 4 | ID потока |
| `data` | остаток | Фрагмент данных |

---

### STREAM_END (6) — Завершение потока

Направление: Оба направления

```
[type:4] [stream_id:4]
```

| Поле | Размер | Описание |
|---|---|---|
| `type` | 4 | `0x00000006` |
| `stream_id` | 4 | ID потока |

После STREAM_END Gateway удаляет маппинг стрима.

---

### STREAM_CANCEL (7) — Отмена потока

Направление: Оба направления

```
[type:4] [stream_id:4]
```

| Поле | Размер | Описание |
|---|---|---|
| `type` | 4 | `0x00000007` |
| `stream_id` | 4 | ID потока |

Отмена без завершения (например, по таймауту или ошибке).

---

## Обработка сообщений

### Gateway (получение от бэкенда)

```python
def on_backend_message(hdl, data):
    msg = deserialize(data)
    
    if msg.type == RESPONSE:
        pending = correlation.resolve(msg.corr_id)
        if not pending:
            return  # таймаут или неизвестный corr_id
        
        sender_pool = find_pool(hdl)
        if sender_pool != pending.response_pool:
            log("Rejected: sender %s != expected %s" % (sender_pool, pending.response_pool))
            return  # неавторизованный ответ
        
        client_server.send_response(pending.client_hdl, pending.original_req_id, msg.params)
    
    elif msg.type in (STREAM_DATA, STREAM_END, STREAM_CANCEL):
        mapping = stream_manager.resolve(msg.stream_id)
        if mapping:
            client_server.send(hdl, mapping.opcode, msg)  # форвард клиенту
    
    elif msg.type == STREAM_START:
        # Новый стрим от бэкенда клиенту
        client = find_client_for_stream(msg.stream_id)
        if client:
            client_server.send(client, msg.opcode, msg)  # форвард клиенту
```

### Backend (получение от Gateway)

```python
def on_gateway_message(data):
    msg = deserialize(data)
    
    if msg.type == REQUEST:
        # Извлекаем correlation_id
        corr_id = msg.corr_id
        opcode = msg.opcode
        params = msg.params
        
        # Обрабатываем запрос
        result = handle_request(opcode, params)
        
        # Отправляем ответ
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

## Поток сообщений (Request-Response)

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

## Поток сообщений (Streaming)

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

## Примечания

1. **Big-endian** — все многобайтовые поля в network byte order
2. **Correlation ID** — 32-битное беззнаковое целое, назначается Gateway монотонно
3. **Stream ID** — 32-битное беззнаковое целое, назначается инициатором стрима
4. **Params** — сериализованы через ObscuraProto Payload (length-prefixed parameters)
5. **Таймаут** — если ответ не пришёл в течение `request_timeout`, Gateway удаляет pending request
6. **Проверка response_pool** — Gateway отклоняет RESPONSE, если отправитель не из разрешённого пула
