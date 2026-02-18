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
│   ├── sync_state.hpp                      #   SyncState, SyncMessage, Have
│   ├── patch.hpp                           #   Patch, PatchAction types
│   ├── cursor.hpp                          #   Cursor (stable position)
│   ├── mark.hpp                            #   Mark (rich text annotation)
│   └── error.hpp                           #   Error, ErrorKind
├── src/                                    # IMPLEMENTATION
│   ├── doc_state.hpp                       #   internal: DocState, ObjectState, MapEntry, ListElement, MarkEntry
│   ├── document.cpp                        #   Document methods (core, save/load, sync, patches, time travel, cursors, marks)
│   ├── transaction.cpp                     #   Transaction methods
│   ├── encoding/                           #   LEB128 variable-length integer codec
│   │   └── leb128.hpp
│   ├── storage/                            #   Binary serialization
│   │   ├── serializer.hpp                  #     byte stream writer
│   │   └── deserializer.hpp                #     byte stream reader
│   └── sync/                               #   Sync protocol internals
│       └── bloom_filter.hpp                #     bloom filter (10 bits/entry, 7 probes)
├── tests/                                  # TESTS (Google Test) — 207 tests
│   ├── CMakeLists.txt
│   ├── error_test.cpp
│   ├── types_test.cpp
│   ├── value_test.cpp
│   ├── op_test.cpp
│   ├── change_test.cpp
│   ├── document_test.cpp                   #   core, merge, serialization, sync, patches, time travel, cursors, marks
│   └── leb128_test.cpp
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
│       └── roadmap.md                      #   phased implementation plan (Phases 0-6 complete)
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
| `Document` | User-facing API, owns the CRDT state, provides reads, gates mutations |
| `Transaction` | Mutation interface, produces operations, commits changes |
| `DocState` (internal) | Object store, op log, change history, RGA merge, time travel |
| `SyncState` | Per-peer synchronization state machine |
| `BloomFilter` (internal) | Probabilistic set for sync protocol change discovery |
| `storage/` (internal) | Binary serialization/deserialization (LEB128-based) |
| `Patch` | Incremental change notifications from transactions |
| `Cursor` | Stable position tracking in lists/text |
| `Mark` | Rich text range annotations (bold, italic, links) |

### Key Invariants

- A `Document` is always in a valid state (no partial mutations).
- Operations have globally unique `OpId`s (monotonic counter + actor).
- The change DAG is acyclic (enforced by hash-based dependencies).
- Merge is commutative, associative, and idempotent.
- Cursors track by element identity (OpId), not by index.
- Marks are anchored to element OpIds for merge correctness.

## Testing

207 tests across 7 test files. Uses Google Test (fetched via CMake FetchContent).

| Test File | Count | Covers |
|-----------|-------|--------|
| `error_test.cpp` | 4 | Error, ErrorKind |
| `types_test.cpp` | 22 | ActorId, ChangeHash, OpId, ObjId, Prop |
| `value_test.cpp` | 14 | ScalarValue, Value, ObjType, Null, Counter, Timestamp |
| `op_test.cpp` | 4 | Op, OpType |
| `change_test.cpp` | 4 | Change |
| `document_test.cpp` | 137 | Document core, merge, serialization, sync, patches, time travel, cursors, marks |
| `leb128_test.cpp` | 22 | LEB128 encode/decode |

- **Property tests**: verify CRDT algebraic properties (commutativity, associativity, idempotency)
- **Naming**: `TEST(ModuleName, descriptive_behavior_name)`

```cpp
TEST(Document, put_and_get_round_trips) {
    auto doc = Document{};
    doc.transact([](auto& tx) {
        tx.put(root, "key", std::int64_t{42});
    });
    auto val = doc.get(root, "key");
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(std::get<std::int64_t>(std::get<ScalarValue>(*val)), 42);
}
```

## Examples

Four example programs in `examples/`:

| Example | Description |
|---------|-------------|
| `basic_usage` | Create doc, put/get values, counters, save/load |
| `collaborative_todo` | Two actors concurrently editing a shared todo list |
| `text_editor` | Text editing with patches, cursors, and time travel |
| `sync_demo` | Peer-to-peer sync with SyncState |

## Benchmarks

26 benchmarks in `benchmarks/placeholder_benchmark.cpp` using Google Benchmark.
Run with: `./build-release/benchmarks/automerge_cpp_benchmarks`

Key release-build results (Apple M3 Max):

| Operation | Throughput |
|-----------|------------|
| Map put | 3.3 M ops/s |
| Map get | 29.3 M ops/s |
| Merge | 305.8 K ops/s |
| Cursor resolve | 6.1 M ops/s |

See [docs/benchmark-results.md](docs/benchmark-results.md) for full results.

## Documentation

| Document | Path | Description |
|----------|------|-------------|
| API Reference | [docs/api.md](docs/api.md) | Every public type, method, and usage examples |
| Benchmark Results | [docs/benchmark-results.md](docs/benchmark-results.md) | Performance measurements |
| Style Guide | [docs/style.md](docs/style.md) | Coding conventions (Ben Deane principles) |
| Architecture | [docs/plans/architecture.md](docs/plans/architecture.md) | Design, types, modules, data model |
| Roadmap | [docs/plans/roadmap.md](docs/plans/roadmap.md) | Phased implementation plan (Phases 0-7 complete) |

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
