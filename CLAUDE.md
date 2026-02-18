# CLAUDE.md — Development Guide for automerge-cpp

## Project Overview

automerge-cpp is a from-scratch C++23 implementation of the Automerge CRDT library.
It mirrors the upstream Rust Automerge semantics with a modern, declarative C++ API.

## Repository Tree

```
automerge-cpp/
├── CMakeLists.txt                          # root build — library + optional targets
├── include/automerge-cpp/                  # PUBLIC HEADERS (installed)
│   ├── automerge.hpp                       #   umbrella header
│   ├── document.hpp                        #   Document class
│   ├── transaction.hpp                     #   Transaction class
│   ├── types.hpp                           #   ActorId, ObjId, OpId, ChangeHash, Prop
│   ├── value.hpp                           #   ScalarValue, Value, ObjType
│   ├── change.hpp                          #   Change struct
│   ├── op.hpp                              #   Op, OpType
│   └── error.hpp                           #   Error, ErrorKind
├── src/                                    # IMPLEMENTATION
│   ├── doc_state.hpp                       #   internal: DocState, ObjectState, MapEntry, ListElement
│   ├── document.cpp                        #   Document methods
│   └── transaction.cpp                     #   Transaction methods
├── tests/                                  # TESTS (Google Test)
│   ├── CMakeLists.txt
│   ├── error_test.cpp
│   ├── types_test.cpp
│   ├── value_test.cpp
│   ├── op_test.cpp
│   ├── change_test.cpp
│   └── document_test.cpp
├── examples/                               # EXAMPLES
│   ├── CMakeLists.txt
│   └── basic_usage.cpp
├── benchmarks/                             # BENCHMARKS (Google Benchmark)
│   ├── CMakeLists.txt
│   └── placeholder_benchmark.cpp
├── docs/                                   # DOCUMENTATION
│   ├── api.md                              #   API reference
│   ├── style.md                            #   coding style guide (Ben Deane)
│   └── plans/
│       ├── architecture.md                 #   design & module decomposition
│       └── roadmap.md                      #   phased implementation plan
├── .github/workflows/                      # CI
│   ├── linux.yml                           #   GCC + Clang
│   ├── macos.yml                           #   Apple Clang
│   ├── windows.yml                         #   MSVC
│   └── freebsd.yml                         #   Clang (VM)
└── upstream/automerge/                     # UPSTREAM REFERENCE (git submodule)
```

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

**All code must follow [docs/style.md](docs/style.md)** — the project's comprehensive
style guide based on Ben Deane's modern C++ principles. Read it before writing any code.

Key rules (see style guide for full details, examples, and rationale):

1. **No raw loops** in library code — use `std::ranges`, algorithms, or folds
2. **Algebraic types** — `std::variant` for sum types, structs for product types
3. **Make illegal states unrepresentable** — types model the domain precisely
4. **`std::expected` for errors** — no exceptions for expected failure paths
5. **Strong types** — `ActorId`, `ObjId`, `ChangeHash` never implicitly convert
6. **Value semantics** — `const` by default, mutations through transactions
7. **Composition over inheritance** — no deep class hierarchies, use concepts
8. **Immutability by default** — use `const` everywhere, IIFE for complex init

### Naming (quick reference)

| Entity | Style | Examples |
|--------|-------|---------|
| Namespace | `snake_case` | `automerge_cpp` |
| Type | `PascalCase` | `Document`, `ActorId` |
| Function/method | `snake_case` | `put_object`, `get_changes` |
| Constant | `snake_case` | `root` |
| Enum value | `snake_case` | `ObjType::map` |
| File | `snake_case` | `document.hpp` |

### File Layout

| Location | Contents |
|----------|----------|
| `include/automerge-cpp/*.hpp` | Public headers |
| `src/*.cpp`, `src/**/*.cpp` | Implementation |
| `src/*.hpp` | Internal headers (not installed) |
| `tests/*.cpp` | Tests |
| `examples/*.cpp` | Examples |
| `benchmarks/*.cpp` | Benchmarks |

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

## Documentation

| Document | Path | Description |
|----------|------|-------------|
| API Reference | [docs/api.md](docs/api.md) | Every public type, method, and usage examples |
| Style Guide | [docs/style.md](docs/style.md) | Coding conventions (Ben Deane principles) |
| Architecture | [docs/plans/architecture.md](docs/plans/architecture.md) | Design, types, modules, data model |
| Roadmap | [docs/plans/roadmap.md](docs/plans/roadmap.md) | Phased implementation plan with status |

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
