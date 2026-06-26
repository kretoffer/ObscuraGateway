# ObscuraGateway

**ObscuraGateway** — высокопроизводительный Gateway для [ObscuraProto](https://github.com/kretoffer/ObscuraProto), выполняющий функции точки входа, балансировщика нагрузки и маршрутизации запросов по opcode между клиентами и внутренними серверами-обработчиками.

---

## Архитектура

### Схема соединений

```
Backend (auth-node-1) ──ObscuraProto──►┐
(Ed25519: 0xA...)                      │
Backend (auth-node-2) ──ObscuraProto──►├─── Gateway ────◄── Клиент
(Ed25519: 0xB...)                      │   :clients_port        │
Backend (storage-1) ──ObscuraProto──►┘   :backends_port        │
(Ed25519: 0xC...)                                     ObscuraProto
                                                      (full crypto)
```

Порты настраиваются в конфиге (`listen.clients` и `listen.backends`).

### Компоненты

```
Gateway
├── Frontend Listener (port 8443)
│   └── ObscuraProto WsServerWrapper
│       ├── Anonymous handlers (регистрируются динамически через Router)
│       └── Authenticated handlers (регистрируются динамически через Router)
│
├── Backend Listener (port 9444)
│   └── ObscuraProto WsServerWrapper
│       ├── Принимает подключения от backend-серверов
│       └── Верифицирует Ed25519 public key против конфига
│
├── Router
│   ├── Читает маршруты из конфига
│   ├── Маппинг: opcode + session_type → (RequestPool, ResponsePool)
│   └── Поддерживает диапазоны и отдельные opcode
│
├── BackendPool (на каждый пул бэкендов)
│   ├── Список активных backend-соединений
│   └── Стратегия балансировки (RoundRobin / LeastConnections)
│
├── CorrelationManager
│   ├── Request-Response: карта correlation_id → (client, original_req_id)
│   └── Очистка по таймауту
│
├── StreamManager
│   ├── Привязка клиентского stream_id к backend-соединению
│   └── Маршрутизация STREAM_DATA / STREAM_END / STREAM_CANCEL
│
└── InternalProtocol
    └── Сериализация сообщений между Gateway и Backend
```

---

## Поток запроса

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

## Конфигурация

### config.yml

```yaml
gateway:
  # Порты для клиентов и бэкендов (настраиваются в YAML)
  listen:
    clients: "0.0.0.0:8443"
    backends: "0.0.0.0:9444"

  # Пулы бэкенд-серверов
  backend_pools:
    - name: "auth-processor"
      # Стратегия балансировки:
      #   round-robin        — по кругу между бэкендами
      #   least-connections  — на бэкенд с наименьшим числом активных запросов
      #   random             — случайный выбор
      #   weighted           — пропорционально весу бэкенда (требует weight)
      #   consistent-hash    — привязка по хешу (требует hash_key)
      strategy: "least-connections"

      # Ключ для consistent-hash (если strategy: consistent-hash)
      # hash_key: "client_ip"       # client_ip | client_id | opcode

      # Режим безопасности соединения gateway↔backend:
      #   plain     — без шифрования (trusted network, max throughput)
      #   encrypted — полный ObscuraProto handshake + E2E encryption
      security: "encrypted"

      # Список бэкендов в пуле
      backends:
        - id: "proc-node-1"
          # Ed25519 публичный ключ бэкенда (hex)
          # Gateway сверяет его при handshake и маппит в этот пул
          public_key: "e61a2c8b9d0f3e7a5b4d2c1f6a8e0d3b7c9f1e4a5b2d8c0f3e7a6b4d9c1f0e"
          # Вес для weighted strategy (опционально, по умолчанию 100)
          # weight: 200
        - id: "proc-node-2"
          public_key: "a1b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6e7f8a9b0c1d2e3f4a5b6c7d8e9f0a1"
          # weight: 100

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

  # Маршрутизация opcode → пул
  routes:
    # Запрос идёт в auth-processor, ответ приходит от auth-responder
    - opcodes: "0x0001-0x00FF"
      backend: "auth-processor"
      response_backend: "auth-responder"
      session: "authenticated"

    # Response_backend не указан — request и response от storage
    - opcodes: "0x0100-0x01FF"
      backend: "storage"
      session: "any"
```

### Формат opcodes

Каждый элемент в `opcodes` — список через запятую. Элементы:

| Формат | Пример | Описание |
|---|---|---|
| Одиночный opcode | `0x0001` | Конкретный opcode |
| Диапазон | `0x0001-0x00FF` | Все opcode от 0x0001 до 0x00FF включительно |
| Комбинация | `0x0001,0x0005-0x000A,0x1000` | Смешанный список |

### Параметр session

| Значение | Описание |
|---|---|
| `anonymous` | Только для неаутентифицированных сессий |
| `authenticated` | Только для аутентифицированных сессий (с Ed25519 identity) |
| `any` | Любые сессии |

### Разделение request и response

Каждый маршрут может указывать разные пулы для обработки запроса и отправки ответа:

```yaml
routes:
  - opcodes: "0x0001-0x00FF"
    backend: "auth-processor"       # запрос идёт сюда
    response_backend: "auth-responder"  # ответ приходит отсюда
```

Параметр `response_backend` опционален. Если он не указан, Gateway ожидает ответ от того же пула, что и запрос.

**Когда это полезно:**

| Сценарий | Request pool | Response pool | Описание |
|---|---|---|---|
| Асинхронная обработка | workers | notifiers | Запрос ставится в очередь, ответ приходит от сервера уведомлений |
| Разделение ответственности | validators | executors | Валидация запроса одним сервером, исполнение и ответ — другим |
| Fan-out | dispatcher | collector | Запрос рассылается, ответ собирается агрегатором |

**Безопасность:** Gateway проверяет, что ответ пришёл именно от того пула, который указан в `response_backend`. Если ответ приходит от другого пула, он отклоняется:

```
[Gateway] Request routed to pool "auth-processor" (expects response from "auth-responder")
[Gateway] Response from "auth-processor" → rejected (not authorized to respond)
[Gateway] Response from "auth-responder" → accepted, forwarded to client
```

---

## Внутренний протокол (Gateway ↔ Backend)

Сообщения поверх WebSocket binary frames. Формат:

```
[4 байта: message_type] [payload...]
```

### Типы сообщений

| Тип | Value | Направление | Формат |
|---|---|---|---|
| `DATA` | 1 | Gateway→Backend | `[type][opcode:2][params...]` |
| `REQUEST` | 2 | Gateway→Backend | `[type][corr_id:4][opcode:2][params...]` |
| `RESPONSE` | 3 | Backend→Gateway | `[type][corr_id:4][opcode:2][params...]` |
| `STREAM_START` | 4 | Оба направления | `[type][stream_id:4][opcode:2][params...]` |
| `STREAM_DATA` | 5 | Оба направления | `[type][stream_id:4][data...]` |
| `STREAM_END` | 6 | Оба направления | `[type][stream_id:4]` |
| `STREAM_CANCEL` | 7 | Оба направления | `[type][stream_id:4]` |

Все многобайтовые значения — **network byte order (big-endian)**.

### Обработка сообщений

- **DATA** — fire-and-forget. Gateway не ждёт ответа, просто пересылает.
- **REQUEST** — запрос, требующий ответа. Backend обязан вернуть RESPONSE с тем же `corr_id`.
- **RESPONSE** — ответ на запрос. Gateway маппит `corr_id` обратно на клиента.
- **STREAM_*** — потоковая передача. `stream_id` назначается инициатором (с чётной/нечётной параностью, как в ObscuraProto). Gateway форвардит все пакеты одного стрима на один и тот же бэкенд.

---

## Безопасность

### Шифрование между клиентом и Gateway

Используется полный стек ObscuraProto:
- **Key Exchange:** X25519 ECDH
- **Identity:** Ed25519 подписи (опционально для клиентов)
- **AEAD:** ChaCha20-Poly1305 (IETF variant)
- **Replay Protection:** Counter + nonce (AEAD associated data)

### Шифрование между Gateway и Backend

Два режима (настраивается в конфиге):

| Режим | Безопасность | CPU overhead | Размер оверхеда |
|---|---|---|---|
| `plain` | Отсутствует (доверенная сеть) | ~0% | 0 |
| `encrypted` | Полный ObscuraProto handshake + session | ~3-5% на ядро | 28 байт/сообщение |

**Рекомендации:**
- `plain` — gateway и бэкенды на одном хосте, в Kubernetes pod'е, или в изолированном VLAN
- `encrypted` — через публичные/полупубличные сети, при требованиях compliance (PCI DSS, SOC2, и т.п.)

**Handshake** (X25519 + Ed25519) занимает ~0.5-2ms, но выполняется один раз на всё время жизни соединения (часы/дни), поэтому overhead незначителен.

---

## Балансировка нагрузки

Все стратегии настраиваются в конфиге через поле `strategy` пула.

### Round-robin

Запросы распределяются по кругу:

```
backend-1 → backend-2 → backend-3 → backend-1 → ...
```

Подходит для:
- Однородных запросов
- Бэкендов равной мощности
- Простейшего распределения

### Least-connections

Запрос отправляется на бэкенд с наименьшим числом активных запросов в данный момент:

```
backend-1: 12 активных запросов
backend-2:  3 активных запроса  ← новый запрос сюда
backend-3:  9 активных запросов
```

Gateway отслеживает `active_requests` для каждого backend-соединения: +1 при отправке REQUEST, -1 при получении RESPONSE.

Подходит для:
- Запросов переменной сложности
- Бэкендов равной мощности
- Пиковых нагрузок

### Random

Случайный выбор бэкенда из пула. Простейшая стратегия без состояния:

```
pick = rand() % pool.size()
```

**Плюсы:** не требует счётчиков, минимальный overhead.
**Минусы:** нет гарантий равномерного распределения.

Подходит для:
- Прототипов и тестов
- Когда равномерность не критична

### Weighted

Распределение пропорционально весу бэкенда (`weight`). Вес задаётся в конфиге для каждого бэкенда (стандартный 100):

```yaml
backends:
  - id: "node-1"
    public_key: "..."
    weight: 200    # получает вдвое больше трафика
  - id: "node-2"
    public_key: "..."
    weight: 100    # базовая единица
  - id: "node-3"
    public_key: "..."
    weight: 100
```

Итоговая пропорция: 200/400 : 100/400 : 100/400 = 50% : 25% : 25%.

Реализуется через **smooth weighted round-robin** (алгоритм nginx):

| Шаг | node-1 (200) | node-2 (100) | node-3 (100) | Выбран |
|---|---|---|---|---|
| 1 | +200 → 200 ✓ | +100 → 100 | +100 → 100 | node-1 (-400 → -200) |
| 2 | +200 → 0 | +100 → 200 ✓ | +100 → 200 | node-2 (-400 → -200) |
| 3 | +200 → -100 | +100 → -100 | +100 → 300 ✓ | node-3 (-400 → -100) |
| ... | | | | |

Это даёт точное распределение без образования «пачек» запросов на один бэкенд.

Подходит для:
- Гетерогенных кластеров (разные CPU/RAM)
- Canary deployments (один бэкенд получает 1% трафика)
- Постепенных rollout'ов

### Consistent-hash

Запрос направляется на бэкенд на основе хеша от ключа (`hash_key`). Один и тот же ключ всегда попадает на один и тот же бэкенд, пока не меняется состав пула.

Конфигурация:

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

Возможные значения `hash_key`:

| Ключ | Источник | Эффект |
|---|---|---|
| `client_id` | Ed25519 pubkey клиента | Все запросы одного клиента → один бэкенд |
| `client_ip` | IP адрес клиента | Как выше, но по IP |
| `opcode` | Opcode запроса | Конкретная операция всегда на одном бэкенде |

Используется алгоритм **ketama** (consistent hashing с виртуальными нодами):

```
hash("cache-1:0"), hash("cache-1:1"), ..., hash("cache-1:159")
hash("cache-2:0"), hash("cache-2:1"), ..., hash("cache-2:159")
...
→ сортируем кольцо хешей
→ для запроса: hash(key) → ищем следующий по кольцу
```

При добавлении/удалении бэкенда перераспределяется только **1/N** запросов (против 100% у round-robin).

Подходит для:
- Кеширующих бэкендов (in-memory cache)
- Сессионных данных (sticky sessions)
- Состояния, привязанного к клиенту

---

## Аутентификация бэкендов

Gateway идентифицирует бэкенды по их **Ed25519 публичному ключу**:

1. Бэкенд подключается к Gateway (`:9444`)
2. Выполняется ObscuraProto handshake
3. Gateway получает публичный ключ бэкенда
4. Gateway ищет ключ в конфиге (`backend_pools[].backends[].public_key`)
5. Если найден — бэкенд добавляется в соответствующий пул
6. Если не найден — соединение отклоняется

---

## Паттерны сообщений

### Fire-and-forget (DATA)

Клиент → Gateway → Backend. Ответ не ожидается.

```
Client: opcode=0x0200, params=[event_data]  ──►  Gateway  ──►  Backend
```

### Request-Response (REQUEST / RESPONSE)

Клиент отправляет запрос, ожидает ответ. Gateway коррелирует через `correlation_id`.

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

Бидирекциональный поток данных между клиентом и бэкендом.

```
Client: STREAM_START, opcode, params  ──►  Gateway  ──►  Backend
Client: STREAM_DATA, data             ──►  Gateway  ──►  Backend
Client: STREAM_END                    ──►  Gateway  ──►  Backend
Backend: STREAM_DATA, data            ──►  Gateway  ──►  Client
...
```

Gateway гарантирует, что все пакеты одного стрима попадают на один и тот же бэкенд.

---

## Проект

### Структура

```
ObscuraGateway/
├── CMakeLists.txt
├── config.yml                          # Пример конфигурации
├── include/obscuragateway/
│   ├── gateway.hpp                     # Главный класс Gateway
│   ├── config.hpp                      # Парсер конфигурации
│   ├── router.hpp                      # Маршрутизация opcode → pool
│   ├── backend_pool.hpp                # Пул бэкендов + балансировка
│   ├── backend_node.hpp                # Одно backend-соединение
│   ├── correlation.hpp                 # Request-ID correlation
│   ├── stream_manager.hpp              # Управление стримами
│   └── internal_protocol.hpp           # Внутренний протокол
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

### Зависимости

| Библиотека | Назначение | Источник |
|---|---|---|
| obscuraproto | WsServerWrapper, Payload, Session, Crypto | [ObscuraProto](https://github.com/kretoffer/ObscuraProto) |
| yaml-cpp | Парсинг YAML-конфигурации | External / vcpkg |
| websocketpp | WebSocket транспорт | Транзитивно через ObscuraProto |
| libsodium | Криптография | Транзитивно через ObscuraProto |

### Сборка

```bash
git clone https://github.com/kretoffer/ObscuraGateway.git
cd ObscuraGateway
cmake -B build
cmake --build build
```

---

## Backend Library (отдельный репозиторий)

Для упрощения разработки бэкендов будет создана отдельная библиотека. Она предоставит:

- Лёгкий WebSocket клиент (без полного crypto стека, если `security: plain`)
- Удобный API для регистрации обработчиков opcode
- Автоматическое переподключение к Gateway при обрыве связи
- Поддержку всех трёх паттернов (fire-and-forget, request-response, streaming)

Это позволяет разработчикам бэкендов не вникать в детали протокола и сосредоточиться на бизнес-логике.

---

## Лицензия

[License](/LICENSE)
