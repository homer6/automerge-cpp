# automerge-cpp Implementation Roadmap

## Phase 0: Project Scaffolding
**Status**: Complete

- [x] Repository setup (git, license, submodule)
- [x] CMake build system (library, tests, examples, benchmarks)
- [x] CI pipeline (GitHub Actions — Linux GCC/Clang, macOS, Windows MSVC, FreeBSD)
- [x] CLAUDE.md and README.md
- [x] Architecture plan (this document)
- [x] Style guide (docs/style.md)
- [x] Skeleton headers for all public types

---

## Phase 1: Core Types and Value Model
**Status**: Complete — 53 tests passing

### Deliverables
- [x] `error.hpp` — `Error`, `ErrorKind` (7 variants)
- [x] `types.hpp` — `ActorId`, `ObjId`, `OpId`, `ChangeHash`, `Prop` + `std::hash` specializations
- [x] `value.hpp` — `ScalarValue` (9 alternatives), `Value`, `ObjType`, `Null`, `Counter`, `Timestamp`, `Bytes`
- [x] `op.hpp` — `Op`, `OpType` (7 variants)
- [x] `change.hpp` — `Change`
- [x] `automerge.hpp` — umbrella header

### Design Decisions (implemented)
- `ActorId`: fixed-size 16-byte array with `operator<=>`, FNV-1a hashing
- `ChangeHash`: fixed-size 32-byte array (SHA-256 sized)
- `ObjId`: `std::variant<Root, OpId>` — root is a sentinel, non-root is the creating `OpId`
- `OpId`: `(counter, ActorId)` with total ordering (counter first, actor tie-break)
- All types are regular (copyable, movable, comparable, hashable)
- Designated initializers for `Op` and `Change`

### Tests (53 total)
- [x] Round-trip: construct, compare, hash, reconstruct
- [x] Ordering: `ActorId` and `OpId` have deterministic total order
- [x] Hashing: all ID types usable in `std::unordered_set`
- [x] Sorting: `ActorId` sortable via `std::ranges::sort`
- [x] Variant exhaustiveness: every `ScalarValue` alternative tested
- [x] `int64` and `uint64` are distinct alternatives
- [x] `Op` and `Change` equality detects all field differences

---

## Phase 2: Operation Set and Document Core
**Goal**: A `Document` that supports basic map/list/text mutations and reads.

### Deliverables
- `OpSet` (internal) — columnar storage of operations
- `Document` — construction, `transact()`, basic read API
- `Transaction` — `put`, `put_object`, `insert`, `delete_key`, `delete_index`
- Ranges-based read API: `keys()`, `values()`, `length()`, `get()`, `text()`

### Key Algorithms
- Operation insertion with causal ordering
- Conflict resolution (last-writer-wins for maps, positional for lists)
- Counter increment merge semantics

### Tests
- Create doc, put values, read them back
- Nested objects (map within map, list within map, etc.)
- Counter increment and read
- Text splice and read
- Conflict detection: two ops on same key, `get_all()` returns both

---

## Phase 3: Changes and Merge
**Goal**: Documents can be forked, mutated independently, and merged.

### Deliverables
- `Change` serialization (in-memory, not yet binary)
- `Document::fork()` — create independent copy
- `Document::merge()` — merge another document
- `Document::get_changes()` / `apply_changes()`
- Change hashing (SHA-256)

### Key Properties to Test (Monoid Laws)
- **Associativity**: `merge(merge(a, b), c) == merge(a, merge(b, c))`
- **Commutativity**: `merge(a, b) == merge(b, a)` (same final state)
- **Idempotency**: `merge(a, a) == a`
- **Identity**: `merge(a, Document{}) == a`

### Tests
- Fork, mutate both, merge — verify combined state
- Three-way merge
- Concurrent edits to same key (conflict resolution)
- Concurrent list inserts at same index
- Concurrent text edits

---

## Phase 4: Binary Serialization
**Goal**: `save()` and `load()` produce format-compatible binary output.

### Deliverables
- `storage/save.cpp` — document serialization
- `storage/load.cpp` — document deserialization
- `encoding/leb128.hpp` — variable-length integer codec
- `storage/columnar.cpp` — columnar encoding/decoding
- `storage/compression.cpp` — DEFLATE support

### Interoperability
- Load documents saved by the Rust `automerge` library
- Save documents and verify the Rust library can load them
- Test with the upstream test vectors (if available)

### Tests
- Round-trip: save then load, verify identical state
- Load upstream Rust-generated documents
- Corrupt data: `load()` returns `Error`, never crashes
- Compression on/off round-trips

---

## Phase 5: Sync Protocol
**Goal**: Two `Document` instances can synchronize over a byte stream.

### Deliverables
- `SyncState` — per-peer state machine
- `sync/bloom_filter.cpp` — bloom filter encode/decode
- `sync/message.cpp` — sync message encode/decode
- `generate_message()` / `receive_message()` API

### Tests
- Two fresh documents sync to identical state
- Partially overlapping documents sync efficiently
- Already-in-sync documents produce no messages
- Interop: sync C++ doc with Rust doc via message bytes

---

## Phase 6: Advanced Features
**Goal**: Rich text marks, cursors, patches, time travel.

### Deliverables
- `marks.hpp` — rich text mark API
- `cursor.hpp` — stable cursor positioning
- `patch.hpp` — incremental change notifications
- Time-travel reads: `get_at()`, `keys_at()`, etc.

### Tests
- Marks: apply, merge, query
- Cursors: track position through edits and merges
- Patches: verify correct diff after mutations and merges
- Historical queries: read state at past heads

---

## Phase 7: Performance and Polish
**Goal**: Competitive performance, complete documentation, stable API.

### Deliverables
- Benchmark suite (Google Benchmark)
- Performance profiling and optimization
- API documentation (Doxygen or similar)
- Example programs
- Fuzz testing harness

### Benchmark Targets
- Map put: >= 1M ops/sec
- List insert: >= 500K ops/sec
- Text splice: >= 500K ops/sec
- Save/Load: >= 100 MB/sec
- Merge: proportional to change count, not document size

---

## Example Programs (Planned)

| Example | Description |
|---------|-------------|
| `basic_usage` | Create doc, put/get values, save/load |
| `collaborative_todo` | Two actors editing a shared todo list |
| `text_editor` | Concurrent text editing with merge |
| `sync_demo` | Two documents syncing over a simulated network |
| `counter_demo` | Distributed counter with concurrent increments |
| `history_browser` | Time-travel through document history |
