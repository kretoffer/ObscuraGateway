# Архитектура ObscuraGateway

## Общая схема

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

## Ключевые решения

### 1. Backend → Gateway (reverse connection)

Бэкенды сами устанавливают соединение с Gateway, а не наоборот.

**Преимущества:**
- Gateway не нужно знать адреса бэкендов заранее
- Бэкенды могут динамически подключаться и отключаться (auto-scaling)
- При падении бэкенда gateway не тратит ресурсы на переподключение
- Проще конфигурация: все бэкенды знают только адрес gateway

### 2. Два порта

Gateway слушает два порта (настраиваются в конфиге):

| Порт | Назначение | Аутентификация |
|---|---|---|
| `clients` (8443) | Внешние клиенты | Полный ObscuraProto handshake |
| `backends` (9444) | Внутренние бэкенды | Ed25519 public key из конфига |

Разделение портов позволяет:
- Закрыть backend-порт firewall'ом для внешнего доступа
- Использовать разные настройки шифрования
- Логировать и мониторить порты отдельно

### 3. Терминирование шифрования на Gateway

Gateway расшифровывает клиентские запросы и пересылает их бэкендам.

**Почему не сквозное шифрование:**
- Gateway должен инспектировать opcode для маршрутизации
- Бэкенды могут быть упрощены (без полного crypto стека)
- Возможна асинхронная обработка (request и response на разных бэкендах)

### 4. Connection pooling

Gateway держит постоянные WebSocket-соединения к бэкендам. Разные клиенты разделяют эти соединения.

**Преимущества:**
- Нет накладных расходов на handshake при каждом запросе
- Эффективное использование ресурсов
- Бэкенды обслуживают N клиентов через одно соединение

## Компоненты

### Gateway (главный класс)

```cpp
class Gateway {
    WsServerWrapper client_server_;   // приём клиентов
    WsServerWrapper backend_server_;  // приём бэкендов
    Router router_;                   // маршрутизация
    std::vector<BackendPool> pools_;  // пулы бэкендов
    CorrelationManager correlation_;  // correlation_id tracking
    StreamManager stream_manager_;    // стримы
};
```

### Frontend Listener

WsServerWrapper из ObscuraProto, регистрирующий хендлеры, которые перенаправляют запросы в Router.

Обрабатывает:
- Анонимные запросы (до аутентификации)
- Аутентифицированные запросы
- Fire-and-forget, request-response, streaming

### Backend Listener

Второй WsServerWrapper, принимающий подключения от бэкендов.

На каждый connect:
1. Получает Ed25519 публичный ключ из handshake
2. Ищет ключ в конфиге
3. Добавляет бэкенд в соответствующий пул
4. Регистрирует хендлер для внутренних сообщений

### Router

Хранит маппинг opcode → Route:

```cpp
struct RouteEntry {
    BackendPool* request_pool;   // пул для отправки запроса
    BackendPool* response_pool;  // пул, откуда ждём ответ
    SessionType session;
};
```

Поддерживает:
- Одиночные opcode
- Диапазоны opcode
- Session type (anonymous / authenticated / any)

### BackendPool

Управляет группой backend-соединений:

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

Одно WebSocket-соединение к бэкенду:

```cpp
class BackendNode {
    WsClientWrapper client_;    // ObscuraProto client
    std::string id_;            // из конфига
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

Сопоставляет ответы от бэкендов с ожидающими клиентами:

```cpp
struct PendingRequest {
    WsConnectionHdl client_hdl;
    uint32_t original_req_id;
    std::string response_pool;  // какой пул имеет право ответить
    TimePoint created;
};

class CorrelationManager {
    uint32_t register_request(client_hdl, req_id, response_pool);
    PendingRequest resolve(corr_id);
    void cleanup_expired(timeout);
};
```

### StreamManager

Привязывает клиентский стрим к конкретному backend-соединению:

```cpp
class StreamManager {
    void register(client_stream_id, node, backend_stream_id, opcode);
    void resolve(client_stream_id) → {node, opcode};
    void remove(client_stream_id);
};
```

## Жизненный цикл запроса

```
1. Бэкенд auth-processor подключается к gateway:9444
2. Gateway: handshake → Ed25519 pubkey → ищет в конфиге → добавляет в пул "auth-processor"
3. Бэкенд auth-responder подключается → добавляется в пул "auth-responder"
4. Клиент подключается к gateway:8443
5. Client → Gateway: opcode=0x0003, params=[login, password]
6. Gateway: find_route(0x0003, authenticated) → {request_pool="auth-processor", response_pool="auth-responder"}
7. Gateway: pool("auth-processor").select() → proc-node-1
8. Gateway → proc-node-1: REQUEST corr_id=42, opcode=0x0003, params
9. Gateway: correlation_.register(client_hdl, req_id, "auth-responder") → corr_id=42
10. Backend обрабатывает, отвечает
11. resp-node-1 → Gateway: RESPONSE corr_id=42, opcode=0x0003, params
12. Gateway: resolve(42) → PendingRequest{client_hdl, req_id, "auth-responder"}
13. Gateway: find_pool(resp-node-1) → "auth-responder" ✓ совпадает
14. Gateway → Client: RESPONSE original_req_id, params
```

## Потоки выполнения

```
┌──────────────────────────────────────────────────┐
│  Main Thread                                     │
│  ├── Запуск client_server_run() в отдельном      │
│  │   потоке                                      │
│  ├── Запуск backend_server_() в отдельном        │
│  │   потоке                                      │
│  └── Ожидание сигнала (SIGINT/SIGTERM)           │
└──────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────┐
│  Client Server Thread (websocketpp event loop)   │
│  ├── Приём клиентских подключений                │
│  ├── Handshake                                   │
│  ├── Приём сообщений                             │
│  ├── Вызов коллбеков (синхронно)                 │
│  └── Отправка ответов                            │
└──────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────┐
│  Backend Server Thread (websocketpp event loop)  │
│  ├── Приём backend-подключений                   │
│  ├── Handshake + Ed25519 верификация             │
│  ├── Приём внутренних сообщений                  │
│  ├── Correlation resolve                         │
│  └── Отправка ответов клиентам                   │
└──────────────────────────────────────────────────┘
```

Коллбеки client- и backend-серверов выполняются в соответствующих event loop потоках. CorrelationManager, Router и StreamManager защищены мьютексами.
