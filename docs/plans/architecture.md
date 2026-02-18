# automerge-cpp Architecture Plan

## Vision

A from-scratch, modern C++23 implementation of [Automerge](https://automerge.org/) — a
conflict-free replicated data type (CRDT) library for building collaborative applications.

This is **not** a wrapper around the Rust library. It is a clean-room reimplementation
that mirrors the upstream Automerge semantics while embracing idiomatic, declarative C++
in the style of Ben Deane: algebraic types, ranges pipelines, monoid-based composition,
and APIs that make illegal states unrepresentable.

---

## Design Principles

### 1. Make Illegal States Unrepresentable

Every type precisely models its domain. A `Connected` state cannot expose a
`connection_id` that doesn't exist. A `ScalarValue` is a closed `std::variant` —
you cannot construct a value that the system doesn't understand.

```cpp
// Not this:
struct Value {
    int type_tag;       // what if it's wrong?
    int64_t int_val;    // valid only if type_tag == INT
    std::string str_val; // valid only if type_tag == STR
};

// This:
using ScalarValue = std::variant<
    Null, bool, std::int64_t, std::uint64_t, double,
    Counter, Timestamp, std::string, Bytes
>;
```

### 2. Algorithms Over Raw Loops

No raw `for` or `while` loops in library code. Every traversal is expressed through
`std::ranges` pipelines, standard algorithms, or fold/reduce operations. This makes
intent explicit and enables future parallelism.

```cpp
// Collect all keys from a map object
auto keys(ObjId obj) const -> std::generator<std::string_view>;

// Compose with ranges
auto uppercase_keys = doc.keys(root)
    | std::views::transform(to_upper)
    | std::ranges::to<std::vector>();
```

### 3. CRDTs Are Monoids

A CRDT's merge operation is associative and has an identity (the empty document).
This is a monoid. We model it explicitly:

```cpp
// merge is associative: merge(merge(a, b), c) == merge(a, merge(b, c))
// identity: merge(a, Document{}) == a
auto merged = std::reduce(docs.begin(), docs.end(), Document{},
    [](Document a, const Document& b) { a.merge(b); return a; });
```

### 4. Strong Types Prevent Mixups

Raw integers and strings are never used as identifiers. Every ID is a distinct,
non-interchangeable type:

```cpp
struct ActorId  { /* ... */ };
struct ObjId    { /* ... */ };
struct ChangeHash { /* ... */ };
struct OpId     { /* ... */ };

// These do not interconvert. Passing an ActorId where an ObjId is expected
// is a compile error — not a runtime surprise.
```

### 5. Errors in the Type System

No exceptions for expected failure paths. Use `std::expected` so callers must
handle errors:

```cpp
auto load(std::span<const std::byte> data) -> std::expected<Document, Error>;

// Caller cannot ignore the error:
auto doc = Document::load(bytes);
if (!doc) { log(doc.error()); return; }
doc->put(root, "key", 42);
```

### 6. Value Semantics and Immutability

Prefer value types. Use `const` aggressively. Mutations go through explicit
transaction boundaries — outside a transaction, a document is immutable:

```cpp
const auto doc = Document{};

// Mutation requires an explicit transaction
doc.transact([](auto& tx) {
    tx.put(root, "name", "Alice");
    tx.insert(list, 0, 42);
});
// doc is conceptually const again after transact returns
```

### 7. Composable Small Pieces

The library is built from small, independent, testable components that compose:
- `OpSet` stores operations (knows nothing about networking)
- `SyncState` manages peer synchronization (knows nothing about storage)
- `Document` composes them into a user-facing API
- Serialization is a separate concern from the document model

---

## Core Types

### Identifiers

| Type | Description | Representation |
|------|-------------|----------------|
| `ActorId` | Unique identifier for a peer/actor | 16 bytes (UUID-sized) |
| `ObjId` | Identifies a nested object (map, list, text) | `(ActorId, counter)` or `Root` |
| `OpId` | Identifies a single operation | `(counter, ActorId)` |
| `ChangeHash` | Content hash of a change | SHA-256 (32 bytes) |
| `Prop` | Key into a map or index into a list | `std::variant<std::string, std::size_t>` |

### Values

```cpp
namespace automerge_cpp {

// Tag types for special scalars
struct Null {};
struct Counter { std::int64_t value; };
struct Timestamp { std::int64_t millis_since_epoch; };
using Bytes = std::vector<std::byte>;

// A scalar value — closed set, no extension
using ScalarValue = std::variant<
    Null,
    bool,
    std::int64_t,
    std::uint64_t,
    double,
    Counter,
    Timestamp,
    std::string,
    Bytes
>;

// Object types — the CRDT containers
enum class ObjType : std::uint8_t {
    map,
    list,
    text,
    table
};

// A value is either a nested object or a scalar
using Value = std::variant<ObjType, ScalarValue>;

}  // namespace automerge_cpp
```

### Operations

```cpp
// What kind of mutation occurred
enum class OpType : std::uint8_t {
    put,           // set a value at a key/index
    del,           // delete a key/index
    insert,        // insert into a sequence
    make_object,   // create a nested object
    increment,     // increment a counter
    splice_text,   // splice text content
    mark,          // apply a rich-text mark
};

// A single operation in the CRDT log
struct Op {
    OpId id;
    ObjId obj;
    Prop key;
    OpType action;
    Value value;
    std::vector<OpId> pred;  // predecessor ops (for conflict tracking)
};
```

### Change

```cpp
struct Change {
    ActorId actor;
    std::uint64_t seq;
    std::uint64_t start_op;
    std::int64_t timestamp;
    std::optional<std::string> message;
    std::vector<ChangeHash> deps;
    std::vector<Op> operations;

    auto hash() const -> ChangeHash;
};
```

### Document

```cpp
class Document {
public:
    // Construction
    Document();
    static auto load(std::span<const std::byte> data) -> std::expected<Document, Error>;

    // Persistence
    auto save() const -> std::vector<std::byte>;

    // Identity
    auto actor_id() const -> const ActorId&;
    auto set_actor_id(ActorId id) -> void;

    // Reading (always available)
    auto get(ObjId obj, Prop key) const -> std::optional<Value>;
    auto get_all(ObjId obj, Prop key) const -> std::vector<Value>;  // conflicts
    auto keys(ObjId obj) const -> /* range */;
    auto values(ObjId obj) const -> /* range */;
    auto length(ObjId obj) const -> std::size_t;
    auto text(ObjId obj) const -> std::string;
    auto heads() const -> std::vector<ChangeHash>;

    // Time travel (read at a historical point)
    auto get_at(ObjId obj, Prop key, std::span<const ChangeHash> heads) const
        -> std::optional<Value>;

    // Mutation (via transactions)
    template <std::invocable<Transaction&> F>
    auto transact(F&& fn) -> std::expected<ChangeHash, Error>;

    // Merging (the monoid operation)
    auto merge(const Document& other) -> std::expected<void, Error>;
    auto fork() const -> Document;

    // Changes
    auto get_changes() const -> std::vector<Change>;
    auto apply_changes(std::span<const Change> changes) -> std::expected<void, Error>;
};
```

### Transaction

```cpp
class Transaction {
public:
    // Map operations
    auto put(ObjId obj, std::string_view key, ScalarValue val) -> void;
    auto put_object(ObjId obj, std::string_view key, ObjType type) -> ObjId;
    auto delete_key(ObjId obj, std::string_view key) -> void;

    // List operations
    auto insert(ObjId obj, std::size_t index, ScalarValue val) -> void;
    auto insert_object(ObjId obj, std::size_t index, ObjType type) -> ObjId;
    auto set(ObjId obj, std::size_t index, ScalarValue val) -> void;
    auto delete_index(ObjId obj, std::size_t index) -> void;

    // Text operations
    auto splice_text(ObjId obj, std::size_t pos, std::size_t del,
                     std::string_view text) -> void;

    // Counter operations
    auto increment(ObjId obj, Prop key, std::int64_t delta) -> void;

    // Rich text marks
    auto mark(ObjId obj, std::size_t start, std::size_t end,
              std::string_view name, ScalarValue value) -> void;

    // Commit
    auto commit(std::optional<std::string_view> message = std::nullopt) -> ChangeHash;
};
```

### Sync Protocol

```cpp
class SyncState {
public:
    SyncState();

    auto generate_message(const Document& doc) -> std::optional<std::vector<std::byte>>;
    auto receive_message(Document& doc, std::span<const std::byte> msg)
        -> std::expected<void, Error>;
};
```

---

## Module Decomposition

```
automerge-cpp/
  include/automerge-cpp/
    automerge.hpp          # umbrella header
    document.hpp           # Document class
    transaction.hpp        # Transaction class
    types.hpp              # ActorId, ObjId, OpId, ChangeHash, Prop
    value.hpp              # ScalarValue, Value, ObjType
    change.hpp             # Change struct
    op.hpp                 # Op, OpType
    sync.hpp               # SyncState, sync protocol
    error.hpp              # Error types (std::expected compatible)
    patch.hpp              # Patch, PatchLog (incremental updates)
    cursor.hpp             # Cursor (stable position tracking)
    marks.hpp              # Rich text marks
  src/
    document.cpp
    transaction.cpp
    op_set.hpp / op_set.cpp    # internal: columnar operation storage
    change_graph.hpp / .cpp    # internal: DAG of changes
    clock.hpp / .cpp           # internal: vector clocks
    storage/
      save.cpp                 # binary serialization
      load.cpp                 # binary deserialization
      columnar.hpp / .cpp      # columnar encoding/decoding
      compression.hpp / .cpp   # deflate compression
    sync/
      state.cpp                # sync state machine
      bloom_filter.hpp / .cpp  # bloom filter for sync
      message.hpp / .cpp       # sync message encoding
    hash.hpp / .cpp            # SHA-256 (thin wrapper over a vendored/system lib)
    encoding/
      leb128.hpp               # variable-length integer encoding
      hex.hpp                  # hex encoding/decoding
```

---

## Data Model

### Document Structure

A document is a tree of CRDT objects:

```
ROOT (Map)
 +-- "title" -> "My Doc" (Scalar)
 +-- "tags" -> List
 |    +-- [0] -> "crdt" (Scalar)
 |    +-- [1] -> "cpp" (Scalar)
 +-- "content" -> Text
 |    +-- "Hello, world!"
 +-- "metadata" -> Map
      +-- "created" -> Timestamp(1708000000000)
      +-- "views" -> Counter(42)
```

- The root is always a `Map` accessed via the constant `ObjId::root()`.
- Nested objects are created via `put_object` / `insert_object` and accessed by `ObjId`.
- Text is a specialized sequence optimized for character-level editing.

### Operations and Causality

Every mutation produces an `Op` with a unique `OpId`. Operations reference their
causal predecessors (`pred` field), forming a DAG. The merge function replays
operations from both documents, resolving conflicts deterministically:

- **Maps**: last-writer-wins (deterministic tie-breaking by OpId)
- **Lists/Text**: concurrent inserts at the same position are ordered by OpId
- **Counters**: increments merge by addition (a true CRDT counter)

### Change Graph

Changes form a DAG (directed acyclic graph). Each change records its `deps` —
the hashes of changes it has "seen". Multiple heads indicate divergence; merging
collapses heads.

```
   A
  / \
 B   C    <- two concurrent changes (two heads)
  \ /
   D      <- merge (single head again)
```

---

## Binary Format

The library must read and write the Automerge binary format for interoperability
with the Rust, JavaScript, and other implementations.

### Format Overview

| Section | Description |
|---------|-------------|
| Magic bytes | `0x85 0x6f 0x4a 0x83` |
| Header | Format version, actor count, change count |
| Actors | Table of actor IDs |
| Changes | Columnar-encoded operations |
| Compressed chunks | DEFLATE-compressed data sections |

### Columnar Encoding

Operations are stored column-wise (like a columnar database) for compression
efficiency. Each property of an op (object, key, action, value, etc.) is stored
in a separate column using LEB128 variable-length encoding and run-length encoding.

---

## Sync Protocol

Based on the paper: *Efficient Causal Ordering for Peer-to-Peer Networks*

### Flow

```
Peer A                          Peer B
  |                               |
  |-- generate_message() -------->|
  |                               |-- receive_message()
  |<-------- generate_message() --|
  |-- receive_message()           |
  |                               |
  |   (repeat until both return None)
```

### Components

1. **Bloom filter**: efficiently encodes "what changes I have"
2. **Have messages**: ranges of changes by actor
3. **Need messages**: requested change hashes
4. **Change payloads**: actual change data

---

## Dependencies

### Required (vendored or fetched via CMake)

| Library | Purpose | Notes |
|---------|---------|-------|
| SHA-256 | Change hashing | Small vendored implementation or system OpenSSL |
| DEFLATE | Compression | zlib / libdeflate |

### Optional

| Library | Purpose | Notes |
|---------|---------|-------|
| Google Test | Unit tests | Fetched via CMake FetchContent |
| Google Benchmark | Benchmarks | Fetched via CMake FetchContent |

### C++23 Standard Library Features Used

- `std::expected` — error handling
- `std::variant` / `std::visit` — algebraic types
- `std::optional` — nullable values
- `std::span` — non-owning views over contiguous data
- `std::ranges` / `std::views` — declarative pipelines
- `std::generator` (C++23) — lazy coroutine-based iteration (if available)
- `std::format` — string formatting
- Concepts and constraints — API boundaries

---

## Ranges-First API Design

The read-side of the document exposes ranges that compose with the standard library:

```cpp
#include <automerge-cpp/automerge.hpp>
namespace am = automerge_cpp;

auto doc = am::Document{};
// ... populate doc ...

// All keys in the root map
for (auto key : doc.keys(am::root)) {
    std::println("key: {}", key);
}

// Filter + transform via ranges
auto numeric_values = doc.values(am::root)
    | std::views::filter([](const am::Value& v) {
        return std::holds_alternative<std::int64_t>(
            std::get<am::ScalarValue>(v));
      })
    | std::views::transform([](const am::Value& v) {
        return std::get<std::int64_t>(std::get<am::ScalarValue>(v));
      });

// Reduce (fold) — monoidal composition
auto sum = std::ranges::fold_left(numeric_values, 0LL, std::plus<>{});
```

---

## Error Handling

```cpp
enum class ErrorKind : std::uint8_t {
    invalid_document,
    invalid_change,
    invalid_obj_id,
    encoding_error,
    decoding_error,
    sync_error,
    invalid_operation,
};

struct Error {
    ErrorKind kind;
    std::string message;
};

// All fallible operations return std::expected
auto result = Document::load(data);
if (!result) {
    // result.error() is an Error
}
```

---

## Thread Safety

- A `Document` is **not** thread-safe for concurrent mutation.
- A `const Document&` is safe to read from multiple threads.
- `SyncState` is per-peer and should not be shared across threads.
- Users who need concurrent access should use external synchronization or
  the fork-and-merge pattern (each thread works on a fork, then merges).

---

## Testing Strategy

1. **Unit tests**: every type, every algorithm, every edge case
2. **Property-based tests**: merge commutativity, associativity, idempotency
3. **Interop tests**: load documents saved by the Rust implementation,
   save documents and verify the Rust implementation can load them
4. **Fuzz tests**: feed random bytes to `Document::load()` and sync protocol
5. **Benchmark tests**: measure throughput of operations, merges, sync

---

## Benchmark Targets

| Benchmark | What it measures |
|-----------|-----------------|
| `bm_create_document` | Document construction overhead |
| `bm_put_map` | Map put throughput (single key, many keys) |
| `bm_insert_list` | List insertion (head, tail, random) |
| `bm_splice_text` | Text editing (insert, delete, replace) |
| `bm_merge` | Merge two documents with N changes each |
| `bm_save` | Serialization throughput (document size vs time) |
| `bm_load` | Deserialization throughput |
| `bm_sync` | Full sync round-trip between two peers |
| `bm_edit_trace` | Replay a real-world editing trace |
| `bm_get` | Read throughput (map get, list get) |
| `bm_iterate` | Iteration over keys/values/entries |
