# Phase 12: nlohmann/json Interoperability & Modern C++ API

**Status**: Planned
**Target version**: v0.5.0
**Depends on**: Phase 11 (performance — in progress)

---

## Motivation

automerge-cpp has a correct, well-tested CRDT implementation, but its public API
doesn't yet feel like idiomatic modern C++. Users must write verbose variant
extraction code, wrap string literals in `std::string{...}`, and manually
navigate nested objects by ObjId. Meanwhile, [nlohmann/json](https://github.com/nlohmann/json)
is the de facto standard C++ JSON library — its design is a masterclass in making
a C++ type feel native: implicit conversions, initializer lists, operator
overloads, STL container compatibility, and ADL-based extensibility.

Automerge documents **are** JSON documents with CRDT semantics. The two libraries
are a natural fit:

- **Import**: initialize a Document from `nlohmann::json` (REST responses, config files)
- **Export**: materialize a Document as `nlohmann::json` (UI rendering, debugging, APIs)
- **JSON Patch (RFC 6902)**: apply standard patch operations to a CRDT document
- **JSON Merge Patch (RFC 7386)**: simple partial-update semantics that align with CRDT merge
- **JSON Pointer (RFC 6901)**: path-based access (`"/users/0/name"`) alongside ObjId navigation
- **ADL serialization**: `to_json`/`from_json` for all automerge-cpp types
- **Diff**: generate standard JSON Patches from Automerge document diffs

Beyond interop, this phase modernizes the core API with templates, overloads,
and STL integration — making automerge-cpp easy to use and hard to use incorrectly.

---

## Table of Contents

1. [Dependency Integration](#1-dependency-integration)
2. [Modern C++ API Improvements](#2-modern-c-api-improvements)
   - 2A. Implicit scalar conversion (eliminate `std::string{...}` noise)
   - 2B. Typed value extraction helpers (eliminate double `std::get`)
   - 2C. Templated `transact` (return values, no `std::function` overhead)
   - 2D. STL container overloads (initializer lists, ranges, maps, vectors)
   - 2E. Operator overloads for Document/Transaction
   - 2F. `overload` helper for variant visitors
   - 2G. Path-based access API
3. [nlohmann/json Interoperability Layer](#3-nlohmannjson-interoperability-layer)
   - 3A. ADL serialization (`to_json`/`from_json` for all types)
   - 3B. Document export (`to_json`)
   - 3C. Document import (`from_json`)
   - 3D. JSON Pointer (RFC 6901)
   - 3E. JSON Patch (RFC 6902)
   - 3F. JSON Merge Patch (RFC 7386)
   - 3G. Automerge Patches as JSON Patches
   - 3H. Flatten / Unflatten
4. [Example Code Modernization](#4-example-code-modernization)
5. [Testing Strategy](#5-testing-strategy)
6. [Implementation Phases](#6-implementation-phases)
7. [File Layout](#7-file-layout)
8. [Design Decisions](#8-design-decisions)

---

## 1. Dependency Integration

### 1.1 Add upstream submodule

```bash
git submodule add https://github.com/nlohmann/json.git upstream/json
```

This gives us a reference copy for studying the API (same pattern as
`upstream/automerge/` for the Rust library).

### 1.2 CMake FetchContent (build dependency)

nlohmann/json is header-only. Add it via FetchContent in the root `CMakeLists.txt`:

```cmake
option(AUTOMERGE_CPP_WITH_JSON "Enable nlohmann/json interoperability" ON)

if(AUTOMERGE_CPP_WITH_JSON)
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG        v3.11.3
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(nlohmann_json)
    target_link_libraries(automerge_cpp PUBLIC nlohmann_json::nlohmann_json)
endif()
```

**Rationale**: FetchContent is consistent with how we already fetch Google Test,
Google Benchmark, and BS::thread_pool. The `PUBLIC` link makes nlohmann/json
available to downstream consumers automatically.

### 1.3 Conditional compilation

All nlohmann/json interop code is guarded by `#ifdef AUTOMERGE_CPP_WITH_JSON`
(set by CMake). The core library remains usable without nlohmann/json.

---

## 2. Modern C++ API Improvements

These changes improve the core API regardless of nlohmann/json. They make
automerge-cpp feel like a native C++ type.

### 2A. Implicit Scalar Conversion

**Problem**: Every example writes `std::string{"hello"}` and `std::int64_t{42}`:

```cpp
// Current (verbose)
tx.put(root, "name", std::string{"Alice"});
tx.put(root, "age", std::int64_t{30});
tx.put(root, "active", ScalarValue{true});
```

**Solution**: Add overloads to `Transaction::put`, `Transaction::insert`, and
`Transaction::set` that accept common C++ types directly:

```cpp
// New overloads in Transaction
void put(const ObjId& obj, std::string_view key, const char* val);
void put(const ObjId& obj, std::string_view key, std::string_view val);
void put(const ObjId& obj, std::string_view key, std::string val);
void put(const ObjId& obj, std::string_view key, std::int64_t val);
void put(const ObjId& obj, std::string_view key, std::uint64_t val);
void put(const ObjId& obj, std::string_view key, double val);
void put(const ObjId& obj, std::string_view key, bool val);
void put(const ObjId& obj, std::string_view key, Null val);
void put(const ObjId& obj, std::string_view key, Counter val);
void put(const ObjId& obj, std::string_view key, Timestamp val);

// Same for insert() and set() with size_t index parameter
```

Alternatively, use a single constrained template:

```cpp
/// Put any scalar-convertible value at a map key.
template <typename T>
    requires std::constructible_from<ScalarValue, T>
void put(const ObjId& obj, std::string_view key, T&& val) {
    put(obj, key, ScalarValue{std::forward<T>(val)});
}
```

Additionally, add `int` → `std::int64_t` promotion so that plain integer
literals work:

```cpp
// Enable: tx.put(root, "age", 30)  (int promotes to int64_t)
void put(const ObjId& obj, std::string_view key, int val) {
    put(obj, key, std::int64_t{val});
}
```

**After**:
```cpp
tx.put(root, "name", "Alice");       // const char* → string
tx.put(root, "age", 30);             // int → int64_t
tx.put(root, "active", true);        // bool directly
tx.put(root, "score", 3.14);         // double directly
tx.put(root, "views", Counter{0});   // Counter directly
```

The same overloads apply to `insert()` and `set()`.

### 2B. Typed Value Extraction Helpers

**Problem**: Extracting a scalar from `std::optional<Value>` requires two levels
of `std::get`, which is verbose and error-prone:

```cpp
// Current (3 lines to read a string)
auto val = doc.get(root, "name");
auto& sv = std::get<ScalarValue>(*val);
auto& name = std::get<std::string>(sv);
```

**Solution**: Add free-function helpers in `value.hpp`:

```cpp
/// Extract a typed scalar from a Value, or nullopt on type mismatch.
template <typename T>
auto get_scalar(const Value& v) -> std::optional<T> {
    if (auto* sv = std::get_if<ScalarValue>(&v)) {
        if (auto* t = std::get_if<T>(sv)) {
            return *t;
        }
    }
    return std::nullopt;
}

/// Extract a typed scalar from an optional<Value>.
template <typename T>
auto get_scalar(const std::optional<Value>& v) -> std::optional<T> {
    if (!v) return std::nullopt;
    return get_scalar<T>(*v);
}
```

Common type aliases for the most frequent extractions:

```cpp
inline auto get_string(const std::optional<Value>& v) -> std::optional<std::string> {
    return get_scalar<std::string>(v);
}
inline auto get_int(const std::optional<Value>& v) -> std::optional<std::int64_t> {
    return get_scalar<std::int64_t>(v);
}
inline auto get_uint(const std::optional<Value>& v) -> std::optional<std::uint64_t> {
    return get_scalar<std::uint64_t>(v);
}
inline auto get_double(const std::optional<Value>& v) -> std::optional<double> {
    return get_scalar<double>(v);
}
inline auto get_bool(const std::optional<Value>& v) -> std::optional<bool> {
    return get_scalar<bool>(v);
}
inline auto get_counter(const std::optional<Value>& v) -> std::optional<Counter> {
    return get_scalar<Counter>(v);
}
```

Also add typed `get` directly on Document:

```cpp
// On Document:
template <typename T>
auto get_as(const ObjId& obj, std::string_view key) const -> std::optional<T> {
    return get_scalar<T>(get(obj, key));
}

template <typename T>
auto get_as(const ObjId& obj, std::size_t index) const -> std::optional<T> {
    return get_scalar<T>(get(obj, index));
}
```

**After**:
```cpp
// One-liner value extraction
auto name = doc.get_as<std::string>(root, "name");    // -> optional<string>
auto age  = doc.get_as<std::int64_t>(root, "age");    // -> optional<int64_t>
auto pi   = doc.get_as<double>(root, "pi");            // -> optional<double>

// Or using free functions
auto name = get_string(doc.get(root, "name"));
```

### 2C. Templated `transact` with Return Values

**Problem**: `transact` uses `std::function<void(Transaction&)>`, which:
1. Has runtime overhead from type erasure
2. Cannot return values — forces users to declare ObjIds outside the lambda
3. Uses opaque `auto&` parameter

```cpp
// Current: must declare list_id outside
ObjId list_id;
doc.transact([&](auto& tx) {
    list_id = tx.put_object(root, "items", ObjType::list);
});
```

**Solution**: Add a templated overload that returns values:

```cpp
// Non-returning (keep existing for backward compatibility)
template <typename Fn>
    requires std::invocable<Fn, Transaction&>
void transact(Fn&& fn);

// Returning version
template <typename Fn>
    requires std::invocable<Fn, Transaction&>
auto transact(Fn&& fn) -> std::invoke_result_t<Fn, Transaction&> {
    // ...commit logic...
    return fn(tx);
}
```

**After**:
```cpp
auto list_id = doc.transact([](Transaction& tx) {
    return tx.put_object(root, "items", ObjType::list);
});
```

The same applies to `transact_with_patches`:

```cpp
// Return both user result and patches
template <typename Fn>
auto transact_with_patches(Fn&& fn)
    -> std::pair<std::invoke_result_t<Fn, Transaction&>, std::vector<Patch>>;
```

**Note**: Keep the existing `std::function` overloads for ABI stability. The
template overloads are additions, not replacements.

### 2D. STL Container Overloads

**Problem**: Populating maps and lists requires one-at-a-time calls:

```cpp
doc.transact([&](auto& tx) {
    tx.put(root, "name", "Alice");
    tx.put(root, "age", 30);
    tx.put(root, "active", true);

    auto list_id = tx.put_object(root, "items", ObjType::list);
    tx.insert(list_id, 0, "Milk");
    tx.insert(list_id, 1, "Eggs");
    tx.insert(list_id, 2, "Bread");
});
```

**Solution**: Add batch overloads that accept STL containers and initializer lists.

#### Initializer list for maps (key-value pairs)

```cpp
using MapInit = std::initializer_list<std::pair<std::string_view, ScalarValue>>;

/// Batch put from initializer list of key-value pairs.
void put_all(const ObjId& obj, MapInit entries);
```

**Usage**:
```cpp
tx.put_all(root, {
    {"name", "Alice"},
    {"age",  std::int64_t{30}},
    {"active", true}
});
```

#### Initializer list for lists (values)

```cpp
/// Batch insert from an initializer list of scalars.
void insert_all(const ObjId& obj, std::size_t start,
                std::initializer_list<ScalarValue> values);
```

**Usage**:
```cpp
auto list_id = tx.put_object(root, "items", ObjType::list);
tx.insert_all(list_id, 0, {"Milk", "Eggs", "Bread"});
```

#### Range-based overloads (C++20 ranges)

```cpp
/// Put entries from any range of (string, ScalarValue) pairs.
template <std::ranges::input_range R>
    requires std::convertible_to<
        std::ranges::range_value_t<R>,
        std::pair<std::string_view, ScalarValue>>
void put_range(const ObjId& obj, R&& range);

/// Insert elements from any range of ScalarValues.
template <std::ranges::input_range R>
    requires std::convertible_to<std::ranges::range_value_t<R>, ScalarValue>
void insert_range(const ObjId& obj, std::size_t start, R&& range);
```

**Usage**:
```cpp
auto items = std::vector<std::string>{"Milk", "Eggs", "Bread"};
tx.insert_range(list_id, 0, items | std::views::transform([](auto& s) {
    return ScalarValue{s};
}));

// Or from a std::map
auto config = std::map<std::string, std::int64_t>{
    {"timeout", 30}, {"retries", 3}
};
tx.put_range(root, config | std::views::transform([](auto& kv) {
    return std::pair{std::string_view{kv.first}, ScalarValue{kv.second}};
}));
```

#### Import from `std::map` / `std::unordered_map`

```cpp
/// Populate a map object from a std::map.
template <typename Map>
    requires requires(Map m) {
        { m.begin()->first } -> std::convertible_to<std::string_view>;
        { m.begin()->second } -> std::convertible_to<ScalarValue>;
    }
void put_map(const ObjId& obj, const Map& map);

/// Populate a list from a std::vector (or any contiguous range).
template <typename Vec>
    requires std::ranges::input_range<Vec> &&
             std::convertible_to<std::ranges::range_value_t<Vec>, ScalarValue>
void insert_vec(const ObjId& obj, std::size_t start, const Vec& vec);
```

**Usage**:
```cpp
auto data = std::unordered_map<std::string, ScalarValue>{
    {"host", "localhost"},
    {"port", std::int64_t{8080}}
};
tx.put_map(root, data);

auto numbers = std::vector<std::int64_t>{1, 2, 3, 4, 5};
tx.insert_vec(list_id, 0, numbers);
```

### 2E. Operator Overloads for Document

Inspired by nlohmann/json's `operator[]`:

```cpp
// On Document (read-only):
auto operator[](std::string_view key) const -> std::optional<Value> {
    return get(root, key);
}
```

**Usage**:
```cpp
auto name = doc["name"];  // same as doc.get(root, "name")
```

**Note**: This is a convenience for root-level access only. We intentionally
do NOT make `operator[]` auto-create missing keys (unlike nlohmann/json)
because Document reads should be pure. Mutations always go through transactions.

### 2F. `overload` Helper

Add a standard C++ overload set helper for variant visitors:

```cpp
// In include/automerge-cpp/value.hpp or a new utility header
template <typename... Ts>
struct overload : Ts... { using Ts::operator()...; };

// C++17 CTAD
template <typename... Ts>
overload(Ts...) -> overload<Ts...>;
```

**Usage**:
```cpp
for (const auto& patch : patches) {
    std::visit(overload{
        [](const PatchSpliceText& s) { printf("splice at %zu\n", s.index); },
        [](const PatchInsert& i)     { printf("insert at %zu\n", i.index); },
        [](const PatchDelete& d)     { printf("delete at %zu\n", d.index); },
        [](const PatchPut& p)        { printf("put\n"); },
        [](const PatchIncrement& i)  { printf("increment %lld\n", i.delta); },
    }, patch.action);
}
```

### 2G. Path-Based Access API

Provide a way to access nested values without manually tracking ObjIds:

```cpp
/// Get a value at a path of keys/indices from root.
/// Example: doc.get_path("config", "database", "port")
template <typename... Props>
    requires (std::convertible_to<Props, Prop> && ...)
auto get_path(Props&&... props) const -> std::optional<Value>;

/// Get a value at a path specified as a vector of Props.
auto get_path(std::span<const Prop> path) const -> std::optional<Value>;
```

**Usage**:
```cpp
// Navigate nested structure without ObjId tracking
auto port = doc.get_path("config", "database", "port");
auto item = doc.get_path("todos", std::size_t{0}, "title");
```

**Implementation**: Walk the document tree, resolving each path element to
its ObjId by looking up get() and extracting the ObjType/ObjId at each level.

---

## 3. nlohmann/json Interoperability Layer

All interop code lives in `include/automerge-cpp/json.hpp` (public header)
and `src/json.cpp` (implementation), within namespace `automerge_cpp`.

### 3A. ADL Serialization for All Types

Provide `to_json`/`from_json` free functions so all automerge-cpp types
work natively with nlohmann/json:

```cpp
// In namespace automerge_cpp (found via ADL)

void to_json(nlohmann::json& j, const ScalarValue& val);
void from_json(const nlohmann::json& j, ScalarValue& val);

void to_json(nlohmann::json& j, const Value& val);
void from_json(const nlohmann::json& j, Value& val);

void to_json(nlohmann::json& j, Null);
void to_json(nlohmann::json& j, const Counter& c);
void to_json(nlohmann::json& j, const Timestamp& t);

void to_json(nlohmann::json& j, const ActorId& id);    // hex string
void from_json(const nlohmann::json& j, ActorId& id);

void to_json(nlohmann::json& j, const ChangeHash& h);  // hex string
void from_json(const nlohmann::json& j, ChangeHash& h);

void to_json(nlohmann::json& j, const OpId& id);
void to_json(nlohmann::json& j, const ObjId& id);

void to_json(nlohmann::json& j, const Change& c);
void to_json(nlohmann::json& j, const Patch& p);
void to_json(nlohmann::json& j, const PatchAction& a);
void to_json(nlohmann::json& j, const Mark& m);
void to_json(nlohmann::json& j, const Cursor& c);
void to_json(nlohmann::json& j, const SyncMessage& m);
```

**After**:
```cpp
nlohmann::json j = some_scalar_value;   // implicit conversion
auto sv = j.get<ScalarValue>();          // implicit extraction

// Serialize a patch list to JSON for debugging
nlohmann::json patch_json = patches;     // vector<Patch> → JSON array
std::cout << patch_json.dump(2) << "\n";

// Serialize a Change for logging
nlohmann::json change_json = change;
```

### 3B. Document Export (`to_json`)

Materialize the current document state as a `nlohmann::json` value:

```cpp
/// Export an object subtree as JSON.
/// Resolves conflicts using deterministic last-writer-wins (by OpId ordering).
auto to_json(const Document& doc, const ObjId& obj = root) -> nlohmann::json;

/// Export state at a historical point.
auto to_json_at(const Document& doc,
                const std::vector<ChangeHash>& heads,
                const ObjId& obj = root) -> nlohmann::json;
```

**Type mapping**:

| Automerge Type | JSON Type | Notes |
|----------------|-----------|-------|
| `ObjType::map` / `table` | `object` | Keys preserved |
| `ObjType::list` | `array` | Order preserved |
| `ObjType::text` | `string` | Materialized via `doc.text()` |
| `Null` | `null` | |
| `bool` | `boolean` | |
| `std::int64_t` | `number` | Integer |
| `std::uint64_t` | `number` | Integer |
| `double` | `number` | Floating point |
| `std::string` | `string` | |
| `Counter` | `number` | `.value` field (integer) |
| `Timestamp` | `number` | `.millis_since_epoch` (integer) |
| `Bytes` | `string` | Base64-encoded |

**Conflict resolution options**:

```cpp
struct ToJsonOptions {
    enum class ConflictPolicy {
        last_writer_wins,    // Default: deterministic by OpId
        all_as_array,        // Expose all conflicting values as an array
    };
    ConflictPolicy conflicts = ConflictPolicy::last_writer_wins;

    bool timestamps_as_iso8601 = false;  // false: millis (number), true: ISO string
    bool bytes_as_base64 = true;         // false: array of numbers
};

auto to_json(const Document& doc,
             const ObjId& obj = root,
             const ToJsonOptions& opts = {}) -> nlohmann::json;
```

### 3C. Document Import (`from_json`)

Populate a Document from a JSON value, within a transaction:

```cpp
/// Import a JSON value into the document at the given target object.
/// Wraps all mutations in a single transaction.
void from_json(Document& doc,
               const nlohmann::json& j,
               const ObjId& target = root);

/// Import within an existing transaction (for batching).
void from_json(Transaction& tx,
               const nlohmann::json& j,
               const ObjId& target = root);
```

**Type inference**:

| JSON Type | Automerge Type | Notes |
|-----------|---------------|-------|
| `null` | `Null` | |
| `boolean` | `bool` | |
| `number` (integer) | `std::int64_t` | Unsigned if > INT64_MAX |
| `number` (float) | `double` | |
| `string` | `std::string` | (not text ObjType — see schema) |
| `object` | `ObjType::map` | Recursive |
| `array` | `ObjType::list` | Recursive |

**Schema-guided import** for disambiguation:

```cpp
/// Type hints for fields that need special Automerge types.
struct FieldHint {
    std::string path;         // JSON Pointer path (e.g. "/views")
    enum Type { counter, timestamp, text } type;
};

void from_json(Document& doc,
               const nlohmann::json& j,
               std::span<const FieldHint> hints,
               const ObjId& target = root);
```

**Usage**:
```cpp
auto j = nlohmann::json::parse(R"({
    "title": "Shopping List",
    "views": 0,
    "content": "Hello World",
    "items": ["Milk", "Eggs"]
})");

auto doc = Document{};
from_json(doc, j, {{"/views", FieldHint::counter},
                    {"/content", FieldHint::text}});
// "views" becomes a Counter, "content" becomes a text ObjType
```

**Static factory for clean one-liner construction**:

```cpp
// On Document:
static auto from_json(const nlohmann::json& j) -> Document {
    auto doc = Document{};
    automerge_cpp::from_json(doc, j);
    return doc;
}
```

**Usage**:
```cpp
auto doc = Document::from_json({
    {"name", "Alice"},
    {"age", 30},
    {"tasks", {"read", "code", "test"}}
});
```

### 3D. JSON Pointer (RFC 6901)

Enable path-based access using standard JSON Pointer syntax:

```cpp
/// Get a value at a JSON Pointer path.
auto get_pointer(const Document& doc, std::string_view pointer)
    -> std::optional<Value>;

/// Get a value at a nlohmann::json_pointer path.
auto get_pointer(const Document& doc, const nlohmann::json::json_pointer& ptr)
    -> std::optional<Value>;

/// Put a scalar at a JSON Pointer path (creates intermediates as maps).
void put_pointer(Transaction& tx, std::string_view pointer, ScalarValue val);

/// Delete at a JSON Pointer path.
void delete_pointer(Transaction& tx, std::string_view pointer);
```

**Usage**:
```cpp
auto port = get_pointer(doc, "/config/database/port");
auto name = get_pointer(doc, "/users/0/name");

doc.transact([](auto& tx) {
    put_pointer(tx, "/config/database/port", std::int64_t{5432});
    delete_pointer(tx, "/config/deprecated_key");
});
```

**Implementation**:
1. Parse the pointer into path segments (split on `/`, unescape `~0`→`~`, `~1`→`/`)
2. Walk the document tree: for each segment, resolve the current ObjId via `doc.get()`
3. If the segment is numeric and the current object is a list, treat it as an index
4. Otherwise treat it as a map key

### 3E. JSON Patch (RFC 6902)

Apply an array of patch operations to a Document:

```cpp
/// Apply an RFC 6902 JSON Patch to the document.
/// All operations run in a single transaction (atomic).
/// Operations: add, remove, replace, move, copy, test.
auto apply_json_patch(Document& doc, const nlohmann::json& patch)
    -> std::expected<void, Error>;
```

**Operation mapping**:

| JSON Patch Op | Automerge Operation | Notes |
|---------------|---------------------|-------|
| `add` | `put` or `insert` | Map key → put, list index → insert |
| `remove` | `delete_key` or `delete_index` | |
| `replace` | `delete` + `put`/`insert` | Atomic via single transaction |
| `move` | `get` + `delete` + `add` | Decomposed |
| `copy` | `get` + `add` | Decomposed |
| `test` | `get` + assert | Returns error on mismatch |

**Usage**:
```cpp
auto patch = nlohmann::json::parse(R"([
    {"op": "replace", "path": "/name", "value": "Bob"},
    {"op": "add", "path": "/scores/-", "value": 95},
    {"op": "remove", "path": "/deprecated"},
    {"op": "test", "path": "/version", "value": 2}
])");

auto result = apply_json_patch(doc, patch);
if (!result) {
    std::cerr << result.error().message << "\n";
}
```

**Generate** a JSON Patch from two document states:

```cpp
/// Generate an RFC 6902 JSON Patch representing the diff between two documents.
auto diff_as_json_patch(const Document& before, const Document& after)
    -> nlohmann::json;

/// Generate a JSON Patch between two historical states of the same document.
auto diff_as_json_patch(const Document& doc,
                        const std::vector<ChangeHash>& before_heads,
                        const std::vector<ChangeHash>& after_heads)
    -> nlohmann::json;
```

### 3F. JSON Merge Patch (RFC 7386)

Simpler than JSON Patch — partial updates with null-means-delete semantics:

```cpp
/// Apply an RFC 7386 JSON Merge Patch.
/// - Non-null values: set/replace
/// - null values: delete
/// - Missing keys: unchanged
void apply_merge_patch(Document& doc,
                       const nlohmann::json& patch,
                       const ObjId& target = root);

/// Generate a merge patch showing differences.
auto generate_merge_patch(const Document& before,
                          const Document& after)
    -> nlohmann::json;
```

**Usage**:
```cpp
// Partial update: change name, delete deprecated, leave age unchanged
apply_merge_patch(doc, {
    {"name", "Bob"},
    {"deprecated", nullptr},
});
```

**Why this is a great fit for CRDTs**: Merge patches are naturally idempotent
and commutative for non-conflicting keys — the same properties as CRDT merge.

### 3G. Automerge Patches as JSON Patches

Convert automerge-cpp's `Patch` / `PatchAction` types to RFC 6902 format:

```cpp
/// Convert automerge patches to RFC 6902 JSON Patch format.
auto patches_to_json_patch(const std::vector<Patch>& patches)
    -> nlohmann::json;
```

**Mapping**:

| Automerge Patch | JSON Patch Op |
|-----------------|---------------|
| `PatchPut` | `add` or `replace` (depending on prior existence) |
| `PatchInsert` | `add` (with array index path) |
| `PatchDelete` | `remove` |
| `PatchIncrement` | `replace` (with the new counter value) |
| `PatchSpliceText` | Decomposed to `remove` + `add` at string level |

**Use case**: UI frameworks that understand JSON Patch (React, Vue) can
consume automerge patch notifications directly.

### 3H. Flatten / Unflatten

Convert a nested document to flat JSON Pointer → value pairs:

```cpp
/// Flatten: nested document → flat map of (pointer_path → value).
auto flatten(const Document& doc, const ObjId& obj = root)
    -> std::map<std::string, Value>;

/// Unflatten: flat map → nested document.
void unflatten(Document& doc,
               const std::map<std::string, ScalarValue>& flat,
               const ObjId& target = root);
```

**Usage**:
```cpp
auto flat = flatten(doc);
// flat = {"/name": "Alice", "/config/port": 8080, "/items/0": "Milk", ...}

// Useful for config systems, search indexing, debugging
for (const auto& [path, value] : flat) {
    std::cout << path << " = " << nlohmann::json(value) << "\n";
}
```

---

## 4. Example Code Modernization

Every example should be rewritten to showcase the new APIs. Below are
before/after comparisons.

### 4.1 `basic_usage.cpp`

**Before**:
```cpp
doc.transact([&](auto& tx) {
    tx.put(am::root, "title", std::string{"Shopping List"});
    tx.put(am::root, "created_by", std::string{"Alice"});

    list_id = tx.put_object(am::root, "items", am::ObjType::list);
    tx.insert(list_id, 0, std::string{"Milk"});
    tx.insert(list_id, 1, std::string{"Eggs"});
    tx.insert(list_id, 2, std::string{"Bread"});
});

auto title = doc.get(am::root, "title");
if (title) {
    auto& sv = std::get<am::ScalarValue>(*title);
    std::printf("Title: %s\n", std::get<std::string>(sv).c_str());
}
```

**After**:
```cpp
auto list_id = doc.transact([](Transaction& tx) {
    tx.put(root, "title", "Shopping List");
    tx.put(root, "created_by", "Alice");

    auto list_id = tx.put_object(root, "items", ObjType::list);
    tx.insert_all(list_id, 0, {"Milk", "Eggs", "Bread"});
    return list_id;
});

if (auto title = doc.get_as<std::string>(root, "title")) {
    std::printf("Title: %s\n", title->c_str());
}
```

### 4.2 `basic_usage.cpp` — JSON interop variant

```cpp
// Initialize from JSON
auto doc = Document::from_json({
    {"title", "Shopping List"},
    {"created_by", "Alice"},
    {"items", {"Milk", "Eggs", "Bread"}},
    {"views", 0}
});

// Read via path
if (auto title = get_pointer(doc, "/title")) {
    std::printf("Title: %s\n", get_string(*title)->c_str());
}

// Export as JSON
auto j = to_json(doc);
std::cout << j.dump(2) << "\n";
```

### 4.3 `collaborative_todo.cpp`

**Before**:
```cpp
alice_doc.transact([&](auto& tx) {
    tx.put(am::root, "title", std::string{"Team Tasks"});
    todo_list = tx.put_object(am::root, "todos", am::ObjType::list);
    tx.insert(todo_list, 0, std::string{"Set up CI pipeline"});
    tx.insert(todo_list, 1, std::string{"Write unit tests"});
});
```

**After**:
```cpp
auto todo_list = alice_doc.transact([](Transaction& tx) {
    tx.put(root, "title", "Team Tasks");
    auto list = tx.put_object(root, "todos", ObjType::list);
    tx.insert_all(list, 0, {"Set up CI pipeline", "Write unit tests"});
    return list;
});
```

### 4.4 New example: `json_interop_demo.cpp`

A new example demonstrating all JSON integration features:

```cpp
#include <automerge-cpp/automerge.hpp>
#include <automerge-cpp/json.hpp>
#include <nlohmann/json.hpp>
#include <iostream>

namespace am = automerge_cpp;

int main() {
    // 1. Create from JSON
    auto doc = am::Document::from_json({
        {"name", "Alice"},
        {"age", 30},
        {"tasks", {"read", "code", "test"}}
    });

    // 2. Export as JSON
    auto j = am::to_json(doc);
    std::cout << "Initial: " << j.dump(2) << "\n";

    // 3. Apply JSON Merge Patch (RFC 7386)
    am::apply_merge_patch(doc, {
        {"age", 31},
        {"email", "alice@example.com"},
    });

    // 4. Apply JSON Patch (RFC 6902)
    am::apply_json_patch(doc, nlohmann::json::parse(R"([
        {"op": "add", "path": "/tasks/-", "value": "deploy"},
        {"op": "remove", "path": "/tasks/0"}
    ])"));

    // 5. Use JSON Pointer for access
    auto name = am::get_pointer(doc, "/name");
    auto task = am::get_pointer(doc, "/tasks/0");

    // 6. Fork, merge, diff
    auto bob = doc.fork();
    bob.transact([](am::Transaction& tx) {
        tx.put(am::root, "name", "Bob");
    });
    doc.merge(bob);

    // 7. Generate diff as JSON Patch
    auto diff = am::diff_as_json_patch(/* before */ doc, /* after */ bob);
    std::cout << "Diff: " << diff.dump(2) << "\n";

    // 8. ADL serialization: automerge types ↔ nlohmann::json
    auto change = doc.get_changes().back();
    nlohmann::json change_json = change;
    std::cout << "Last change: " << change_json.dump(2) << "\n";

    // 9. Flatten for inspection
    auto flat = am::flatten(doc);
    for (const auto& [path, value] : flat) {
        std::cout << path << " = " << nlohmann::json(value) << "\n";
    }

    return 0;
}
```

---

## 5. Testing Strategy

### 5.1 Test File Layout

| Test File | Covers | Estimated Tests |
|-----------|--------|-----------------|
| `tests/value_helpers_test.cpp` | `get_scalar`, `get_string`, `get_int`, etc. | ~20 |
| `tests/transaction_overloads_test.cpp` | Scalar overloads, `insert_all`, `put_all`, `put_map`, ranges | ~30 |
| `tests/transact_return_test.cpp` | Templated `transact` return values | ~10 |
| `tests/path_access_test.cpp` | `get_path`, operator[] on Document | ~15 |
| `tests/json_serialization_test.cpp` | ADL `to_json`/`from_json` for all types | ~25 |
| `tests/json_import_export_test.cpp` | `to_json(doc)`, `from_json(doc, j)`, round-trips | ~30 |
| `tests/json_pointer_test.cpp` | RFC 6901: `get_pointer`, `put_pointer`, escaping | ~15 |
| `tests/json_patch_test.cpp` | RFC 6902: all 6 operations, error cases | ~25 |
| `tests/json_merge_patch_test.cpp` | RFC 7386: apply, generate, idempotence | ~15 |
| `tests/json_diff_test.cpp` | `diff_as_json_patch`, `patches_to_json_patch` | ~15 |
| **Total** | | **~200 new tests** |

### 5.2 Property-Based Tests

```cpp
// Round-trip: JSON → Document → JSON preserves structure
TEST(JsonInterop, round_trip_preserves_structure) {
    auto j = nlohmann::json{{"a", 1}, {"b", {1, 2, 3}}, {"c", {{"d", true}}}};
    auto doc = Document::from_json(j);
    auto exported = to_json(doc);
    EXPECT_EQ(exported, j);
}

// Merge patch idempotence
TEST(JsonMergePatch, idempotent) {
    auto doc = Document::from_json({{"x", 1}});
    auto patch = nlohmann::json{{"x", 2}};
    apply_merge_patch(doc, patch);
    apply_merge_patch(doc, patch);  // second time is no-op
    EXPECT_EQ(to_json(doc)["x"], 2);
}

// CRDT property: to_json(merge(a,b)) == to_json(merge(b,a))
TEST(JsonInterop, merge_commutativity_in_json) {
    auto a = Document::from_json({{"x", 1}});
    auto b = a.fork();
    a.transact([](auto& tx) { tx.put(root, "a", 1); });
    b.transact([](auto& tx) { tx.put(root, "b", 2); });

    auto ab = Document{a}; ab.merge(b);
    auto ba = Document{b}; ba.merge(a);
    EXPECT_EQ(to_json(ab), to_json(ba));
}
```

### 5.3 RFC Compliance Tests

- **RFC 6901**: Test escape sequences (`~0`/`~1`), empty pointer `""`, root pointer `/`,
  array indices, out-of-bounds
- **RFC 6902**: Test all 6 operations, atomic rollback on failure, `test` operation
  assertions, `-` append syntax for arrays
- **RFC 7386**: Null-deletion, recursive merge, missing keys unchanged

### 5.4 Backward Compatibility

All existing 347+ tests must continue to pass unchanged. The new overloads
are additions — no existing signatures are modified or removed.

---

## 6. Implementation Phases

### Phase 12A: Core API Modernization (no nlohmann dependency)

**Estimated effort**: Medium

1. **2A**: Scalar conversion overloads on Transaction (`put`, `insert`, `set`)
2. **2B**: `get_scalar<T>`, `get_string`, `get_int`, etc. in `value.hpp`
3. **2B**: `Document::get_as<T>` templated getters
4. **2C**: Templated `transact` / `transact_with_patches` with return values
5. **2D**: `insert_all`, `put_all` initializer list overloads
6. **2D**: `put_map`, `insert_vec`, range-based overloads
7. **2E**: `Document::operator[]` for root access
8. **2F**: `overload` helper struct
9. **2G**: `get_path` variadic template
10. Tests: ~75 new tests
11. Update all 6 examples to use new APIs

### Phase 12B: nlohmann/json Dependency & ADL Serialization

**Estimated effort**: Medium

1. Add `upstream/json` submodule
2. CMake FetchContent integration with `AUTOMERGE_CPP_WITH_JSON` option
3. **3A**: `to_json`/`from_json` for all automerge-cpp types
4. `include/automerge-cpp/json.hpp` header
5. Tests: ~25 new tests

### Phase 12C: Document Import/Export

**Estimated effort**: Medium

1. **3B**: `to_json(doc)` with recursive materialization
2. **3B**: `to_json_at(doc, heads)` for historical export
3. **3B**: `ToJsonOptions` (conflict policy, timestamp format)
4. **3C**: `from_json(doc, j)` with recursive import
5. **3C**: `Document::from_json(j)` static factory
6. **3C**: Schema-guided import (`FieldHint`)
7. Tests: ~30 new tests, round-trip properties

### Phase 12D: JSON Pointer (RFC 6901)

**Estimated effort**: Small

1. **3D**: Pointer parsing (split, unescape)
2. **3D**: `get_pointer(doc, path)`
3. **3D**: `put_pointer(tx, path, val)`, `delete_pointer(tx, path)`
4. Tests: ~15 new tests, RFC compliance

### Phase 12E: JSON Patch (RFC 6902)

**Estimated effort**: Medium

1. **3E**: `apply_json_patch(doc, patch)` — all 6 operations
2. **3E**: `diff_as_json_patch(before, after)`
3. **3G**: `patches_to_json_patch(patches)`
4. Tests: ~25 new tests, RFC compliance

### Phase 12F: JSON Merge Patch (RFC 7386) & Extras

**Estimated effort**: Small

1. **3F**: `apply_merge_patch(doc, patch)`
2. **3F**: `generate_merge_patch(before, after)`
3. **3H**: `flatten(doc)`, `unflatten(doc, flat)`
4. New example: `json_interop_demo.cpp`
5. Tests: ~30 new tests

### Phase 12G: Documentation & Polish

**Estimated effort**: Small

1. Update `docs/api.md` with new APIs
2. Update `CLAUDE.md` with new test counts, headers, examples
3. Doxygen annotations on all new public functions
4. Version bump to v0.5.0

---

## 7. File Layout

### New/Modified Files

```
include/automerge-cpp/
├── json.hpp              (NEW)  nlohmann interop: to_json, from_json, patches, pointers
├── value.hpp             (MOD)  get_scalar<T>, get_string, overload helper
├── document.hpp          (MOD)  get_as<T>, get_path, operator[], from_json factory
├── transaction.hpp       (MOD)  scalar overloads, insert_all, put_all, range overloads

src/
├── json.cpp              (NEW)  nlohmann interop implementation
├── document.cpp          (MOD)  new method implementations
├── transaction.cpp       (MOD)  new overload implementations

tests/
├── value_helpers_test.cpp          (NEW)
├── transaction_overloads_test.cpp  (NEW)
├── transact_return_test.cpp        (NEW)
├── path_access_test.cpp            (NEW)
├── json_serialization_test.cpp     (NEW)
├── json_import_export_test.cpp     (NEW)
├── json_pointer_test.cpp           (NEW)
├── json_patch_test.cpp             (NEW)
├── json_merge_patch_test.cpp       (NEW)
├── json_diff_test.cpp              (NEW)

examples/
├── basic_usage.cpp            (MOD)  use new APIs
├── collaborative_todo.cpp     (MOD)  use new APIs
├── text_editor.cpp            (MOD)  use new APIs
├── sync_demo.cpp              (MOD)  use new APIs
├── thread_safe_demo.cpp       (MOD)  use new APIs
├── parallel_perf_demo.cpp     (MOD)  use new APIs
├── json_interop_demo.cpp      (NEW)  comprehensive JSON interop example

upstream/
├── json/                      (NEW)  nlohmann/json submodule
```

---

## 8. Design Decisions

### 8.1 Why nlohmann/json as a dependency (not just interop code)?

- **Industry standard**: Most widely used C++ JSON library (70k+ GitHub stars)
- **Header-only**: Zero binary dependency overhead
- **CMake native**: First-class FetchContent support
- **RFC compliance**: Built-in JSON Pointer, Patch, Merge Patch
- **ADL extensibility**: Our types work with nlohmann/json automatically via `to_json`/`from_json`
- **Design inspiration**: Its API patterns directly inform our modernization

### 8.2 Why `PUBLIC` link (not `PRIVATE`)?

Our public header `json.hpp` exposes `nlohmann::json` in function signatures.
Downstream consumers who include `json.hpp` need the nlohmann headers too.
The core headers (`document.hpp`, `transaction.hpp`) do NOT depend on nlohmann —
only `json.hpp` does.

### 8.3 Why conditional compilation (`AUTOMERGE_CPP_WITH_JSON`)?

Users who don't need JSON interop shouldn't pay for it. The core library
must remain dependency-free (other than the standard library and zlib).

### 8.4 Why explicit overloads instead of a single template for put/insert?

A single template `put(obj, key, T&& val)` risks ambiguity with existing
overloads and makes error messages worse when passing unsupported types.
Explicit overloads give:
- Clear compiler errors on unsupported types
- Exact signature documentation
- No SFINAE/concepts complexity in the public header
- IDE autocompletion that shows all supported types

However, we can provide BOTH: explicit overloads for common types AND a
constrained template as a catch-all for user-extended types.

### 8.5 Why keep `std::function` overloads alongside templates?

- ABI stability: existing compiled code continues to work
- `std::function` can be stored, passed around, composed
- Template versions are zero-overhead for direct lambda usage
- Both coexist without ambiguity (template is preferred by overload resolution)

### 8.6 Upstream Rust parity

The Rust Automerge library provides similar JSON interop:
- `automerge::AutoCommit::put_object` accepts nested structures
- `serde_json` integration for serialization
- `json!` macro for initialization (analogous to our `from_json`)

Our API maintains parity with the Rust API's capabilities while being
idiomatically C++.

### 8.7 Error handling

- New APIs use `std::expected<T, Error>` where failure is expected (RFC compliance)
- Use `std::optional<T>` where absence is the only failure mode (get operations)
- This is consistent with the style guide's prescription

### 8.8 Thread safety

All new APIs follow the existing thread safety model:
- Read functions (`to_json`, `get_pointer`, `flatten`) acquire shared locks
- Write functions (`from_json`, `apply_json_patch`, `apply_merge_patch`) acquire exclusive locks
- The transaction-based APIs inherit the transaction's lock

---

## Appendix A: API Comparison — Before vs After

### Reading a value

| Before | After |
|--------|-------|
| `auto v = doc.get(root, "name"); auto& sv = std::get<ScalarValue>(*v); auto& s = std::get<std::string>(sv);` | `auto s = doc.get_as<std::string>(root, "name");` |

### Writing values

| Before | After |
|--------|-------|
| `tx.put(root, "name", std::string{"Alice"});` | `tx.put(root, "name", "Alice");` |
| `tx.put(root, "age", std::int64_t{30});` | `tx.put(root, "age", 30);` |

### Creating lists

| Before | After |
|--------|-------|
| `list_id = tx.put_object(root, "items", ObjType::list); tx.insert(list_id, 0, std::string{"A"}); tx.insert(list_id, 1, std::string{"B"});` | `auto list_id = tx.put_object(root, "items", ObjType::list); tx.insert_all(list_id, 0, {"A", "B"});` |

### Returning from transactions

| Before | After |
|--------|-------|
| `ObjId id; doc.transact([&](auto& tx) { id = tx.put_object(...); });` | `auto id = doc.transact([](Transaction& tx) { return tx.put_object(...); });` |

### JSON initialization

| Before | After |
|--------|-------|
| (not possible) | `auto doc = Document::from_json({{"name", "Alice"}, {"age", 30}});` |

### Nested access

| Before | After |
|--------|-------|
| `auto inner = doc.get(root, "config"); /* extract ObjId */ auto val = doc.get(config_id, "port");` | `auto val = doc.get_path("config", "port");` or `auto val = get_pointer(doc, "/config/port");` |

---

## Appendix B: nlohmann/json Design Patterns We Adopt

| Pattern | nlohmann/json | automerge-cpp |
|---------|---------------|---------------|
| ADL serialization | `to_json`/`from_json` free functions | Same, for all automerge types |
| Implicit construction | `json j = 42;` | `ScalarValue sv = 42;` (via overloads) |
| Initializer lists | `json j = {{"k", "v"}};` | `tx.put_all(obj, {{"k", "v"}});` |
| `operator[]` | `j["key"]` | `doc["key"]` (root access, read-only) |
| `.get<T>()` | `j.get<int>()` | `doc.get_as<int>(obj, key)` |
| STL iterators | Full container interface | Keys/values/items iteration (future) |
| JSON Pointer | `j["/a/b"_json_pointer]` | `get_pointer(doc, "/a/b")` |
| JSON Patch | `j.patch(ops)` | `apply_json_patch(doc, ops)` |
| Merge Patch | `j.merge_patch(p)` | `apply_merge_patch(doc, p)` |

---

## Appendix C: Risks and Mitigations

| Risk | Mitigation |
|------|-----------|
| nlohmann/json version conflicts with user's copy | Use CMake `find_package` first, fall back to FetchContent |
| Template overloads break existing code | All overloads are additions; existing `ScalarValue` signatures remain |
| JSON import loses CRDT-specific types (Counter, Timestamp) | Schema hints (`FieldHint`) for disambiguation |
| Large JSON export is slow | Lazy/streaming export (future phase) |
| JSON Patch index shifting during list mutations | Apply operations in a single transaction with consistent indices |
| Thread safety of new APIs | Follow existing shared_mutex pattern |
| Compile time increase from nlohmann headers | Conditional inclusion (`AUTOMERGE_CPP_WITH_JSON`) |
