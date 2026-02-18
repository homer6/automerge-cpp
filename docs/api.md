# automerge-cpp API Reference

## Namespace

All public symbols live in `namespace automerge_cpp`. Users typically alias it:

```cpp
#include <automerge-cpp/automerge.hpp>
namespace am = automerge_cpp;
```

---

## Document

The primary user-facing type. Owns the CRDT state, provides reads, and gates
mutations through transactions.

```cpp
#include <automerge-cpp/document.hpp>
```

### Construction

```cpp
auto doc = Document{};                // empty document, zero actor ID
```

`Document` is copyable (deep copy — independent state) and movable.

### Identity

```cpp
doc.actor_id()              -> const ActorId&
doc.set_actor_id(ActorId)   -> void
```

### Mutation

All mutations go through `transact()`. The document is conceptually immutable
outside of a transaction.

```cpp
doc.transact([](Transaction& tx) {
    tx.put(root, "key", std::int64_t{42});
});
```

```cpp
doc.transact(std::function<void(Transaction&)> fn) -> void
```

### Reading — Maps

```cpp
doc.get(ObjId, std::string_view key)       -> std::optional<Value>
doc.get_all(ObjId, std::string_view key)   -> std::vector<Value>   // all conflict values
```

### Reading — Lists

```cpp
doc.get(ObjId, std::size_t index)          -> std::optional<Value>
```

### Reading — Ranges

```cpp
doc.keys(ObjId)       -> std::vector<std::string>   // sorted map keys
doc.values(ObjId)     -> std::vector<Value>          // values in key/index order
doc.length(ObjId)     -> std::size_t                 // # of entries/elements
```

### Reading — Text

```cpp
doc.text(ObjId)       -> std::string                 // concatenated text content
```

### Object Queries

```cpp
doc.object_type(ObjId) -> std::optional<ObjType>     // map, list, text, table
```

---

## Transaction

Created internally by `Document::transact()`. Provides all mutation operations.

```cpp
#include <automerge-cpp/transaction.hpp>
```

### Map Operations

```cpp
tx.put(ObjId, std::string_view key, ScalarValue val)            -> void
tx.put_object(ObjId, std::string_view key, ObjType type)        -> ObjId   // returns new object
tx.delete_key(ObjId, std::string_view key)                      -> void
```

### List Operations

```cpp
tx.insert(ObjId, std::size_t index, ScalarValue val)            -> void
tx.insert_object(ObjId, std::size_t index, ObjType type)        -> ObjId
tx.set(ObjId, std::size_t index, ScalarValue val)               -> void
tx.delete_index(ObjId, std::size_t index)                       -> void
```

### Text Operations

```cpp
tx.splice_text(ObjId, std::size_t pos, std::size_t del,
               std::string_view text)                           -> void
```

### Counter Operations

```cpp
tx.increment(ObjId, std::string_view key, std::int64_t delta)   -> void
```

---

## Types

```cpp
#include <automerge-cpp/types.hpp>
```

### ActorId

16-byte unique identifier for a peer/actor.

```cpp
ActorId{}                             // all zeros
ActorId{std::array<std::byte, 16>}    // from byte array
ActorId{uint8_t[16]}                  // from raw bytes (convenience)

id.is_zero()    -> bool
id.bytes        -> std::array<std::byte, 16>
```

Supports: `==`, `<=>`, `std::hash`.

### ChangeHash

32-byte SHA-256 content hash of a change.

```cpp
ChangeHash{}                              // all zeros
ChangeHash{std::array<std::byte, 32>}
ChangeHash{uint8_t[32]}

hash.is_zero()  -> bool
hash.bytes      -> std::array<std::byte, 32>
```

Supports: `==`, `<=>`, `std::hash`.

### OpId

Identifies a single operation. Total ordering: counter first, actor tie-break.

```cpp
OpId{}                          // {0, zero actor}
OpId{uint64_t counter, ActorId}

id.counter  -> std::uint64_t
id.actor    -> ActorId
```

Supports: `==`, `<=>`, `std::hash`.

### ObjId

Identifies a CRDT object in the document tree.

```cpp
ObjId{}             // root
ObjId{OpId}         // non-root (the OpId that created the object)

id.is_root()  -> bool
id.inner      -> std::variant<Root, OpId>
```

Supports: `==`, `<=>`, `std::hash`.

### root

```cpp
inline constexpr auto root = ObjId{};   // the root Map, always exists
```

### Prop

A key into a map or index into a list.

```cpp
using Prop = std::variant<std::string, std::size_t>;

map_key("name")     -> Prop    // string alternative
list_index(3)       -> Prop    // size_t alternative
```

---

## Values

```cpp
#include <automerge-cpp/value.hpp>
```

### ObjType

```cpp
enum class ObjType : uint8_t { map, list, text, table };

to_string_view(ObjType) -> std::string_view
```

### Scalar Tag Types

```cpp
struct Null {};                                          // ==, <=>
struct Counter { std::int64_t value; };                   // ==, <=>
struct Timestamp { std::int64_t millis_since_epoch; };    // ==, <=>
using Bytes = std::vector<std::byte>;
```

### ScalarValue

Closed variant — no extension point.

```cpp
using ScalarValue = std::variant<
    Null, bool, std::int64_t, std::uint64_t, double,
    Counter, Timestamp, std::string, Bytes
>;
```

### Value

A value in the document tree: either a nested object or a scalar.

```cpp
using Value = std::variant<ObjType, ScalarValue>;

is_scalar(const Value&) -> bool
is_object(const Value&) -> bool
```

---

## Operations

```cpp
#include <automerge-cpp/op.hpp>
```

### OpType

```cpp
enum class OpType : uint8_t {
    put, del, insert, make_object, increment, splice_text, mark
};

to_string_view(OpType) -> std::string_view
```

### Op

```cpp
struct Op {
    OpId id;
    ObjId obj;
    Prop key;
    OpType action;
    Value value;
    std::vector<OpId> pred;
};
```

Supports: `==`.

---

## Change

```cpp
#include <automerge-cpp/change.hpp>
```

```cpp
struct Change {
    ActorId actor;
    std::uint64_t seq;
    std::uint64_t start_op;
    std::int64_t timestamp;
    std::optional<std::string> message;
    std::vector<ChangeHash> deps;
    std::vector<Op> operations;
};
```

Supports: `==`.

---

## Error

```cpp
#include <automerge-cpp/error.hpp>
```

### ErrorKind

```cpp
enum class ErrorKind : uint8_t {
    invalid_document, invalid_change, invalid_obj_id,
    encoding_error, decoding_error, sync_error, invalid_operation
};

to_string_view(ErrorKind) -> std::string_view
```

### Error

```cpp
struct Error {
    ErrorKind kind;
    std::string message;
};
```

Used with `std::expected<T, Error>` for fallible operations (planned for
Phase 3+).

---

## Usage Examples

### Basic Map Operations

```cpp
namespace am = automerge_cpp;

auto doc = am::Document{};
doc.transact([](auto& tx) {
    tx.put(am::root, "name", std::string{"Alice"});
    tx.put(am::root, "age", std::int64_t{30});
    tx.put(am::root, "active", true);
});

auto name = doc.get(am::root, "name");    // optional<Value>
auto keys = doc.keys(am::root);           // {"active", "age", "name"}
```

### Nested Objects

```cpp
auto doc = am::Document{};
am::ObjId list_id;

doc.transact([&](auto& tx) {
    list_id = tx.put_object(am::root, "items", am::ObjType::list);
    tx.insert(list_id, 0, std::string{"Milk"});
    tx.insert(list_id, 1, std::string{"Eggs"});
});

doc.length(list_id);              // 2
doc.get(list_id, std::size_t{0}); // "Milk"
```

### Text Editing

```cpp
auto doc = am::Document{};
am::ObjId text_id;

doc.transact([&](auto& tx) {
    text_id = tx.put_object(am::root, "content", am::ObjType::text);
    tx.splice_text(text_id, 0, 0, "Hello World");
});

doc.text(text_id);  // "Hello World"

doc.transact([&](auto& tx) {
    tx.splice_text(text_id, 5, 6, " C++23");
});

doc.text(text_id);  // "Hello C++23"
```

### Counters

```cpp
auto doc = am::Document{};
doc.transact([](auto& tx) {
    tx.put(am::root, "views", am::Counter{0});
});
doc.transact([](auto& tx) {
    tx.increment(am::root, "views", 1);
});
// Counter value is now 1
```
