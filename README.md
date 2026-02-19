# automerge-cpp

![Linux](https://github.com/homer6/automerge-cpp/actions/workflows/linux.yml/badge.svg)
![macOS](https://github.com/homer6/automerge-cpp/actions/workflows/macos.yml/badge.svg)
![Windows](https://github.com/homer6/automerge-cpp/actions/workflows/windows.yml/badge.svg)
![FreeBSD](https://github.com/homer6/automerge-cpp/actions/workflows/freebsd.yml/badge.svg)

A modern C++23 implementation of [Automerge](https://automerge.org/) — a conflict-free
replicated data type (CRDT) library for building collaborative applications.

This is a **from-scratch** reimplementation, not a wrapper. It mirrors the upstream
Automerge semantics while embracing idiomatic C++23: algebraic types, ranges pipelines,
strong types, and an API inspired by [nlohmann/json](https://github.com/nlohmann/json).

**470 tests passing** across 13 test files. [nlohmann/json](https://github.com/nlohmann/json) interoperability included.

## Quick Example

```cpp
#include <automerge-cpp/automerge.hpp>
namespace am = automerge_cpp;

int main() {
    auto doc = am::Document{};

    // Initializer lists — like nlohmann/json
    auto list_id = doc.transact([](am::Transaction& tx) {
        tx.put(am::root, "title", "Shopping List");
        tx.put(am::root, "count", 0);
        return tx.put(am::root, "items", am::List{"Milk", "Eggs", "Bread"});
    });

    // Typed get<T>() — returns std::optional<T>
    auto title = doc.get<std::string>(am::root, "title");   // optional<string>
    auto count = doc.get<std::int64_t>(am::root, "count");  // optional<int64_t>

    // get_path() for nested access
    auto first = doc.get_path("items", std::size_t{0});     // optional<Value>

    // Fork and make concurrent edits
    auto doc2 = doc.fork();

    doc.transact([&](auto& tx) { tx.insert(list_id, 3, "Butter"); });
    doc2.transact([&](auto& tx) { tx.insert(list_id, 3, "Cheese"); });

    // Merge — both edits preserved, no data loss
    doc.merge(doc2);

    // Save to binary and reload
    auto bytes = doc.save();
    auto loaded = am::Document::load(bytes);
}
```

## Modern C++ API

The API is designed to feel like modern C++ — inspired by nlohmann/json's ergonomics:

### Typed `get<T>()` — no variant unwrapping

```cpp
auto name = doc.get<std::string>(root, "name");       // optional<string>
auto age  = doc.get<std::int64_t>(root, "age");        // optional<int64_t>
auto pi   = doc.get<double>(root, "pi");               // optional<double>
auto ok   = doc.get<bool>(root, "active");             // optional<bool>
auto hits = doc.get<Counter>(root, "hits");            // optional<Counter>
```

### Scalar overloads — no wrapping in `ScalarValue{}`

```cpp
doc.transact([](auto& tx) {
    tx.put(root, "name", "Alice");                     // const char*
    tx.put(root, "age", 30);                           // int → int64_t
    tx.put(root, "score", 99.5);                       // double
    tx.put(root, "active", true);                      // bool
    tx.put(root, "views", Counter{0});                 // Counter
    tx.put(root, "empty", Null{});                     // null
});
```

### Transact with return values

```cpp
// Lambda return type is deduced — no external variable needed
auto list_id = doc.transact([](Transaction& tx) {
    return tx.put(root, "items", {"Milk", "Eggs", "Bread"});
});

// transact_with_patches returns {result, patches}
auto [obj_id, patches] = doc.transact_with_patches([](Transaction& tx) {
    return tx.put(root, "data", Map{});
});
```

### Initializer lists — like nlohmann/json

```cpp
doc.transact([](auto& tx) {
    // Bare initializer lists — auto-detects list vs map
    auto items = tx.put(root, "items", {"Milk", "Eggs", "Bread"});
    auto config = tx.put(root, "config", {{"port", 8080}, {"host", "localhost"}});

    // Explicit wrappers also work
    auto tags = tx.put(root, "tags", List{"crdt", "cpp", "collaborative"});
    auto meta = tx.put(root, "meta", Map{{"version", "1.0"}, {"stable", true}});

    // Mixed types work naturally
    auto mixed = tx.put(root, "data", List{1, "hello", 3.14, true});

    // Insert populated objects into lists
    auto records = tx.put(root, "records", ObjType::list);
    tx.insert(records, 0, {{"name", "Alice"}, {"role", "admin"}});
    tx.insert(records, 1, {{"name", "Bob"}, {"role", "editor"}});
});
```

### STL containers — vector, set, map

```cpp
// std::vector → creates a list
auto tags = std::vector<std::string>{"crdt", "cpp", "collaborative"};
doc.transact([&](auto& tx) { tx.put(root, "tags", tags); });

// std::set → creates a list (sorted)
auto unique = std::set<std::string>{"alpha", "beta", "gamma"};
doc.transact([&](auto& tx) { tx.put(root, "sorted", unique); });

// std::map → creates a map
auto dims = std::map<std::string, ScalarValue>{
    {"w", ScalarValue{std::int64_t{800}}},
    {"h", ScalarValue{std::int64_t{600}}},
};
doc.transact([&](auto& tx) { tx.put(root, "dims", dims); });
```

### Path-based nested access

```cpp
auto port = doc.get_path("config", "database", "port");
auto item = doc.get_path("todos", std::size_t{0}, "title");
```

### Variant visitor helper

```cpp
std::visit(overload{
    [](std::string s)  { printf("%s\n", s.c_str()); },
    [](std::int64_t i) { printf("%ld\n", i); },
    [](auto&&)         { printf("other\n"); },
}, some_scalar_value);
```

## nlohmann/json Interoperability

automerge-cpp includes [nlohmann/json](https://github.com/nlohmann/json) as a dependency
and provides example patterns for importing/exporting JSON data:

```cpp
#include <automerge-cpp/automerge.hpp>
#include <nlohmann/json.hpp>

// Import a JSON object into an Automerge document
auto input = json::parse(R"({"name": "Alice", "scores": [10, 20, 30]})");
import_json(doc, input);

// Access imported data with typed get<T>() and get_path()
auto name = doc.get<std::string>(root, "name");        // "Alice"
auto score = doc.get_path("scores", std::size_t{0});   // 10

// Export Automerge state back to JSON
auto output = export_json(doc);
```

See [`examples/json_interop_demo.cpp`](examples/json_interop_demo.cpp) for a full working example
with nested objects, fork/merge round-trips, and save/load verification.

## Features

### Core CRDT

- **Data types**: Map, List, Text, Counter, Table
- **Conflict-free merging**: concurrent edits merge deterministically (RGA for lists/text)
- **Fork and merge**: create independent document copies, merge them back
- **Strong types**: `ActorId`, `ObjId`, `ChangeHash`, `OpId` never implicitly convert
- **Type-safe values**: `std::variant`-based `ScalarValue` and `Value` types

### Modern C++ API (Phase 12A)

- **Typed `get<T>()`**: returns `optional<T>` directly, no variant unwrapping
- **Scalar overloads**: `put`, `insert`, `set` accept native C++ types
- **Transact with return values**: lambda return type is deduced
- **Batch operations**: `put_all`, `insert_all`, `put_map`, `insert_range`
- **`operator[]`**: root map access
- **`get_path()`**: variadic nested access
- **`overload{}` helper**: ad-hoc variant visitors
- **nlohmann/json interop**: import/export patterns

### Serialization and Sync

- **Binary serialization**: `save()` / `load()` with upstream-compatible columnar encoding
- **Columnar encoding**: RLE, delta, and boolean encoders matching upstream Rust format
- **DEFLATE compression**: raw DEFLATE for columns exceeding 256 bytes
- **SHA-256 checksums**: chunk envelope with integrity validation
- **Backward compatibility**: v1 format loading with automatic detection
- **Sync protocol**: bloom filter-based peer-to-peer synchronization

### Advanced Features

- **Patches**: incremental change notifications via `transact_with_patches()`
- **Time travel**: read document state at any historical point (`get_at()`, `text_at()`, etc.)
- **Cursors**: stable positions in lists/text that survive edits and merges
- **Rich text marks**: range annotations (bold, italic, links) anchored by identity

### Quality and Performance

- **470 tests** across 13 test files
- **Fuzz testing**: libFuzzer targets for `Document::load()`, LEB128, and change chunk parsing
- **Static analysis**: clang-tidy CI with `bugprone-*`, `performance-*`, `clang-analyzer-*`
- **Sanitizer CI**: Address Sanitizer + Undefined Behavior Sanitizer
- **Thread safety**: `std::shared_mutex` — N concurrent readers, exclusive writers
- **Thread pool**: built-in BS::thread_pool, shared across documents
- **Lock-free reads**: 13.5x parallel scaling on 30 cores

## Performance

Release-build highlights (Intel Xeon Platinum 8358, 30 cores, Linux, GCC 13.3, `-O3 -march=native`):

### Single-Threaded

| Operation | Throughput |
|-----------|------------|
| Map put (batched) | 4.3 M ops/s |
| Map get | 19.8 M ops/s |
| Sync round trip | 23.6 K ops/s |
| Time travel get_at | 2.9 M ops/s |
| Merge (10+10 puts) | 241 K ops/s |
| Cursor resolve | 1.8 M ops/s |
| Save (100 keys) | 30.6 K ops/s |

### Parallel Scaling (30 cores)

| Operation | Sequential | Parallel | Speedup |
|-----------|-----------|----------|---------|
| Get (100K keys, lock-free) | 7.1 M ops/s | **95.0 M ops/s** | **13.5x** |
| Get (1M keys, lock-free) | 5.7 M ops/s | **55.0 M ops/s** | **9.6x** |
| Put (100K keys, sharded) | 2.0 M ops/s | **13.5 M ops/s** | **6.9x** |
| Save 500 docs | 95 K docs/s | **806 K docs/s** | **8.4x** |

See [docs/benchmark-results.md](docs/benchmark-results.md) for full results.

## Building

### Requirements

- C++23 compiler (GCC 14+, Clang 18+, MSVC 19.38+)
- CMake 3.28+
- zlib (for raw DEFLATE compression)

### Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Build with tests, examples, and benchmarks

```bash
cmake -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DAUTOMERGE_CPP_BUILD_TESTS=ON \
    -DAUTOMERGE_CPP_BUILD_EXAMPLES=ON \
    -DAUTOMERGE_CPP_BUILD_BENCHMARKS=ON

cmake --build build
ctest --test-dir build --output-on-failure
```

### Run benchmarks

```bash
./build/benchmarks/automerge_cpp_benchmarks
```

## Project Structure

```
automerge-cpp/
  include/automerge-cpp/     # public headers
    automerge.hpp             #   umbrella header
    document.hpp              #   Document class
    transaction.hpp           #   Transaction class
    types.hpp                 #   ActorId, ObjId, OpId, ChangeHash, Prop
    value.hpp                 #   ScalarValue, Value, ObjType, overload, get_scalar
    change.hpp                #   Change struct
    op.hpp                    #   Op, OpType
    sync_state.hpp            #   SyncState, SyncMessage
    patch.hpp                 #   Patch, PatchAction types
    cursor.hpp                #   Cursor (stable position)
    mark.hpp                  #   Mark (rich text annotation)
    error.hpp                 #   Error, ErrorKind
    thread_pool.hpp           #   BS::thread_pool (header-only)
  src/                        # implementation
    document.cpp              #   Document methods
    transaction.cpp           #   Transaction methods
    doc_state.hpp             #   internal: DocState, ObjectState
    crypto/                   #   SHA-256 (vendored)
    encoding/                 #   LEB128, RLE, delta, boolean codecs
    storage/                  #   columnar binary format
    sync/                     #   bloom filter for sync protocol
  tests/                      # 470 tests (Google Test)
  examples/                   # 7 example programs
  benchmarks/                 # performance benchmarks (Google Benchmark)
  docs/                       # documentation
    api.md                    #   API reference
    style.md                  #   coding style guide
    plans/                    #   architecture and roadmap
  upstream/
    automerge/                # upstream Rust reference (git submodule)
    json/                     # nlohmann/json (git submodule)
```

## Examples

Seven example programs in `examples/`:

| Example | Description |
|---------|-------------|
| `basic_usage` | Create doc, typed get, operator[], get_path, counters, save/load |
| `collaborative_todo` | Two actors concurrently editing a shared todo list |
| `text_editor` | Text editing with patches, cursors, and time travel |
| `sync_demo` | Peer-to-peer sync with SyncState |
| `thread_safe_demo` | Multi-threaded concurrent reads and writes |
| `parallel_perf_demo` | Monoid-powered fork/merge parallelism |
| `json_interop_demo` | nlohmann/json import/export, fork/merge round-trip |

```bash
./build/examples/basic_usage
./build/examples/collaborative_todo
./build/examples/text_editor
./build/examples/sync_demo
./build/examples/thread_safe_demo
./build/examples/parallel_perf_demo
./build/examples/json_interop_demo
```

## Documentation

- [API Reference](docs/api.md) — every public type, method, and usage examples
- [Benchmark Results](docs/benchmark-results.md) — performance measurements
- [Style Guide](docs/style.md) — coding style (Ben Deane's modern C++ principles)
- [Architecture](docs/plans/architecture.md) — design, types, modules
- [Roadmap](docs/plans/roadmap.md) — phased development plan with status
- [nlohmann/json Interop Plan](docs/plans/nlohmann-json-interop.md) — JSON integration design

## Design Philosophy

Inspired by Ben Deane's approach to modern C++ and nlohmann/json's API design:

- **Make illegal states unrepresentable** — algebraic types model the domain precisely
- **Algorithms over raw loops** — `std::ranges` pipelines, folds, transforms
- **CRDTs are monoids** — merge is associative, commutative, and idempotent
- **Strong types prevent mixups** — `ActorId`, `ObjId`, `ChangeHash` never interconvert
- **Value semantics** — immutable outside transactions, explicit mutation boundaries
- **Easy to use, hard to misuse** — typed accessors, scalar overloads, deduced return types

## License

MIT License. See [LICENSE](LICENSE).

## Acknowledgments

- [Automerge](https://automerge.org/) — the original CRDT library by Martin Kleppmann et al.
- [automerge-rs](https://github.com/automerge/automerge) — the upstream Rust implementation
- [nlohmann/json](https://github.com/nlohmann/json) — JSON for Modern C++ by Niels Lohmann
