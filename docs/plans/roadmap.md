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
**Status**: Complete — 103 tests passing (87 Phase 1+2 + 16 Phase 3)

### Deliverables
- [x] Change tracking — each `transact()` produces a `Change` with ops, deps, and hash
- [x] `Document::fork()` — create independent copy with unique actor
- [x] `Document::merge()` — merge another document's unseen changes
- [x] `Document::get_changes()` / `apply_changes()` — change-level API
- [x] `Document::get_heads()` — current DAG leaf hashes
- [x] Change hashing (FNV-1a in-memory, SHA-256 deferred to Phase 4)
- [x] RGA list/text merge — `insert_after` tracking, `find_rga_position` algorithm
- [x] Map conflict resolution — multi-value register with predecessor tracking
- [x] Counter merge — concurrent increments accumulate correctly
- [x] Concurrent delete/put — put survives concurrent delete

### Design Decisions (implemented)
- `Op` gains `insert_after: optional<OpId>` for RGA merge tracking
- `Op.pred` populated by Transaction for conflict-aware merge
- `DocState` tracks `change_history`, `heads` (DAG leaves), `clock` (vector clock)
- `apply_op()` handles remote ops with conflict-preserving semantics
- RGA uses scanned-set algorithm for correct subtree skipping
- `map_put` clears all for local ops; `apply_op` removes only predecessors for remote

### Tests (16 new)
- [x] Fork creates independent copy, fork has different actor
- [x] Merge combines independent map edits
- [x] Concurrent map edits create conflict (get_all returns multiple values)
- [x] Concurrent list inserts have deterministic RGA ordering
- [x] Concurrent text edits both present
- [x] Concurrent counter increments sum correctly
- [x] Concurrent delete and put — put survives
- [x] Commutativity: merge(a,b) == merge(b,a)
- [x] Idempotency: merge(a,a) is no-op
- [x] Identity: merge(a, empty) == a
- [x] Three-way merge
- [x] get_changes returns history, apply_changes cross-doc
- [x] Merge nested objects
- [x] get_heads tracks DAG

---

## Phase 4: Binary Serialization
**Status**: Complete — 154 tests passing (103 Phase 1-3 + 22 LEB128 + 29 serialization)

### Deliverables
- [x] `encoding/leb128.hpp` — unsigned/signed LEB128 encode/decode codec
- [x] `storage/serializer.hpp` — byte stream writer (LEB128, raw, strings, ActorId, OpId, ObjId, Prop, Value)
- [x] `storage/deserializer.hpp` — byte stream reader with safe `optional` returns
- [x] `Document::save()` — serialize to binary format
- [x] `Document::load()` — deserialize from binary, returns `optional<Document>`
- [ ] Columnar encoding (deferred — using row-based format for now)
- [ ] DEFLATE compression (deferred)
- [ ] Upstream Rust format interoperability (deferred)

### Binary Format (v1)
- Magic bytes: `0x85 0x6F 0x4A 0x83` + version byte `0x01`
- Deduplicated actor table
- Changes with operations (row-based encoding)
- Heads and clock metadata
- All integers use LEB128 variable-length encoding

### Tests (51 new: 22 LEB128 + 29 serialization)
- [x] LEB128: encode/decode unsigned (0, single byte, multi-byte, max uint64)
- [x] LEB128: encode/decode signed (0, positive, negative, edge cases)
- [x] LEB128: round-trip, stream decoding, truncation returns nullopt
- [x] Save/load round-trip for all scalar types (int, uint, string, bool, double, null, counter, timestamp, bytes)
- [x] Save/load with multiple keys, nested maps, lists, text
- [x] Save/load multiple transactions preserves change history
- [x] Save/load preserves actor_id, heads, and change history
- [x] Save/load after merge preserves merged state
- [x] Loaded document can continue editing and merging
- [x] Save/load with deeply nested objects
- [x] Save/load with delete operations
- [x] Save/load negative integers
- [x] Corrupt data: empty, bad magic, truncated, wrong version → nullopt
- [x] Double round-trip: save → load → save → load

---

## Phase 5: Sync Protocol
**Status**: Complete — 170 tests passing (154 Phase 1-4 + 16 sync)

### Deliverables
- [x] `sync/bloom_filter.hpp` — bloom filter (10 bits/entry, 7 probes, ~1% FPR, LFSR hashing)
- [x] `sync_state.hpp` — `SyncState` per-peer state machine with encode/decode
- [x] `SyncMessage` — message type with heads, need, have (bloom), and changes
- [x] `Document::generate_sync_message()` — produces next message to send
- [x] `Document::receive_sync_message()` — processes received message, applies changes
- [x] Bloom filter-based change discovery (get_hashes_to_send algorithm)
- [x] In-flight message tracking and deduplication
- [ ] Upstream Rust interop via message bytes (deferred)

### Protocol Design
- Bloom filter summarizes changes since last sync point (Have struct)
- LFSR multi-hash from first 12 bytes of ChangeHash for probe positions
- `get_hashes_to_send` uses bloom + transitive dependency closure
- SyncState persistence via encode/decode (shared_heads only)

### Tests (16 new)
- [x] Two fresh documents sync to identical state
- [x] One empty + one populated sync correctly
- [x] Already-in-sync produces few/no change messages
- [x] Sync after concurrent edits preserves all changes
- [x] Sync with list, text, counter, and nested object operations
- [x] Sync multiple transactions
- [x] Three-peer transitive sync
- [x] Incremental sync (multiple rounds of edits + sync)
- [x] Bidirectional concurrent sync
- [x] Sync with delete operations
- [x] First message always generated from empty state
- [x] SyncState encode/decode round-trip
- [x] SyncState decode invalid data returns nullopt

---

## Phase 6: Advanced Features
**Status**: Complete — 207 tests passing (170 Phase 1-5 + 37 Phase 6)

### Deliverables
- [x] `patch.hpp` — Patch types: `PatchPut`, `PatchInsert`, `PatchDelete`, `PatchIncrement`, `PatchSpliceText`
- [x] `cursor.hpp` — `Cursor` type backed by `OpId` for stable positioning
- [x] `mark.hpp` — `Mark` type for rich text annotations
- [x] `Document::transact_with_patches()` — mutation with change notifications
- [x] Patch coalescing — consecutive `splice_text` ops grouped into single `PatchSpliceText`
- [x] Historical reads: `get_at()`, `keys_at()`, `values_at()`, `length_at()`, `text_at()`
- [x] Cursors: `cursor()`, `resolve_cursor()` — stable positions in lists/text
- [x] Rich text marks: `Transaction::mark()`, `Document::marks()`, `Document::marks_at()`

### Design Decisions (implemented)
- `Patch` contains `ObjId obj`, `Prop key`, and `PatchAction` variant
- `PatchSpliceText` coalesces consecutive character insertions and deletions
- Historical reads rebuild state by replaying only changes visible at given heads
- `Cursor` wraps the `OpId` (insert_id) of the target element
- Cursor survives insertions, deletions, and merges because it tracks by identity, not index
- Marks anchored to element OpIds (not indices) for merge correctness
- `MarkEntry` stores `start_elem`/`end_elem` OpIds resolved at read time
- Mark ops reuse Op fields: `key=name`, `value=mark value`, `pred=[start_elem, end_elem]`

### Tests (37 new)
- [x] Patches: map put, map delete, list insert, list delete
- [x] Patches: splice_text insert-only coalesces to PatchSpliceText
- [x] Patches: splice_text replace coalesces deletes + inserts
- [x] Patches: counter increment produces PatchIncrement
- [x] Patches: make_object produces PatchPut with ObjType
- [x] Patches: empty transaction produces no patches
- [x] Historical: get_at reads past map and list values
- [x] Historical: keys_at, values_at, length_at read past state
- [x] Historical: text_at reads past text content
- [x] Historical: missing/deleted key returns nullopt
- [x] Historical: multiple versions all readable
- [x] Cursors: basic create and resolve
- [x] Cursors: survive insert before (index shifts)
- [x] Cursors: survive insert after (index stable)
- [x] Cursors: deleted element returns nullopt
- [x] Cursors: out-of-bounds returns nullopt
- [x] Cursors: work on text objects
- [x] Cursors: survive merge with concurrent inserts
- [x] Marks: basic apply and query
- [x] Marks: multiple non-overlapping marks
- [x] Marks: overlapping ranges
- [x] Marks: string-valued marks (link URLs)
- [x] Marks: survive insert before range (indices shift)
- [x] Marks: survive insert within range (range expands)
- [x] Marks: no marks returns empty vector
- [x] Marks: survive merge with concurrent marks
- [x] Marks: marks_at historical read
- [x] Marks: save/load round-trip
- [x] Marks: sync round-trip
- [x] Marks: mark-only transaction produces no element patches

---

## Phase 7: Performance and Polish
**Status**: Complete — 26 benchmarks, 4 example programs

### Deliverables
- [x] Benchmark suite (26 benchmarks covering all operations via Google Benchmark)
- [x] Example programs: `basic_usage`, `collaborative_todo`, `text_editor`, `sync_demo`
- [x] Benchmark results documented in [docs/benchmark-results.md](../benchmark-results.md)
- [x] README.md and CLAUDE.md updated with current project state
- [ ] Fuzz testing harness (deferred to Phase 9)
- [ ] Doxygen API docs (deferred to Phase 10)

### Benchmark Results (Release, Apple M3 Max)

| Operation | Throughput | Target | Status |
|-----------|-----------|--------|--------|
| Map put | 3.3 M ops/s | >= 1M ops/s | Exceeded |
| Map get | 29.3 M ops/s | — | — |
| List get | 4.5 M ops/s | — | — |
| Save (100 keys) | 237.7 K ops/s | — | — |
| Load (100 keys) | 53.7 K ops/s | — | — |
| Merge (10+10 puts) | 305.8 K ops/s | — | — |
| Cursor resolve | 6.1 M ops/s | — | — |
| Time travel get_at | 1.3 M ops/s | — | — |

See [docs/benchmark-results.md](../benchmark-results.md) for full results.

### Example Programs

| Example | Description |
|---------|-------------|
| `basic_usage` | Create doc, put/get values, counters, save/load |
| `collaborative_todo` | Two actors concurrently editing a shared todo list |
| `text_editor` | Text editing with patches, cursors, and time travel |
| `sync_demo` | Peer-to-peer sync with SyncState |

---

## Phase 8: Upstream Binary Format Interoperability
**Status**: Complete — 274 tests passing (207 Phase 1-7 + 67 Phase 8)

### Deliverables
- [x] `crypto/sha256.hpp` — vendored header-only SHA-256 (FIPS 180-4)
- [x] `encoding/rle.hpp` — `RleEncoder<T>` / `RleDecoder<T>` (runs, literals, null runs)
- [x] `encoding/boolean_encoder.hpp` — `BooleanEncoder` / `BooleanDecoder` (alternating run-length)
- [x] `encoding/delta_encoder.hpp` — `DeltaEncoder` / `DeltaDecoder` (RLE on deltas)
- [x] `storage/columns/column_spec.hpp` — `ColumnType` enum, `ColumnSpec` bitfield `(id << 4) | (deflate << 3) | type`
- [x] `storage/columns/raw_column.hpp` — `RawColumn` struct, column header parse/write
- [x] `storage/columns/compression.hpp` — DEFLATE compress/decompress via zlib (threshold 256 bytes)
- [x] `storage/chunk.hpp` — chunk envelope: magic + SHA-256[:4] checksum + type + LEB128(length)
- [x] `storage/columns/value_encoding.hpp` — value metadata `(byte_length << 4) | type_tag` as ULEB128
- [x] `storage/columns/change_op_columns.hpp` — 14-column op encoding/decoding
- [x] `storage/change_chunk.hpp` — change body serialization/deserialization
- [x] `Document::save()` rewritten to produce v2 chunk-based format
- [x] `Document::load()` supports both v2 (chunk-based) and v1 (row-based) with auto-detection
- [x] `doc_state.hpp` — change hash migrated from FNV-1a to SHA-256

### Design Decisions (implemented)
- Value metadata places type tag in low 4 bits, byte length in upper bits (supports arbitrary value sizes)
- ObjType encoded as uint in value columns to preserve map/table and list/text distinction
- Format detection: try v2 first, fall back to v1 (backward compatible)
- Column layout: OBJ(0), KEY(1), INSERT(3), ACTION(4), VAL(5), PRED(7), EXPAND(9), MARK_NAME(10)
- Action codes: make_map/table=0, put=1, make_list/text=2, del=3, increment=4, mark=5
- Raw DEFLATE compression (no zlib/gzip header, `windowBits=-15`) applied per-column when data exceeds 256 bytes, matching upstream Rust format
- v1 heads recomputation tracks last hash per actor to preserve concurrent heads from merged documents

### Tests (67 new)
- [x] RLE: empty, single value, runs, literal runs, null runs, mixed, string values (10 tests)
- [x] Delta: monotonic, negative deltas, nulls, large sequences (11 tests)
- [x] SHA-256: NIST vectors (empty, "abc", long input), round-trip (7 tests)
- [x] Column spec: encoding/decoding, well-known specs, ordering (8 tests)
- [x] Chunk: header round-trip, checksum validation, tampered data, type encoding (10 tests)
- [x] Op columns: all op types (put, delete, insert, make_object, increment, mark), multi-actor, nested objects, mixed ops (21 tests)

---

## Phase 9: Fuzz Testing, ASan/UBSan, clang-tidy
**Status**: Complete — 274 tests passing, 3 fuzz targets, 2 new CI jobs

### Deliverables
- [x] `fuzz/fuzz_load.cpp` — libFuzzer target for `Document::load()` + save round-trip
- [x] `fuzz/fuzz_leb128.cpp` — libFuzzer target for LEB128 decode edge cases
- [x] `fuzz/fuzz_change_chunk.cpp` — libFuzzer target for columnar change chunk parsing
- [x] `fuzz/CMakeLists.txt` — libFuzzer build config with ASan+UBSan
- [x] `fuzz/generate_seeds.cpp` — helper to generate valid seed corpus files
- [x] `fuzz/corpus/` — seed corpus with 8 upstream crasher files + generated valid documents
- [x] `AUTOMERGE_CPP_BUILD_FUZZ` CMake option
- [x] ASan + UBSan CI job in linux.yml (Clang, Debug, all 274 tests)
- [x] `.clang-tidy` — project-wide static analysis configuration
- [x] clang-tidy CI job in linux.yml

### Design Decisions
- Fuzz targets require Clang (libFuzzer); CMake gracefully skips on other compilers
- Seed corpus includes 8 upstream Rust crasher files for cross-implementation coverage
- ASan+UBSan runs in Debug mode (unoptimized) for maximum sensitivity
- clang-tidy checks are conservative: bugprone-*, clang-analyzer-*, performance-*, select modernize/readability
- WarningsAsErrors empty in .clang-tidy (local dev sees warnings); CI uses --warnings-as-errors='*'

---

## Phase 10: Doxygen API Documentation
**Status**: Complete — all 12 public headers annotated

### Deliverables
- [x] `docs/Doxyfile` — Doxygen configuration (input=`include/automerge-cpp/`, output=`docs/html/`)
- [x] `/// @file` and `/// @brief` on all 12 public headers
- [x] `///` doc comments on all classes, structs, enums, methods, and fields
- [x] `@param` and `@return` annotations on Transaction and Document methods
- [x] `@code` examples in Document and Transaction class docs
- [x] `.gitignore` updated with `docs/html/`

### Headers Annotated
- `automerge.hpp` — umbrella header file docs
- `document.hpp` — Document class, all methods
- `transaction.hpp` — Transaction class, all mutation methods
- `types.hpp` — ActorId, ChangeHash, OpId, ObjId, Root, Prop
- `value.hpp` — ScalarValue, Value, ObjType, Null, Counter, Timestamp
- `op.hpp` — Op, OpType
- `change.hpp` — Change
- `sync_state.hpp` — SyncState, SyncMessage, Have
- `patch.hpp` — Patch, PatchAction, PatchPut/Insert/Delete/Increment/SpliceText
- `cursor.hpp` — Cursor
- `mark.hpp` — Mark
- `error.hpp` — Error, ErrorKind

---

## Phase 11: Performance and Parallelism (v0.4.0)
**Status**: In progress — 412 tests passing, 32 benchmarks

See [v0.4.0-performance.md](v0.4.0-performance.md) for the full plan.

### Completed
- [x] 11A.3 — Change hash cache (22x faster time travel, 5.6x faster sync)
- [x] 11A.3b — Actor table cache (1.2x faster save_large)
- [x] 11B.3 — Serialization buffer pre-sizing (1.1x faster save)
- [x] 11B.4 — SHA-256 stack-allocated padding buffer (10-20% faster hashing)
- [x] 11C.0 — Remove Taskflow dependency
- [x] 11C.1 — Thread pool (Barak Shoshany's BS::thread_pool, header-only)
- [x] 11C.2 — Thread-safe Document with `std::shared_mutex` (N readers, exclusive writers)
- [x] 11C.2b — Lock-free reads via `set_read_locking(false)` (13.5x parallel read scaling on 30 cores)
- [x] 11E — Benchmark expansion (32 benchmarks including parallel scaling, thread safety)
- [x] 11E — Example programs: `thread_safe_demo`, `parallel_perf_demo`

### Remaining
- [ ] 11A.1 — Remove dead `op_log` field
- [ ] 11A.2 — Replace `std::map` with `std::unordered_map` for objects
- [ ] 11A.4 — Fenwick tree for O(log n) visible-index-to-real (7-25x list/text)
- [ ] 11A.5 — OpId-to-position index for RGA merge
- [ ] 11B.1 — Bloom filter stack-allocated probe array
- [ ] 11B.2 — String copy elimination in Transaction
- [ ] 11B.5 — Reserve-based pre-allocation throughout
- [ ] 11B.6 — Object pool for Op and ListElement
- [ ] 11C.3 — Parallel save (change serialization + compression)
- [ ] 11C.4 — Parallel load (chunk parsing + decompression)
- [ ] 11C.5 — Parallel SHA-256 hash computation
- [ ] 11C.6 — Parallel bloom filter construction
- [ ] 11C.7 — Fork/merge parallelism for batch mutations
- [ ] 11D.1 — Hardware SHA-256 (ARM Crypto Extensions + x86 SHA-NI)
- [ ] 11D.2 — Cache-line alignment for ObjectState

---

## Phase 12: Modern C++ API and nlohmann/json Interoperability
**Status**: Complete — ~570 tests passing (412 Phase 1-11 + 41 Phase 12A + ~98 Phase 12B-12G + 19 extra)

See [nlohmann-json-interop.md](nlohmann-json-interop.md) for the full interop plan.

### Phase 12A: Modern C++ API (Complete)

Redesigned the public API to feel like modern C++ — inspired by nlohmann/json's ergonomics.

#### Deliverables
- [x] Template `get<T>()` on `Document` — returns `std::optional<T>` directly, no variant unwrapping
- [x] `get_scalar<T>()` free function — low-level extraction from `Value` and `optional<Value>`
- [x] Scalar convenience overloads on `Transaction` — `put`, `insert`, `set` accept native C++ types (`std::string`, `const char*`, `std::string_view`, `std::int64_t`, `std::uint64_t`, `double`, `bool`, `Counter`, `Null`)
- [x] Templated `transact()` — lambda return type deduced, returns value directly
- [x] `transact_with_patches()` — returns `{result, patches}` structured binding
- [x] Batch operations: `put_all`, `insert_all` with `std::initializer_list`
- [x] Container operations: `put_map` (any associative container), `insert_range` (any `std::ranges::input_range`)
- [x] `operator[]` on `Document` — root map access returning `optional<Value>`
- [x] Variadic `get_path()` — nested access with arbitrary key/index path
- [x] `overload{}` helper struct — ad-hoc variant visitors
- [x] `is_object()` free function — test if a `Value` holds `ObjType`
- [x] nlohmann/json added as build dependency (upstream submodule at `upstream/json/`)
- [x] `json_interop_demo` example — import/export, fork/merge round-trip, structured batch import

#### Design Decisions
- `get<T>()` template coexists with non-template `get()` — C++ prefers non-templates when no explicit template argument is provided
- Explicit `std::string` overloads placed before `std::string_view` overloads to resolve ambiguity (string literals and `std::string` are equally convertible to `ScalarValue` and `string_view`)
- `get_path_impl` resolves nested objects by looking up `ObjId{OpId}` from winning `MapEntry` or `ListElement` entries in `DocState`
- Batch operations use `std::initializer_list` for ergonomic inline use and templates for STL container compatibility

#### Tests (41 new)
- [x] Typed `get<T>()`: string, int64, uint64, double, bool, counter, null, missing key, wrong type, nested path (10 tests)
- [x] Scalar convenience overloads: string literal, std::string, int, double, bool, counter, null, string_view (8 tests)
- [x] Templated transact: return ObjId, return int, void lambda, return with patches (5 tests)
- [x] Batch operations: put_all, insert_all, put_map, insert_range (4 tests)
- [x] `operator[]` and `get_path`: root access, missing key, nested map, nested list, deeply nested, missing intermediate (6 tests)
- [x] `get_scalar<T>()`: from Value, from optional, wrong type, nullopt, is_object (5 tests)
- [x] `overload` helper: two-type visitor, multi-type visitor, default handler (3 tests)

#### Example Programs Updated
All 7 examples rewritten to use modern API:

| Example | Description |
|---------|-------------|
| `basic_usage` | Typed `get<T>()`, `operator[]`, `get_path()`, counters, save/load |
| `collaborative_todo` | Scalar overloads, transact with return values, fork/merge |
| `text_editor` | Text editing with patches, cursors, time travel |
| `sync_demo` | Peer-to-peer sync with SyncState |
| `thread_safe_demo` | Multi-threaded concurrent reads and writes |
| `parallel_perf_demo` | Monoid-powered fork/merge parallelism |
| `json_interop_demo` | nlohmann/json import/export, fork/merge round-trip |

### Phases 12B–12G: Deep nlohmann/json Integration (Complete)

See [phase12b-12g-json-interop.md](phase12b-12g-json-interop.md) for the full plan.

#### Deliverables
- [x] 12B — ADL serialization (`to_json`/`from_json`) for all automerge types (ScalarValue, Counter, Timestamp, ActorId, ChangeHash, OpId, ObjId, Change, Patch, Mark, Cursor)
- [x] 12C — Document import/export (`export_json`, `import_json`, `export_json_at`) with recursive nested object resolution
- [x] 12C — `Document::get_obj_id()` public method (2 overloads: key and index)
- [x] 12D — JSON Pointer (RFC 6901): `get_pointer`, `put_pointer`, `delete_pointer` with `~0`/`~1` escaping
- [x] 12E — JSON Patch (RFC 6902): `apply_json_patch` (add/remove/replace/move/copy/test), `diff_json_patch`
- [x] 12F — JSON Merge Patch (RFC 7386): `apply_merge_patch`, `generate_merge_patch`
- [x] 12F — `flatten`/`unflatten` (JSON Pointer paths → leaf values)
- [x] 12G — Documentation updates (api.md, CLAUDE.md)

#### New Files
- `include/automerge-cpp/json.hpp` — public header with all interop declarations
- `src/json.cpp` — implementations (~650 lines)
- `tests/json_test.cpp` — 98 tests
- `docs/plans/phase12b-12g-json-interop.md` — implementation plan

#### Tests (98 new)
- [x] `get_obj_id`: map child, list child, nonexistent, scalar, out-of-bounds (5 tests)
- [x] ADL serialization: all scalar types, tagged Counter/Timestamp/Bytes, identity types, compound types (22 tests)
- [x] Document export: empty, flat, nested, list, mixed, text, deep, subtree, after merge, historical (12 tests)
- [x] Document import: flat, nested, arrays, round-trips, transaction overload (10 tests)
- [x] JSON Pointer: get, put, delete, escaping, nested intermediates, list append (12 tests)
- [x] JSON Patch: all 6 ops, test assertion, diff generation, round-trip (14 tests)
- [x] JSON Merge Patch: set, add, delete, recursive, idempotent, generate (8 tests)
- [x] Flatten/unflatten: nested, list indices, escaping, round-trip, text (9 tests)
- [x] Integration: merge commutativity, save/load, combined operations (6 tests)
