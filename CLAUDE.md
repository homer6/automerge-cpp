# CLAUDE.md — Development Guide for automerge-cpp

## Project Overview

automerge-cpp is a from-scratch C++23 implementation of the Automerge CRDT library.
It mirrors the upstream Rust Automerge semantics with a modern, declarative C++ API.

## Build Commands

```bash
# Configure (all targets)
cmake -B build -DCMAKE_BUILD_TYPE=Debug \
    -DAUTOMERGE_CPP_BUILD_TESTS=ON \
    -DAUTOMERGE_CPP_BUILD_EXAMPLES=ON \
    -DAUTOMERGE_CPP_BUILD_BENCHMARKS=ON

# Build
cmake --build build

# Run tests
ctest --test-dir build --output-on-failure

# Run a single test
./build/tests/automerge_cpp_tests --gtest_filter="TypesTest.*"

# Run benchmarks
./build/benchmarks/automerge_cpp_benchmarks
```

## Code Style

### C++23 — Ben Deane Style

This project follows Ben Deane's modern C++ principles rigorously:

1. **No raw loops** in library code. Use `std::ranges`, algorithms, or folds.
   ```cpp
   // Bad
   for (int i = 0; i < vec.size(); ++i) { ... }

   // Good
   auto result = vec | std::views::filter(pred) | std::views::transform(fn);
   ```

2. **Algebraic types** for all domain modeling. Use `std::variant` (sum types)
   and structs (product types). Never use type tags + unions.
   ```cpp
   // Bad
   struct Value { int type; int64_t i; string s; };

   // Good
   using Value = std::variant<int64_t, std::string>;
   ```

3. **Make illegal states unrepresentable.** If a field is only valid in one
   state, it belongs in that state's type, not in a shared base.

4. **`std::expected` for errors**, not exceptions. Exceptions are reserved for
   truly exceptional, unrecoverable situations (OOM, logic errors).

5. **Strong types** for identifiers. `ActorId`, `ObjId`, `ChangeHash` are
   distinct types that do not implicitly convert.

6. **Value semantics.** Prefer value types. Use `const` aggressively. Mutations
   go through explicit transaction boundaries.

7. **Composition over inheritance.** Small, independent components that compose.

8. **Immutability by default.** Use `const` everywhere. Mutability is explicit.

### Naming Conventions

- **Namespace**: `automerge_cpp`
- **Types**: `PascalCase` — `Document`, `ActorId`, `ScalarValue`
- **Functions/methods**: `snake_case` — `put_object`, `splice_text`, `get_changes`
- **Constants**: `snake_case` — `root` (the root object ID)
- **Enums**: `snake_case` values — `ObjType::map`, `OpType::put`
- **Files**: `snake_case` — `document.hpp`, `op_set.cpp`
- **Macros**: `SCREAMING_SNAKE_CASE` (avoid macros when possible)

### File Organization

- **Public headers**: `include/automerge-cpp/*.hpp`
- **Implementation**: `src/*.cpp` and `src/**/*.cpp`
- **Internal headers**: `src/*.hpp` (not installed)
- **Tests**: `tests/*.cpp`
- **Examples**: `examples/*.cpp`
- **Benchmarks**: `benchmarks/*.cpp`

### Header Style

```cpp
#pragma once

#include <automerge-cpp/types.hpp>  // project headers first

#include <cstdint>                   // C++ standard headers
#include <expected>
#include <string>
#include <variant>

namespace automerge_cpp {

// ... declarations ...

}  // namespace automerge_cpp
```

## Architecture

See [docs/plans/architecture.md](docs/plans/architecture.md) for the full design.

### Key Modules

| Module | Responsibility |
|--------|---------------|
| `Document` | User-facing API, owns the op set and change graph |
| `Transaction` | Mutation interface, produces operations |
| `OpSet` (internal) | Columnar storage of all operations |
| `ChangeGraph` (internal) | DAG of changes with dependency tracking |
| `SyncState` | Per-peer synchronization state machine |
| `storage/` (internal) | Binary serialization/deserialization |

### Key Invariants

- A `Document` is always in a valid state (no partial mutations).
- Operations have globally unique `OpId`s (monotonic counter + actor).
- The change DAG is acyclic (enforced by hash-based dependencies).
- Merge is commutative, associative, and idempotent.

## Testing

- **Unit tests**: use Google Test, one test file per module
- **Property tests**: verify CRDT algebraic properties (commutativity, etc.)
- **Interop tests**: round-trip with upstream Rust binary format
- **Naming**: `TEST(ModuleName, descriptive_behavior_name)`

```cpp
TEST(DocumentTest, put_and_get_round_trips) {
    auto doc = Document{};
    doc.transact([](auto& tx) {
        tx.put(root, "key", std::int64_t{42});
    });
    auto val = doc.get(root, "key");
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(std::get<std::int64_t>(std::get<ScalarValue>(*val)), 42);
}
```

## Dependencies

- **Build**: CMake 3.28+, C++23 compiler
- **Test**: Google Test (fetched via CMake FetchContent)
- **Bench**: Google Benchmark (fetched via CMake FetchContent)
- **Crypto**: SHA-256 (vendored or system)
- **Compression**: zlib / libdeflate

## Upstream Reference

The upstream Rust implementation is available as a git submodule at
`upstream/automerge/`. Key reference paths:

- Core library: `upstream/automerge/rust/automerge/src/`
- C bindings (API reference): `upstream/automerge/rust/automerge-c/`
- Types: `upstream/automerge/rust/automerge/src/types.rs`
- Document: `upstream/automerge/rust/automerge/src/automerge.rs`
- Sync: `upstream/automerge/rust/automerge/src/sync.rs`
