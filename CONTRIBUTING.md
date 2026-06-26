# Contributing to ObscuraGateway

## Code Style

The project follows the **Google** style with settings in `.clang-format`:

- **Indentation**: 4 spaces (no tabs)
- **Line limit**: 120 characters
- **Class names**: `CamelCase`
- **Variable and function names**: `snake_case`
- **Constant names**: `UPPER_SNAKE_CASE`
- **Const references**: west `const` (`const T&`, not `T const&`)
- **Namespaces**: indented content (`NamespaceIndentation: All`)
- **Access sections (`public:`, `private:`)**: indented -4 from class

Style is automatically verified via `clang-format`. Do not deviate from what the formatter produces — all `.cpp` and `.hpp` files are checked before commit.

## Setting up pre-commit

1. **Install `pre-commit`**:
   ```bash
   brew install pre-commit           # macOS
   pip install pre-commit            # Linux
   ```

2. **Install `clang-format`**:
   ```bash
   brew install clang-format         # macOS
   # or from LLVM releases: https://releases.llvm.org/
   ```

3. **Install git hooks** (from the repository root):
   ```bash
   pre-commit install
   ```

After this, `pre-commit` will automatically run `clang-format` on all `.cpp` and `.hpp` files on every `git commit`. If the formatter modifies files, the commit will be rejected — add the changed files (`git add`) and retry `git commit`.

## Formatting and Verification

### Via pre-commit (automatic, all files)

```bash
pre-commit run --all-files
```

### Manually, via clang-format

```bash
# Format a single file
clang-format -i path/to/file.cpp

# Format all .cpp and .hpp files
find . -name '*.cpp' -o -name '*.hpp' | xargs clang-format -i

# Check only (no changes) — for CI
clang-format --dry-run --Werror path/to/file.cpp
```

## Build and Test

```bash
# Configuration
cmake -S . -B build

# Build
cmake --build build

# Run tests
cd build && ctest

# Run a specific test
cd build && ./tests/test_router
```

### Build Dependencies

| Dependency | Installation (macOS) | Installation (Ubuntu) |
|---|---|---|
| CMake ≥ 3.16 | `brew install cmake` | `apt install cmake` |
| ObscuraProto | see [ObscuraProto](https://github.com/kretoffer/ObscuraProto) | — |
| yaml-cpp | `brew install yaml-cpp` | `apt install libyaml-cpp-dev` |
| GTest (tests) | `brew install googletest` | `apt install libgtest-dev` |

## Before Sending a Pull Request

1. `pre-commit run --all-files` — no errors
2. `cmake --build build` — no warnings
3. `ctest` — all tests pass

## Architecture

Key architectural decisions are described in `docs/`:

- [Architecture](docs/architecture.md) — components, lifecycle, threads
- [Configuration](docs/configuration.md) — full YAML reference
- [Internal Protocol](docs/internal_protocol.md) — Gateway↔Backend messages
- [Load Balancing](docs/load_balancing.md) — all 5 strategies

## Development Process

1. Create a feature branch from `develop`:
   ```bash
   git checkout -b feat/your-feature
   ```
2. Make changes, following the code style
3. Ensure tests pass
4. Open a Pull Request to `develop`

### Commit Naming

- `feat: short description` — new feature
- `fix: short description` — bug fix
- `docs: short description` — documentation
- `refactor: short description` — refactoring
- `test: short description` — tests
