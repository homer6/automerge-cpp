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
auto doc = Document{};                                       // default (no pool for max simplicity)
auto doc = Document{8u};                                     // explicit 8-thread pool
auto doc = Document{1u};                                     // single-threaded, no pool, zero overhead
auto doc = Document{pool};                                   // shared pool (std::shared_ptr<thread_pool>)
```

`Document` is copyable (deep copy — independent state) and movable.
`fork()` shares the parent's thread pool.

### Identity

```cpp
doc.actor_id()              -> const ActorId&
doc.set_actor_id(ActorId)   -> void
```

### Mutation

All mutations go through `transact()`. The document is conceptually immutable
outside of a transaction.

```cpp
// Void transaction (std::function overload)
doc.transact([](Transaction& tx) {
    tx.put(root, "key", 42);
});

// Void transaction (generic lambda — template overload)
doc.transact([](auto& tx) {
    tx.put(root, "key", 42);
});

// Return-value transaction — lambda return type is deduced
auto list_id = doc.transact([](Transaction& tx) {
    return tx.put_object(root, "items", ObjType::list);
});

// Return-value transaction with patches
auto [obj_id, patches] = doc.transact_with_patches([](Transaction& tx) {
    return tx.put_object(root, "data", ObjType::map);
});

// Void transaction with patches
auto patches = doc.transact_with_patches([](auto& tx) {
    tx.put(root, "x", 42);
});
```

```cpp
// Signatures
void transact(const std::function<void(Transaction&)>& fn);

template <typename Fn> auto transact(Fn&& fn) -> std::invoke_result_t<Fn, Transaction&>;
template <typename Fn> void transact(Fn&& fn);  // void-returning template overload

auto transact_with_patches(const std::function<void(Transaction&)>& fn) -> std::vector<Patch>;
template <typename Fn> auto transact_with_patches(Fn&& fn)
    -> std::pair<std::invoke_result_t<Fn, Transaction&>, std::vector<Patch>>;
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

### Typed get\<T\>()

Like [nlohmann/json's `get<T>()`](https://json.nlohmann.me/api/basic_json/get/),
returns `optional<T>` directly with no variant unwrapping:

```cpp
auto name  = doc.get<std::string>(root, "name");       // optional<string>
auto age   = doc.get<std::int64_t>(root, "age");        // optional<int64_t>
auto score = doc.get<double>(root, "score");             // optional<double>
auto ok    = doc.get<bool>(root, "active");              // optional<bool>
auto hits  = doc.get<Counter>(root, "hits");             // optional<Counter>
auto ts    = doc.get<Timestamp>(root, "created");        // optional<Timestamp>

// From lists
auto item  = doc.get<std::string>(list_id, std::size_t{0});
```

Returns `nullopt` if the key/index doesn't exist **or** if the value is a different type.

```cpp
// Signatures
template <typename T> auto get(const ObjId&, std::string_view) const -> std::optional<T>;
template <typename T> auto get(const ObjId&, std::size_t) const -> std::optional<T>;
```

### operator[]

Quick root map access:

```cpp
auto val = doc["key"];   // equivalent to doc.get(root, "key")
```

```cpp
auto operator[](std::string_view key) const -> std::optional<Value>;
```

### get_path()

Variadic nested access — each argument is a string (map key) or `size_t` (list index):

```cpp
auto port  = doc.get_path("config", "database", "port");
auto title = doc.get_path("todos", std::size_t{0}, "title");
auto deep  = doc.get_path("a", "b", "c", "d");
```

Returns `nullopt` if any intermediate path element doesn't exist or isn't an object.

```cpp
template <typename... Props> requires (sizeof...(Props) > 0)
auto get_path(Props&&... props) const -> std::optional<Value>;
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

### Fork and Merge

```cpp
doc.fork()                                  -> Document              // deep copy with unique actor
doc.merge(const Document&)                  -> void                  // apply unseen changes
doc.get_changes()                           -> std::vector<Change>   // full change history
doc.apply_changes(std::vector<Change>)      -> void                  // apply remote changes
doc.get_heads()                             -> std::vector<ChangeHash>  // current DAG leaves
```

### Binary Serialization

```cpp
doc.save()                                  -> std::vector<std::byte>
Document::load(std::span<const std::byte>)  -> std::optional<Document>
```

### Sync Protocol

```cpp
doc.generate_sync_message(SyncState&)       -> std::optional<SyncMessage>
doc.receive_sync_message(SyncState&, const SyncMessage&) -> void
```

### Historical Reads — Time Travel

```cpp
doc.get_at(ObjId, std::string_view key, const std::vector<ChangeHash>& heads) -> std::optional<Value>
doc.get_at(ObjId, std::size_t index, const std::vector<ChangeHash>& heads)    -> std::optional<Value>
doc.keys_at(ObjId, heads)    -> std::vector<std::string>
doc.values_at(ObjId, heads)  -> std::vector<Value>
doc.length_at(ObjId, heads)  -> std::size_t
doc.text_at(ObjId, heads)    -> std::string
```

### Cursors — Stable Positions

```cpp
doc.cursor(ObjId, std::size_t index)           -> std::optional<Cursor>
doc.resolve_cursor(ObjId, const Cursor&)       -> std::optional<std::size_t>
```

### Rich Text Marks

```cpp
doc.marks(ObjId)                               -> std::vector<Mark>
doc.marks_at(ObjId, const std::vector<ChangeHash>&) -> std::vector<Mark>
```

### Thread Pool

```cpp
doc.get_thread_pool()  -> std::shared_ptr<thread_pool>   // may be nullptr
```

### Locking Control

```cpp
doc.set_read_locking(bool enabled) -> void    // default: true
doc.read_locking()                 -> bool
```

```cpp
doc.set_read_locking(false);  // caller guarantees no concurrent writers
// ... parallel reads ...
doc.set_read_locking(true);   // re-enable before writes
```

---

## Transaction

Created internally by `Document::transact()`. Provides all mutation operations.

```cpp
#include <automerge-cpp/transaction.hpp>
```

### Map Operations

```cpp
tx.put(ObjId, std::string_view key, ScalarValue val)  -> void
tx.put(ObjId, std::string_view key, ObjType type)     -> ObjId   // create empty object
tx.put(ObjId, std::string_view key, List{...})         -> ObjId   // create populated list
tx.put(ObjId, std::string_view key, Map{...})          -> ObjId   // create populated map
tx.put_object(ObjId, std::string_view key, ObjType)    -> ObjId   // alias for put(ObjType)
tx.delete_key(ObjId, std::string_view key)             -> void
```

### Scalar Convenience Overloads (Map)

`put()` accepts native C++ types — no need to wrap in `ScalarValue{}`:

```cpp
tx.put(root, "name", "Alice");                     // const char*
tx.put(root, "name", std::string{"Alice"});        // std::string
tx.put(root, "name", std::string_view{"Alice"});   // string_view
tx.put(root, "age", 30);                           // int → int64_t
tx.put(root, "age", std::int64_t{30});             // int64_t
tx.put(root, "big", std::uint64_t{999});           // uint64_t
tx.put(root, "pi", 3.14);                          // double
tx.put(root, "ok", true);                          // bool
tx.put(root, "empty", Null{});                     // Null
tx.put(root, "views", Counter{0});                 // Counter
tx.put(root, "created", Timestamp{1700000000000}); // Timestamp
```

### List Operations

```cpp
tx.insert(ObjId, std::size_t index, ScalarValue val)   -> void
tx.insert(ObjId, std::size_t index, ObjType type)      -> ObjId   // create empty object
tx.insert(ObjId, std::size_t index, List{...})          -> ObjId   // create populated list
tx.insert(ObjId, std::size_t index, Map{...})           -> ObjId   // create populated map
tx.insert_object(ObjId, std::size_t index, ObjType)     -> ObjId   // alias for insert(ObjType)
tx.set(ObjId, std::size_t index, ScalarValue val)       -> void
tx.delete_index(ObjId, std::size_t index)               -> void
```

### Scalar Convenience Overloads (List)

`insert()` and `set()` also accept native types:

```cpp
tx.insert(list, 0, "hello");                // const char*
tx.insert(list, 0, std::string{"hello"});   // std::string
tx.insert(list, 0, 42);                     // int → int64_t
tx.insert(list, 0, 3.14);                   // double
tx.insert(list, 0, true);                   // bool

tx.set(list, 0, "updated");                 // same overloads for set()
```

### Initializer Lists

Create and populate nested objects in a single call — like nlohmann/json.
Bare initializer lists auto-detect: flat values `{a, b, c}` create a list,
key-value pairs `{{"k", v}, ...}` create a map.

#### Signatures

```cpp
// Bare initializer lists (auto-detect list vs map)
tx.put(ObjId, key, {values...})            -> ObjId   // creates list
tx.put(ObjId, key, {{"k1",v1}, {"k2",v2}}) -> ObjId   // creates map
tx.insert(ObjId, idx, {values...})          -> ObjId   // creates list in list
tx.insert(ObjId, idx, {{"k1",v1}, ...})     -> ObjId   // creates map in list

// Explicit wrappers (List{}, Map{})
tx.put(ObjId, key, List{values...})         -> ObjId
tx.put(ObjId, key, Map{{"k1",v1}, ...})     -> ObjId
tx.insert(ObjId, idx, List{values...})      -> ObjId
tx.insert(ObjId, idx, Map{{"k1",v1}, ...})  -> ObjId
```

#### Examples

```cpp
doc.transact([](auto& tx) {
    // Bare initializer lists — auto-detects list vs map
    auto items = tx.put(root, "items", {"Milk", "Eggs", "Bread"});
    auto config = tx.put(root, "config", {{"port", 8080}, {"host", "localhost"}});

    // Explicit wrappers work too
    auto tags = tx.put(root, "tags", List{"crdt", "cpp"});
    auto meta = tx.put(root, "meta", Map{{"version", "1.0"}});

    // Mixed types — int, string, double, bool
    auto mixed = tx.put(root, "data", List{1, "hello", 3.14, true});

    // Empty containers
    auto empty_list = tx.put(root, "log", List{});
    auto empty_map = tx.put(root, "meta", Map{});

    // Insert populated objects into lists
    auto records = tx.put(root, "users", ObjType::list);
    tx.insert(records, 0, {{"name", "Alice"}, {"role", "admin"}});
    tx.insert(records, 1, {{"name", "Bob"}, {"role", "editor"}});
});
```

### STL Containers

`put` and `insert` accept any STL container directly — ranges create lists,
associative containers create maps. Uses `std::ranges::for_each` and
`std::views::enumerate` internally.

#### Signatures

```cpp
// Any range (vector, set, deque, etc.) → creates list
template <std::ranges::input_range R>
tx.put(ObjId, key, R&& range)              -> ObjId
tx.insert(ObjId, idx, R&& range)           -> ObjId

// Any associative container (map, unordered_map) → creates map
template <typename AssocContainer>  // requires mapped_type
tx.put(ObjId, key, const AssocContainer&)   -> ObjId
tx.insert(ObjId, idx, const AssocContainer&) -> ObjId
```

#### Examples

```cpp
// std::vector → creates a list
auto tags = std::vector<std::string>{"crdt", "cpp", "collaborative"};
auto list = tx.put(root, "tags", tags);

// std::set → creates a list (sorted order preserved)
auto unique = std::set<std::string>{"alpha", "beta", "gamma"};
tx.put(root, "sorted", unique);

// std::map → creates a map
auto dims = std::map<std::string, ScalarValue>{
    {"w", ScalarValue{std::int64_t{800}}},
    {"h", ScalarValue{std::int64_t{600}}},
};
tx.put(root, "dims", dims);

// Insert containers into lists
auto records = tx.put(root, "records", ObjType::list);
auto row = std::map<std::string, ScalarValue>{
    {"name", ScalarValue{std::string{"Alice"}}},
    {"score", ScalarValue{std::int64_t{100}}},
};
tx.insert(records, 0, row);
```

### Batch Operations (existing objects)

These operate on already-created objects (when you already have an ObjId):

```cpp
// Batch insert from initializer list
tx.insert_all(ObjId, std::size_t start, std::initializer_list<ScalarValue> values);

// Batch put key-value pairs from initializer list
tx.put_all(ObjId, std::initializer_list<std::pair<std::string_view, ScalarValue>> entries);

// Populate existing map from any associative container
template <typename Map>
tx.put_map(ObjId, const Map& map);

// Insert into existing list from any range
template <std::ranges::input_range R>
tx.insert_range(ObjId, std::size_t start, R&& range);
```

#### Batch Examples

```cpp
// Batch put into an existing map
tx.put_all(root, {
    {"name", ScalarValue{std::string{"Alice"}}},
    {"age",  ScalarValue{std::int64_t{30}}},
});

// Batch insert into an existing list
tx.insert_all(list, 0, {
    ScalarValue{std::int64_t{1}},
    ScalarValue{std::int64_t{2}},
    ScalarValue{std::int64_t{3}},
});

// From std::map into an existing map
auto data = std::map<std::string, ScalarValue>{
    {"x", ScalarValue{std::int64_t{10}}},
    {"y", ScalarValue{std::int64_t{20}}},
};
tx.put_map(root, data);

// From std::vector into an existing list
auto vals = std::vector<ScalarValue>{
    ScalarValue{std::int64_t{100}},
    ScalarValue{std::int64_t{200}},
};
tx.insert_range(list, 0, vals);
```

### Text Operations

```cpp
tx.splice_text(ObjId, std::size_t pos, std::size_t del,
               std::string_view text) -> void
```

### Counter Operations

```cpp
tx.increment(ObjId, std::string_view key, std::int64_t delta) -> void
```

### Mark Operations (Rich Text)

```cpp
tx.mark(ObjId, std::size_t start, std::size_t end,
        std::string_view name, ScalarValue value) -> void
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

### List / Map Initializer Types

Wrapper types for creating populated nested objects via `put()` and `insert()`:

```cpp
struct List {
    std::vector<ScalarValue> values;
    List() = default;
    List(std::initializer_list<ScalarValue> v);
};

struct Map {
    std::vector<std::pair<std::string, ScalarValue>> entries;
    Map() = default;
    Map(std::initializer_list<std::pair<std::string_view, ScalarValue>> e);
};
```

Native types convert implicitly: `"hello"` -> string, `42` -> int64, `3.14` -> double, `true` -> bool.

### overload Helper

Ad-hoc variant visitor from lambdas:

```cpp
template <typename... Ts>
struct overload : Ts... { using Ts::operator()...; };

// Usage
std::visit(overload{
    [](std::string s)  { printf("%s\n", s.c_str()); },
    [](std::int64_t i) { printf("%ld\n", i); },
    [](auto&&)         { printf("other\n"); },
}, some_variant);
```

### get_scalar\<T\>()

Extract a typed scalar from a `Value` or `optional<Value>`:

```cpp
template <typename T>
auto get_scalar(const Value& v) -> std::optional<T>;

template <typename T>
auto get_scalar(const std::optional<Value>& v) -> std::optional<T>;
```

```cpp
auto val = doc.get(root, "name");
auto name = get_scalar<std::string>(val);   // optional<string>
auto num  = get_scalar<std::int64_t>(val);  // optional<int64_t> — nullopt if wrong type
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
ActorId{uint8_t[16]}                  // from raw bytes

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

map_key("name")     -> Prop
list_index(3)       -> Prop
```

---

## SyncState

Per-peer synchronization state machine. Create one `SyncState` per peer.

```cpp
#include <automerge-cpp/sync_state.hpp>
```

```cpp
auto state = SyncState{};

state.shared_heads()       -> const std::vector<ChangeHash>&
state.last_sent_heads()    -> const std::vector<ChangeHash>&

state.encode()             -> std::vector<std::byte>
SyncState::decode(span)    -> std::optional<SyncState>
```

### SyncMessage

```cpp
struct SyncMessage {
    std::vector<ChangeHash> heads;
    std::vector<ChangeHash> need;
    std::vector<Have> have;
    std::vector<Change> changes;
};
```

### Have

```cpp
struct Have {
    std::vector<ChangeHash> last_sync;
    std::vector<std::byte> bloom_bytes;
};
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

---

## Patch

```cpp
#include <automerge-cpp/patch.hpp>
```

```cpp
struct PatchPut { Value value; bool conflict; };
struct PatchInsert { std::size_t index; Value value; };
struct PatchDelete { std::size_t index; std::size_t count; };
struct PatchIncrement { std::int64_t delta; };
struct PatchSpliceText { std::size_t index; std::size_t delete_count; std::string text; };

using PatchAction = std::variant<PatchPut, PatchInsert, PatchDelete, PatchIncrement, PatchSpliceText>;

struct Patch {
    ObjId obj;
    Prop key;
    PatchAction action;
};
```

---

## Cursor

```cpp
#include <automerge-cpp/cursor.hpp>
```

```cpp
struct Cursor {
    OpId position;  // the OpId of the element this cursor points to
};
```

Supports: `==`, `<=>`.

---

## Mark

```cpp
#include <automerge-cpp/mark.hpp>
```

```cpp
struct Mark {
    std::size_t start;      // start index (inclusive)
    std::size_t end;        // end index (exclusive)
    std::string name;       // e.g. "bold", "italic", "link"
    ScalarValue value;      // e.g. true, "https://..."
};
```

Marks are anchored to element OpIds internally — indices are resolved at read time
and survive insertions, deletions, and merges.

Supports: `==`.

---

## Error

```cpp
#include <automerge-cpp/error.hpp>
```

```cpp
enum class ErrorKind : uint8_t {
    invalid_document, invalid_change, invalid_obj_id,
    encoding_error, decoding_error, sync_error, invalid_operation
};

to_string_view(ErrorKind) -> std::string_view

struct Error {
    ErrorKind kind;
    std::string message;
};
```

---

## thread_pool

Barak Shoshany's BS::thread_pool — a header-only work-stealing thread pool.

```cpp
#include <automerge-cpp/thread_pool.hpp>
```

```cpp
auto pool = std::make_shared<am::thread_pool>(std::thread::hardware_concurrency());
pool->sleep_duration = 0;  // yield instead of 500us sleep

pool->get_thread_count()  -> uint32_t

pool->parallelize_loop(first, last, [](auto start, auto end) {
    for (auto i = start; i < end; ++i) { /* work */ }
});

auto future = pool->submit([]() { return 42; });
auto result = future.get();  // 42
```

---

## Usage Examples

### Basic Map Operations

```cpp
namespace am = automerge_cpp;

auto doc = am::Document{};
doc.transact([](auto& tx) {
    tx.put(am::root, "name", "Alice");
    tx.put(am::root, "age", 30);
    tx.put(am::root, "active", true);
});

auto name = doc.get<std::string>(am::root, "name");   // optional<string>{"Alice"}
auto age  = doc.get<std::int64_t>(am::root, "age");    // optional<int64_t>{30}
auto keys = doc.keys(am::root);                         // {"active", "age", "name"}
```

### Nested Objects

```cpp
auto doc = am::Document{};
auto list_id = doc.transact([](am::Transaction& tx) {
    auto list = tx.put_object(am::root, "items", am::ObjType::list);
    tx.insert(list, 0, "Milk");
    tx.insert(list, 1, "Eggs");
    return list;
});

doc.length(list_id);                            // 2
doc.get<std::string>(list_id, std::size_t{0});  // "Milk"

// Or use get_path
doc.get_path("items", std::size_t{0});          // Value{"Milk"}
```

### Text Editing

```cpp
auto doc = am::Document{};
auto text_id = doc.transact([](am::Transaction& tx) {
    auto t = tx.put_object(am::root, "content", am::ObjType::text);
    tx.splice_text(t, 0, 0, "Hello World");
    return t;
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

auto views = doc.get<am::Counter>(am::root, "views");
// views->value == 1
```

### Fork and Merge

```cpp
auto doc1 = am::Document{};
doc1.transact([](auto& tx) { tx.put(am::root, "x", 1); });

auto doc2 = doc1.fork();

doc1.transact([](auto& tx) { tx.put(am::root, "a", "from doc1"); });
doc2.transact([](auto& tx) { tx.put(am::root, "b", "from doc2"); });

doc1.merge(doc2);
// doc1 has keys: "a", "b", "x"
```

### Save and Load

```cpp
auto bytes = doc.save();
if (auto loaded = am::Document::load(bytes)) {
    auto val = loaded->get<std::string>(am::root, "key");
}
```

### Sync Protocol

```cpp
auto state_a = am::SyncState{};
auto state_b = am::SyncState{};

for (int i = 0; i < 10; ++i) {
    bool progress = false;
    if (auto msg = doc_a.generate_sync_message(state_a)) {
        doc_b.receive_sync_message(state_b, *msg);
        progress = true;
    }
    if (auto msg = doc_b.generate_sync_message(state_b)) {
        doc_a.receive_sync_message(state_a, *msg);
        progress = true;
    }
    if (!progress) break;
}
```

### Patches

```cpp
auto patches = doc.transact_with_patches([&](auto& tx) {
    tx.put(am::root, "name", "Alice");
    tx.splice_text(text_id, 5, 6, " C++23");
});

for (const auto& patch : patches) {
    std::visit(am::overload{
        [](const am::PatchPut& p) { /* p.value, p.conflict */ },
        [](const am::PatchSpliceText& s) { /* s.index, s.text */ },
        [](auto&&) { /* other patch types */ },
    }, patch.action);
}
```

### Time Travel

```cpp
doc.transact([](auto& tx) { tx.put(am::root, "x", 1); });
auto v1 = doc.get_heads();

doc.transact([](auto& tx) { tx.put(am::root, "x", 2); });

doc.get_at(am::root, "x", v1);     // 1 (past)
doc.get(am::root, "x");            // 2 (current)
```

### Cursors

```cpp
auto cur = doc.cursor(list_id, 1);

doc.transact([&](auto& tx) {
    tx.insert(list_id, 0, "new");  // insert before cursor
});

auto idx = doc.resolve_cursor(list_id, *cur);
// idx == 2 (cursor tracked the element, not the index)
```

### Rich Text Marks

```cpp
auto text_id = doc.transact([](am::Transaction& tx) {
    auto t = tx.put_object(am::root, "content", am::ObjType::text);
    tx.splice_text(t, 0, 0, "Hello World");
    return t;
});

doc.transact([&](auto& tx) {
    tx.mark(text_id, 0, 5, "bold", true);
    tx.mark(text_id, 6, 11, "link", std::string{"https://example.com"});
});

auto marks = doc.marks(text_id);
// marks[0]: {start=0, end=5, name="bold", value=true}
// marks[1]: {start=6, end=11, name="link", value="https://example.com"}
```

---

## JSON Interoperability (`json.hpp`)

```cpp
#include <automerge-cpp/json.hpp>
```

Requires `nlohmann/json` (linked as a PUBLIC dependency).

### ADL Serialization

All automerge-cpp types provide `to_json`/`from_json` free functions for
automatic conversion with `nlohmann::json`:

```cpp
am::ScalarValue sv = am::Counter{42};
nlohmann::json j = sv;  // {"__type": "counter", "value": 42}

am::ScalarValue sv2;
am::from_json(j, sv2);  // round-trips through tagged format

nlohmann::json change_j = doc.get_changes().back();  // Change → JSON
nlohmann::json patch_j = patches;                      // vector<Patch> → JSON
```

**Tagged types** (round-trip safe):
- `Counter{N}` → `{"__type": "counter", "value": N}`
- `Timestamp{N}` → `{"__type": "timestamp", "value": N}`
- `Bytes{...}` → `{"__type": "bytes", "value": "<base64>"}`

**Identity types** serialize as hex strings:
- `ActorId` → 32-character hex string
- `ChangeHash` → 64-character hex string

### Document Export / Import

```cpp
// Export the full document (or subtree) as JSON
auto j = am::export_json(doc);                        // full document
auto j = am::export_json(doc, config_id);             // subtree
auto j = am::export_json_at(doc, heads);              // historical

// Import JSON into a document
am::import_json(doc, json{{"name", "Alice"}, {"age", 30}});

// Import within an existing transaction
doc.transact([&](am::Transaction& tx) {
    am::import_json(tx, some_json);
});

// Round-trip: JSON → Document → JSON
auto input = json{{"a", 1}, {"b", {{"c", 2}}}};
auto doc = am::Document{};
am::import_json(doc, input);
assert(am::export_json(doc) == input);
```

### Child Object Lookup

```cpp
auto child = doc.get_obj_id(am::root, "config");       // map key
auto elem  = doc.get_obj_id(list_id, std::size_t{0});  // list index
// Returns nullopt if key/index doesn't exist or value isn't an object
```

### JSON Pointer (RFC 6901)

```cpp
auto val = am::get_pointer(doc, "/config/port");     // get nested value
am::put_pointer(doc, "/config/timeout", am::ScalarValue{std::int64_t{30}});
am::delete_pointer(doc, "/config/debug");

// Escaped characters: ~0 = ~, ~1 = /
auto val = am::get_pointer(doc, "/a~1b");            // key "a/b"
auto val = am::get_pointer(doc, "/c~0d");            // key "c~d"

// List append with "-"
am::put_pointer(doc, "/items/-", am::ScalarValue{std::string{"new"}});
```

### JSON Patch (RFC 6902)

```cpp
am::apply_json_patch(doc, json::parse(R"([
    {"op": "add", "path": "/name", "value": "Alice"},
    {"op": "remove", "path": "/deprecated"},
    {"op": "replace", "path": "/version", "value": "2.0"},
    {"op": "move", "from": "/old", "path": "/new"},
    {"op": "copy", "from": "/src", "path": "/dst"},
    {"op": "test", "path": "/name", "value": "Alice"}
])"));

// Generate a diff between two documents
auto patch = am::diff_json_patch(doc_before, doc_after);
```

### JSON Merge Patch (RFC 7386)

```cpp
am::apply_merge_patch(doc, json{
    {"name", "Bob"},        // set/replace
    {"deprecated", nullptr}, // delete (null = remove)
    {"config", {{"port", 9090}}},  // recursive merge
});

auto patch = am::generate_merge_patch(doc_before, doc_after);
```

### Flatten / Unflatten

```cpp
auto flat = am::flatten(doc);
// {"/name": "Alice", "/config/port": 8080, "/items/0": "Milk", ...}

am::unflatten(doc, flat);  // recreate nested structure from flat map
```
