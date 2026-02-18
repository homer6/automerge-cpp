# automerge-cpp

![Build](https://github.com/homer6/automerge-cpp/actions/workflows/build.yml/badge.svg)

A modern C++23 implementation of [Automerge](https://automerge.org/) — a conflict-free
replicated data type (CRDT) library for building collaborative applications.

This is a **from-scratch** reimplementation, not a wrapper. It mirrors the upstream
Automerge semantics while embracing idiomatic C++23: algebraic types, ranges pipelines,
`std::expected` error handling, and APIs that make illegal states unrepresentable.

## Quick Example

```cpp
#include <automerge-cpp/automerge.hpp>
namespace am = automerge_cpp;

int main() {
    // Create a document
    auto doc1 = am::Document{};

    doc1.transact([](auto& tx) {
        tx.put(am::root, "title", "Shopping List");
        auto list = tx.put_object(am::root, "items", am::ObjType::list);
        tx.insert(list, 0, "Milk");
        tx.insert(list, 1, "Eggs");
    });

    // Fork and make concurrent edits
    auto doc2 = doc1.fork();

    doc1.transact([](auto& tx) {
        tx.insert(/* items list */ am::root, 2, "Bread");
    });

    doc2.transact([](auto& tx) {
        tx.insert(/* items list */ am::root, 2, "Butter");
    });

    // Merge — both edits are preserved, no data loss
    doc1.merge(doc2);

    // Iterate with ranges
    for (auto val : doc1.values(/* items list */)) {
        std::println("{}", val);
    }
    // Output: Milk, Eggs, Bread, Butter (order is deterministic)
}
```

## Features

- **CRDT data types**: Map, List, Text, Counter, Table
- **Conflict-free merging**: concurrent edits merge deterministically
- **Rich text**: marks/annotations for formatting
- **Sync protocol**: efficient peer-to-peer synchronization
- **Time travel**: read document state at any historical point
- **Binary format**: interoperable with Rust/JS Automerge implementations
- **Ranges-first API**: all iteration composes with `std::ranges`
- **Type-safe**: `std::variant`, `std::expected`, strong ID types

## Design Philosophy

Inspired by Ben Deane's approach to modern C++:

- **Make illegal states unrepresentable** — algebraic types model the domain precisely
- **Algorithms over raw loops** — `std::ranges` pipelines, folds, transforms
- **CRDTs are monoids** — merge is associative with an identity element
- **Strong types prevent mixups** — `ActorId`, `ObjId`, `ChangeHash` never interconvert
- **Errors in the type system** — `std::expected` instead of exceptions
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
    automerge.hpp             # umbrella header
    document.hpp              # Document class
    transaction.hpp           # Transaction class
    types.hpp                 # ActorId, ObjId, OpId, ChangeHash, Prop
    value.hpp                 # ScalarValue, Value, ObjType
    change.hpp                # Change struct
    op.hpp                    # Op, OpType
    sync.hpp                  # SyncState
    error.hpp                 # Error types
    patch.hpp                 # Incremental change notifications
    cursor.hpp                # Stable position tracking
    marks.hpp                 # Rich text marks
  src/                        # implementation
  tests/                      # unit and integration tests
  examples/                   # example programs
  benchmarks/                 # performance benchmarks
  docs/plans/                 # architecture and roadmap
  upstream/automerge/         # upstream Rust reference (git submodule)
```

## Documentation

- [Style Guide](docs/style.md) — coding style and conventions (Ben Deane's modern C++ principles)
- [Architecture Plan](docs/plans/architecture.md) — design principles, core types, module layout
- [Implementation Roadmap](docs/plans/roadmap.md) — phased development plan

## License

MIT License. See [LICENSE](LICENSE).

## Acknowledgments

- [Automerge](https://automerge.org/) — the original CRDT library by Martin Kleppmann et al.
- [automerge-rs](https://github.com/automerge/automerge) — the upstream Rust implementation
