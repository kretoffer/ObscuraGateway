# Конфигурация ObscuraGateway

Конфигурация задаётся в YAML-файле и передаётся gateway при запуске:

```bash
obscuragatewayd /path/to/config.yml
```

## Структура конфига

```yaml
gateway:
  listen:
    clients: "0.0.0.0:8443"
    backends: "0.0.0.0:9444"

  backend_pools:
    - name: "..."
      strategy: "..."
      security: "..."
      hash_key: "..."        # только для consistent-hash
      backends:
        - id: "..."
          public_key: "..."
          weight: 100        # только для weighted

  routes:
    - opcodes: "..."
      backend: "..."
      response_backend: "..."  # опционально
      session: "..."
```

---

## listen

```yaml
listen:
  clients: "0.0.0.0:8443"     # адрес:порт для клиентов
  backends: "0.0.0.0:9444"    # адрес:порт для бэкендов
```

| Поле | По умолчанию | Описание |
|---|---|---|
| `clients` | `0.0.0.0:8443` | Интерфейс и порт для клиентских подключений |
| `backends` | `0.0.0.0:9444` | Интерфейс и порт для backend-подключений |

---

## backend_pools

Список пулов бэкенд-серверов. Каждый пул — группа серверов с общей стратегией балансировки.

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

### Параметры пула

| Поле | Обязательное | По умолчанию | Описание |
|---|---|---|---|
| `name` | да | — | Уникальное имя пула |
| `strategy` | нет | `round-robin` | Стратегия балансировки |
| `security` | нет | `plain` | Режим шифрования |
| `hash_key` | нет | `client_id` | Ключ хеширования (для `consistent-hash`) |
| `backends` | да | — | Список бэкендов |

### strategy

| Значение | Описание |
|---|---|
| `round-robin` | По кругу |
| `least-connections` | Наименьшее число активных запросов |
| `random` | Случайный выбор |
| `weighted` | Пропорционально весу |
| `consistent-hash` | По хешу ключа |

### security

| Значение | Описание |
|---|---|
| `plain` | Без шифрования (максимальная производительность) |
| `encrypted` | Полный ObscuraProto handshake + ChaCha20-Poly1305 |

### hash_key

Для `strategy: consistent-hash`:

| Значение | Источник | Эффект |
|---|---|---|
| `client_id` | Ed25519 публичный ключ клиента | Клиент всегда на одном бэкенде |
| `client_ip` | IP адрес клиента | IP всегда на одном бэкенде |
| `opcode` | Opcode запроса | Операция всегда на одном бэкенде |

### backends

Список серверов в пуле:

```yaml
backends:
  - id: "node-1"               # уникальный идентификатор
    public_key: "e61a..."      # Ed25519 публичный ключ (64 hex символа)
    weight: 200                # вес (только для strategy: weighted)
```

| Поле | Обязательное | По умолчанию | Описание |
|---|---|---|---|
| `id` | да | — | Уникальный ID бэкенда в рамках пула |
| `public_key` | да | — | Ed25519 публичный ключ (64 hex, 32 байта) |
| `weight` | нет | `100` | Вес для балансировки (только `weighted`) |

---

## routes

Маппинг opcode → пул. Может содержать несколько маршрутов.

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

### Параметры маршрута

| Поле | Обязательное | По умолчанию | Описание |
|---|---|---|---|
| `opcodes` | да | — | Список opcode (см. формат ниже) |
| `backend` | да | — | Имя пула для отправки запроса |
| `response_backend` | нет | то же, что `backend` | Имя пула для получения ответа |
| `session` | нет | `any` | Тип сессии |

### Формат opcodes

Строка с opcode, разделёнными запятыми:

| Формат | Пример | Описание |
|---|---|---|
| Одиночный | `0x0001` | Шестнадцатеричное число |
| Диапазон | `0x0001-0x00FF` | От и до включительно |
| Комбинация | `0x0001,0x0005-0x000A,0x1000` | Смешанный список |

### session

| Значение | Описание |
|---|---|
| `anonymous` | Только неаутентифицированные клиенты |
| `authenticated` | Только аутентифицированные (Ed25519 identity) |
| `any` | Любые клиенты |

### response_backend

Позволяет указать другой пул для получения ответа:

```yaml
routes:
  - opcodes: "0x0100-0x01FF"
    backend: "workers"          # запрос → воркеры
    response_backend: "gateway" # ответ → другой сервис
```

Если поле не указано, Gateway ожидает ответ от того же пула.

---

## Полный пример

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
