# Contributing to ObscuraGateway

## Code Style

Проект следует **Google** стилю с настройками в `.clang-format`:

- **Отступы**: 4 пробела (без табуляции)
- **Максимум строки**: 120 символов
- **Имена классов**: `CamelCase`
- **Имена переменных и функций**: `snake_case`
- **Имена констант**: `UPPER_SNAKE_CASE`
- **Const references**: west `const` (`const T&`, не `T const&`)
- **Пространства имён**: содержимое с отступом (`NamespaceIndentation: All`)
- **Секции доступа (`public:`, `private:`)**: отступ -4 от класса

Стиль проверяется автоматически через `clang-format`. Не отклоняйтесь от того,
что производит форматтер — все `.cpp` и `.hpp` файлы проверяются перед коммитом.

## Настройка pre-commit

1. **Установите `pre-commit`**:
   ```bash
   brew install pre-commit           # macOS
   pip install pre-commit            # Linux
   ```

2. **Установите `clang-format`**:
   ```bash
   brew install clang-format         # macOS
   # или из LLVM релизов: https://releases.llvm.org/
   ```

3. **Установите git hooks** (из корня репозитория):
   ```bash
   pre-commit install
   ```

После этого `pre-commit` будет автоматически запускать `clang-format` на всех
`.cpp` и `.hpp` файлах при каждом `git commit`. Если форматтер изменил файлы,
коммит будет отклонён — добавьте изменённые файлы (`git add`) и повторите
`git commit`.

## Форматирование и проверка

### Через pre-commit (автоматически, все файлы)

```bash
pre-commit run --all-files
```

### Вручную, через clang-format

```bash
# Форматировать один файл
clang-format -i path/to/file.cpp

# Форматировать все .cpp и .hpp
find . -name '*.cpp' -o -name '*.hpp' | xargs clang-format -i

# Только проверка (без изменений) — для CI
clang-format --dry-run --Werror path/to/file.cpp
```

## Сборка и тестирование

```bash
# Конфигурация
cmake -S . -B build

# Сборка
cmake --build build

# Запуск тестов
cd build && ctest

# Запуск конкретного теста
cd build && ./tests/test_router
```

### Зависимости для сборки

| Зависимость | Установка (macOS) | Установка (Ubuntu) |
|---|---|---|
| CMake ≥ 3.16 | `brew install cmake` | `apt install cmake` |
| ObscuraProto | см. [ObscuraProto](https://github.com/kretoffer/ObscuraProto) | — |
| yaml-cpp | `brew install yaml-cpp` | `apt install libyaml-cpp-dev` |
| GTest (тесты) | `brew install googletest` | `apt install libgtest-dev` |

## Прежде чем отправить Pull Request

1. `pre-commit run --all-files` — без ошибок
2. `cmake --build build` — без предупреждений
3. `ctest` — все тесты проходят

## Архитектура

Основные архитектурные решения описаны в `docs/`:

- [Архитектура](docs/architecture.md) — компоненты, жизненный цикл, потоки
- [Конфигурация](docs/configuration.md) — полный справочник YAML
- [Внутренний протокол](docs/internal_protocol.md) — Gateway↔Backend сообщения
- [Балансировка](docs/load_balancing.md) — все 5 стратегий

## Процесс разработки

1. Создайте feature-ветку от `develop`:
   ```bash
   git checkout -b feat/your-feature
   ```
2. Внесите изменения, соблюдая кодстайл
3. Убедитесь, что тесты проходят
4. Откройте Pull Request в `develop`

### Именование коммитов

- `feat: краткое описание` — новая функциональность
- `fix: краткое описание` — исправление бага
- `docs: краткое описание` — документация
- `refactor: краткое описание` — рефакторинг
- `test: краткое описание` — тесты
