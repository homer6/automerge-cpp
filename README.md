# automerge-cpp

![Linux](https://github.com/homer6/automerge-cpp/actions/workflows/linux.yml/badge.svg)
![macOS](https://github.com/homer6/automerge-cpp/actions/workflows/macos.yml/badge.svg)
![Windows](https://github.com/homer6/automerge-cpp/actions/workflows/windows.yml/badge.svg)
![FreeBSD](https://github.com/homer6/automerge-cpp/actions/workflows/freebsd.yml/badge.svg)

A modern C++23 implementation of [Automerge](https://automerge.org/) — a conflict-free
replicated data type (CRDT) library for building collaborative applications.

This is a **from-scratch** reimplementation, not a wrapper. It mirrors the upstream
Automerge semantics while embracing idiomatic C++23: algebraic types, ranges pipelines,
strong types, and APIs that make illegal states unrepresentable.

**195 tests passing** across 6 implementation phases.

## Quick Example

```cpp
#include <automerge-cpp/automerge.hpp>
namespace am = automerge_cpp;

int main() {
    auto doc1 = am::Document{};
    am::ObjId list_id;

    doc1.transact([&](auto& tx) {
        tx.put(am::root, "title", std::string{"Shopping List"});
        list_id = tx.put_object(am::root, "items", am::ObjType::list);
        tx.insert(list_id, 0, std::string{"Milk"});
        tx.insert(list_id, 1, std::string{"Eggs"});
    });

    // Fork and make concurrent edits
    auto doc2 = doc1.fork();

    doc1.transact([&](auto& tx) {
        tx.insert(list_id, 2, std::string{"Bread"});
    });

    doc2.transact([&](auto& tx) {
        tx.insert(list_id, 2, std::string{"Butter"});
    });

    // Merge — both edits preserved, no data loss
    doc1.merge(doc2);
    // list now contains: Milk, Eggs, Bread, Butter (deterministic order)

    // Save to binary and reload
    auto bytes = doc1.save();
    auto loaded = am::Document::load(bytes);

    // Time travel — read past state
    auto heads_v1 = doc1.get_heads();
    auto past = doc1.get_at(am::root, "title", heads_v1);
}
```

## Features

### Implemented

- **CRDT data types**: Map, List, Text, Counter
- **Conflict-free merging**: concurrent edits merge deterministically (RGA for lists/text)
- **Fork and merge**: create independent document copies, merge them back
- **Binary serialization**: `save()` / `load()` with LEB128 variable-length encoding
- **Sync protocol**: bloom filter-based peer-to-peer synchronization
- **Patches**: incremental change notifications via `transact_with_patches()`
- **Time travel**: read document state at any historical point (`get_at()`, `text_at()`, etc.)
- **Cursors**: stable positions in lists/text that survive edits and merges
- **Strong types**: `ActorId`, `ObjId`, `ChangeHash`, `OpId` never implicitly convert
- **Type-safe values**: `std::variant`-based `ScalarValue` and `Value` types

### Planned

- Rich text marks (bold, italic, etc.)
- Upstream Rust binary format interoperability
- Columnar encoding and DEFLATE compression

## Performance

Release-build highlights (Apple M3 Max):

| Operation | Throughput |
|-----------|------------|
| Map put | 3.3 M ops/s |
| Map get | 29.3 M ops/s |
| List get | 4.5 M ops/s |
| Save (100 keys) | 237.7 K ops/s |
| Load (100 keys) | 53.7 K ops/s |
| Merge (10+10 puts) | 305.8 K ops/s |
| Cursor resolve | 6.1 M ops/s |
| Time travel get_at | 1.3 M ops/s |

See [docs/benchmark-results.md](docs/benchmark-results.md) for full results.

## Design Philosophy

Inspired by Ben Deane's approach to modern C++:

- **Make illegal states unrepresentable** — algebraic types model the domain precisely
- **Algorithms over raw loops** — `std::ranges` pipelines, folds, transforms
- **CRDTs are monoids** — merge is associative, commutative, and idempotent
- **Strong types prevent mixups** — `ActorId`, `ObjId`, `ChangeHash` never interconvert
- **Value semantics** — immutable outside transactions, explicit mutation boundaries

## Building

### Requirements

- C++23 compiler (GCC 14+, Clang 18+, MSVC 19.38+)
- CMake 3.28+

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
    value.hpp                 #   ScalarValue, Value, ObjType
    change.hpp                #   Change struct
    op.hpp                    #   Op, OpType
    sync_state.hpp            #   SyncState, SyncMessage
    patch.hpp                 #   Patch, PatchAction types
    cursor.hpp                #   Cursor (stable position)
    error.hpp                 #   Error, ErrorKind
  src/                        # implementation
    document.cpp              #   Document methods
    transaction.cpp           #   Transaction methods
    doc_state.hpp             #   internal: DocState, ObjectState
    encoding/                 #   LEB128 codec
    storage/                  #   binary serializer/deserializer
    sync/                     #   bloom filter for sync protocol
  tests/                      # unit and integration tests
  examples/                   # example programs
  benchmarks/                 # performance benchmarks
  docs/                       # documentation
    api.md                    #   API reference
    style.md                  #   coding style guide
    plans/                    #   architecture and roadmap
  upstream/automerge/         # upstream Rust reference (git submodule)
```

## Examples

Four example programs in `examples/`:

| Example | Description |
|---------|-------------|
| `basic_usage` | Create doc, put/get values, counters, save/load |
| `collaborative_todo` | Two actors concurrently editing a shared todo list |
| `text_editor` | Text editing with patches, cursors, and time travel |
| `sync_demo` | Peer-to-peer sync with SyncState |

```bash
./build/examples/basic_usage
./build/examples/collaborative_todo
./build/examples/text_editor
./build/examples/sync_demo
```

## Documentation

- [API Reference](docs/api.md) — every public type, method, and usage examples
- [Benchmark Results](docs/benchmark-results.md) — performance measurements
- [Style Guide](docs/style.md) — coding style and conventions (Ben Deane's modern C++ principles)
- [Architecture Plan](docs/plans/architecture.md) — design principles, core types, module layout
- [Implementation Roadmap](docs/plans/roadmap.md) — phased development plan with status

## License

MIT License. See [LICENSE](LICENSE).

## Acknowledgments

- [Automerge](https://automerge.org/) — the original CRDT library by Martin Kleppmann et al.
- [automerge-rs](https://github.com/automerge/automerge) — the upstream Rust implementation
