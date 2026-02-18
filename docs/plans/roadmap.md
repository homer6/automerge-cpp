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
**Status**: Complete — 87 tests passing (53 Phase 1 + 34 Phase 2)

### Deliverables
- [x] `src/doc_state.hpp` — internal document state (ObjectState, MapEntry, ListElement)
- [x] `document.hpp` / `document.cpp` — Document class with pimpl
- [x] `transaction.hpp` / `transaction.cpp` — Transaction class
- [x] `transact()` via `std::function<void(Transaction&)>`
- [x] Map operations: `put`, `put_object`, `delete_key`, `get`, `get_all`, `keys`, `values`
- [x] List operations: `insert`, `insert_object`, `set`, `delete_index`, `get(index)`, `length`
- [x] Text operations: `splice_text`, `text()`
- [x] Counter operations: `increment`
- [x] Nested objects: `put_object` / `insert_object` return `ObjId`
- [x] Copy semantics: `Document` is copyable (deep copy, independent state)

### Design Decisions (implemented)
- `Document` uses pimpl (`std::unique_ptr<detail::DocState>`) to hide internals
- `Transaction` holds a reference to `DocState`, created only by `Document::transact`
- Maps use `std::map<string, vector<MapEntry>>` — sorted keys, conflict-ready
- Lists use `std::vector<ListElement>` with visibility flags for soft-delete
- Text stored as list of single-character strings (correct for future merge)
- All operations logged to `op_log` for future Phase 3 merge support

### Tests (34 new)
- [x] Construction, actor_id, root is map, root starts empty
- [x] Map put/get for all scalar types (int, string, bool, double, null)
- [x] Put overwrites, get missing returns nullopt, delete removes
- [x] keys() sorted, values() in key order, length()
- [x] Nested maps (2 levels deep), nested lists, lists in maps
- [x] List insert/get/set/delete, insert at beginning, out-of-bounds
- [x] insert_object into list
- [x] Text splice: insert, append, delete, replace
- [x] Counter put + increment, multiple increments
- [x] Copy creates independent document
- [x] Multiple transactions accumulate
- [x] get_all returns values (single actor = 1), missing = empty

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
