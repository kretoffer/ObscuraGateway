# Балансировка нагрузки в ObscuraGateway

## Обзор

Каждый пул бэкендов (`backend_pool`) имеет стратегию балансировки, которая определяет, как Gateway выбирает бэкенд для обработки запроса.

```yaml
backend_pools:
  - name: "auth"
    strategy: "least-connections"
    backends: [...]
```

## Стратегии

---

### 1. Round-robin

**Когда использовать:** Однородные запросы, бэкенды равной мощности.

Запросы распределяются по кругу:

```
Запрос 1 → backend-1
Запрос 2 → backend-2
Запрос 3 → backend-3
Запрос 4 → backend-1
Запрос 5 → backend-2
...
```

**Реализация:**

```cpp
BackendNode* round_robin() {
    size_t idx = rr_counter_++ % nodes_.size();
    return nodes_[idx];
}
```

**Плюсы:** Простейшая реализация, предсказуемое распределение.
**Минусы:** Не учитывает загрузку бэкендов, не подходит для запросов разной сложности.

---

### 2. Least-connections

**Когда использовать:** Запросы разной сложности, бэкенды разной мощности, пиковые нагрузки.

Gateway отправляет запрос на бэкенд с наименьшим числом активных запросов:

```
backend-1: 12 активных запросов
backend-2:  3 активных запроса  ← новый запрос сюда
backend-3:  9 активных запросов
```

**Счётчик активных запросов:**

| Событие | Действие |
|---|---|
| Gateway отправил REQUEST | `node.active_requests++` |
| Gateway получил RESPONSE | `node.active_requests--` |
| Таймаут запроса | `node.active_requests--` |
| Обрыв соединения | Все `active_requests` обнуляются |

**Реализация:**

```cpp
BackendNode* least_connections() {
    return *std::min_element(nodes_.begin(), nodes_.end(),
        [](auto& a, auto& b) {
            return a.active_requests() < b.active_requests();
        });
}
```

**Плюсы:** Адаптируется к реальной загрузке, эффективен при переменной нагрузке.
**Минусы:** Требует синхронизации счётчика, не учитывает вес бэкендов.

---

### 3. Random

**Когда использовать:** Прототипы, тесты, когда равномерность не критична.

```cpp
BackendNode* random() {
    std::uniform_int_distribution<size_t> dist(0, nodes_.size() - 1);
    return nodes_[dist(gen)];
}
```

**Плюсы:** Нулевой overhead, не требует состояния.
**Минусы:** Нет гарантий равномерности, возможны «пачки» на один бэкенд.

---

### 4. Weighted

**Когда использовать:** Гетерогенные кластеры, canary deployments, постепенные rollout'ы.

Распределение пропорционально весу бэкенда:

```yaml
backends:
  - id: "node-1"   # вес 200 → 50% трафика
    weight: 200
  - id: "node-2"   # вес 100 → 25% трафика
    weight: 100
  - id: "node-3"   # вес 100 → 25% трафика
    weight: 100
```

**Алгоритм: Smooth Weighted Round-Robin (nginx)**

Каждый бэкенд имеет:
- `weight` — заданный вес (из конфига)
- `current_weight` — изменяется на каждом шаге

Шаг алгоритма:
1. Для каждого бэкенда: `current_weight += weight`
2. Выбрать бэкенд с максимальным `current_weight`
3. Для выбранного: `current_weight -= total_weight`

**Пример с весами 200, 100, 100:**

| Шаг | node-1 (200) | node-2 (100) | node-3 (100) | Выбран |
|---|---|---|---|---|
| 1 | **+200 → 200** | +100 → 100 | +100 → 100 | node-1 (-400 → **-200**) |
| 2 | +200 → **0** | +100 → **200** | +100 → 200 | node-2 (-400 → **-200**) |
| 3 | +200 → **-100** | +100 → **-100** | +100 → **300** | node-3 (-400 → **-100**) |
| 4 | +200 → **100** | +100 → **0** | +100 → **0** | node-1 (-400 → **-300**) |
| 5 | +200 → **-100** | +100 → **100** | +100 → **100** | node-2 (-400 → **-300**) |
| 6 | +200 → **100** | +100 → **-200** | +100 → **200** | node-1 (-400 → **-300**) |

Результат: `N1, N2, N3, N1, N2, N1` — идеальная пропорция 3:2:2 на 6 запросов = 50%:33%:17% (для весов 200:100:100 было бы 50:25:25 при бесконечной выборке).

**Реализация:**

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

**Когда использовать:** Кеширующие бэкенды, сессионные данные, sticky sessions.

Запрос направляется на бэкенд на основе хеша от ключа:

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

**Ключ хеширования:**

| Значение | Хешируется | Эффект |
|---|---|---|
| `client_id` | Ed25519 pubkey клиента | Все запросы одного клиента → один бэкенд |
| `client_ip` | IP клиента | Все запросы с одного IP → один бэкенд |
| `opcode` | Opcode запроса | Конкретная операция → один бэкенд |

**Алгоритм: Ketama consistent hashing**

```
virtual_nodes = 160 per backend

for each backend:
    for i in 0..159:
        hash = hash("backend.id:i")
        ring.insert(hash, backend)

for each request:
    key = hash(request.hash_key)
    backend = ring.next(key)  # следующий по кольцу
```

**Преимущества consistent-hash:**

При добавлении или удалении бэкенда перераспределяется только **1/N** запросов (где N — число бэкендов). Для сравнения:

| Операция | Round-robin | Consistent-hash |
|---|---|---|
| Добавление backend-4 | 100% запросов перераспределяется | ~25% запросов перераспределяется |
| Удаление backend-2 | 100% | ~25% |

**Реализация (упрощённая):**

```cpp
BackendNode* consistent_hash(opcode, client_id, client_ip) {
    std::string key;
    switch (hash_key) {
        case ClientId: key = client_id; break;
        case ClientIp: key = client_ip; break;
        case Opcode:   key = std::to_string(opcode); break;
    }
    
    // Для production: полноценное кольцо с виртуальными нодами
    size_t idx = std::hash<std::string>(key) % nodes_.size();
    return nodes_[idx];
}
```

**Для продакшена** необходима полноценная реализация с кольцом и виртуальными нодами (160 виртуальных нод на бэкенд, как в libketama).

---

## Выбор стратегии

| Сценарий | Рекомендуемая стратегия |
|---|---|
| Все бэкенды одинаковые, запросы простые | `round-robin` |
| Запросы разной сложности | `least-connections` |
| Сервера разной мощности | `weighted` |
| Кеширование / сессии | `consistent-hash` |
| Тесты / прототипы | `random` |
| Canary deployment (2% трафика) | `weighted` (weight=2 vs 98) |

## Комбинирование с response_pool

Request и Response могут использовать разные пулы с разными стратегиями:

```yaml
backend_pools:
  - name: "workers"
    strategy: "least-connections"  # балансируем нагрузку воркеров
    backends: [...]

  - name: "notifiers"
    strategy: "round-robin"        # уведомления по кругу
    backends: [...]

routes:
  - opcodes: "0x0100-0x01FF"
    backend: "workers"
    response_backend: "notifiers"
```
