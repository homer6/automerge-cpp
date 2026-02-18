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

**281 tests passing** across 8 implementation phases.

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
- **Binary serialization**: `save()` / `load()` with upstream-compatible columnar encoding
- **Sync protocol**: bloom filter-based peer-to-peer synchronization
- **Patches**: incremental change notifications via `transact_with_patches()`
- **Time travel**: read document state at any historical point (`get_at()`, `text_at()`, etc.)
- **Cursors**: stable positions in lists/text that survive edits and merges
- **Rich text marks**: range annotations (bold, italic, links) anchored by identity, not index
- **Strong types**: `ActorId`, `ObjId`, `ChangeHash`, `OpId` never implicitly convert
- **Type-safe values**: `std::variant`-based `ScalarValue` and `Value` types

- **Columnar encoding**: upstream-compatible columnar op encoding with RLE, delta, and boolean encoders
- **DEFLATE compression**: raw DEFLATE (no zlib/gzip header) for columns exceeding 256 bytes, matching upstream Rust format
- **SHA-256 checksums**: chunk envelope with SHA-256 integrity validation
- **Backward compatibility**: v1 format loading with automatic format detection

- **Fuzz testing**: libFuzzer targets for `Document::load()`, LEB128 decode, and change chunk parsing
- **Static analysis**: clang-tidy CI with `bugprone-*`, `performance-*`, and `clang-analyzer-*` checks
- **Sanitizer CI**: Address Sanitizer + Undefined Behavior Sanitizer on all tests

- **Thread safety**: `Document` is thread-safe via `std::shared_mutex` — N concurrent readers, exclusive writers
- **Thread pool**: built-in work-stealing thread pool (Barak Shoshany's BS::thread_pool), shared across documents
- **Lock-free reads**: `set_read_locking(false)` eliminates shared_mutex contention for read-heavy workloads (13.5x parallel scaling on 30 cores)
- **Performance caching**: change hash cache (22x faster time travel), actor table cache (1.2x faster save)

- **Doxygen documentation**: auto-generated API docs with GitHub Pages deployment

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
| Put (1M keys, sharded) | 1.7 M ops/s | **8.4 M ops/s** | **4.8x** |
| Save 500 docs | 95 K docs/s | **806 K docs/s** | **8.4x** |
| Load 500 docs | 48 K docs/s | **190 K docs/s** | **3.9x** |

v0.4.0 optimizations: **22x** faster time travel, **5.6x** faster sync (hash/actor table caching),
**13.5x** parallel read scaling (lock-free reads eliminate shared_mutex contention).

See [docs/benchmark-results.md](docs/benchmark-results.md) for full results, platform comparison,
and perf analysis.

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
    value.hpp                 #   ScalarValue, Value, ObjType
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
      columns/                #   column spec, op encoding, value encoding, compression
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

Six example programs in `examples/`:

| Example | Description |
|---------|-------------|
| `basic_usage` | Create doc, put/get values, counters, save/load |
| `collaborative_todo` | Two actors concurrently editing a shared todo list |
| `text_editor` | Text editing with patches, cursors, and time travel |
| `sync_demo` | Peer-to-peer sync with SyncState |
| `thread_safe_demo` | Multi-threaded concurrent reads and writes on a single Document |
| `parallel_perf_demo` | Monoid-powered fork/merge parallelism, parallel save/load/sync |

```bash
./build/examples/basic_usage
./build/examples/collaborative_todo
./build/examples/text_editor
./build/examples/sync_demo
./build/examples/thread_safe_demo
./build/examples/parallel_perf_demo
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
