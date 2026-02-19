# JSON Integration Guide

automerge-cpp provides deep interoperability with [nlohmann/json](https://github.com/nlohmann/json)
through the `<automerge-cpp/json.hpp>` header. This guide covers every feature, from
basic import/export to RFC-standard patch operations.

## Table of Contents

- [Getting Started](#getting-started)
- [Import and Export](#import-and-export)
  - [Importing JSON into a Document](#importing-json-into-a-document)
  - [Exporting a Document to JSON](#exporting-a-document-to-json)
  - [Subtree Export](#subtree-export)
  - [Historical Export](#historical-export)
  - [Round-Trip Fidelity](#round-trip-fidelity)
- [JSON Pointer (RFC 6901)](#json-pointer-rfc-6901)
  - [Reading with get_pointer](#reading-with-get_pointer)
  - [Writing with put_pointer](#writing-with-put_pointer)
  - [Deleting with delete_pointer](#deleting-with-delete_pointer)
  - [Pointer Escaping](#pointer-escaping)
  - [List Access](#list-access)
- [JSON Patch (RFC 6902)](#json-patch-rfc-6902)
  - [Applying a Patch](#applying-a-patch)
  - [Supported Operations](#supported-operations)
  - [Generating a Diff](#generating-a-diff)
  - [Atomicity](#atomicity)
- [JSON Merge Patch (RFC 7386)](#json-merge-patch-rfc-7386)
  - [Applying a Merge Patch](#applying-a-merge-patch)
  - [Generating a Merge Patch](#generating-a-merge-patch)
  - [Merge Patch vs JSON Patch](#merge-patch-vs-json-patch)
- [Flatten and Unflatten](#flatten-and-unflatten)
  - [Flattening a Document](#flattening-a-document)
  - [Unflattening into a Document](#unflattening-into-a-document)
- [ADL Serialization](#adl-serialization)
  - [Scalar Types](#scalar-types)
  - [Tagged Format for Round-Trip Fidelity](#tagged-format-for-round-trip-fidelity)
  - [Identity Types](#identity-types)
  - [Compound Types](#compound-types)
  - [from_json Type Inference](#from_json-type-inference)
- [CRDT-Aware Workflows](#crdt-aware-workflows)
  - [Fork, Merge, and Diff](#fork-merge-and-diff)
  - [Collaborative JSON Editing](#collaborative-json-editing)
  - [Save, Load, and Verify](#save-load-and-verify)
- [Type Mapping Reference](#type-mapping-reference)
- [API Reference](#api-reference)
- [Building](#building)

---

## Getting Started

Include the JSON interop header alongside the main automerge header:

```cpp
#include <automerge-cpp/automerge.hpp>
#include <automerge-cpp/json.hpp>
#include <nlohmann/json.hpp>

namespace am = automerge_cpp;
using json = nlohmann::json;

// JSON interop functions live in am::json::
am::json::import_json(doc, j);
am::json::export_json(doc);
am::json::get_pointer(doc, "/path");

// ADL serialization stays in am:: (required for implicit conversion)
json j = am::ScalarValue{am::Counter{42}};
```

The `nlohmann_json` library is linked as a PUBLIC dependency of `automerge-cpp`,
so no additional CMake configuration is needed.

---

## Import and Export

### Importing JSON into a Document

`import_json` recursively converts a `nlohmann::json` value into Automerge
operations. JSON objects become maps, arrays become lists, and scalars are stored
with their natural C++ types.

```cpp
auto input = json::parse(R"({
    "name": "automerge-cpp",
    "version": "0.5.0",
    "stars": 42,
    "active": true,
    "tags": ["crdt", "collaborative", "c++23"],
    "config": {
        "port": 8080,
        "host": "localhost"
    }
})");

auto doc = am::Document{};
am::json::import_json(doc, input);

// All data is now in the Automerge document
auto name = doc.get<std::string>(am::root, "name");   // "automerge-cpp"
auto stars = doc.get<std::int64_t>(am::root, "stars"); // 42
```

All mutations are wrapped in a single transaction for atomicity. You can also
import within an existing transaction:

```cpp
doc.transact([&](am::Transaction& tx) {
    tx.put(am::root, "imported_at", "2025-01-01");
    am::json::import_json(tx, input, am::root);  // same transaction
});
```

Import into a subtree by specifying a target object:

```cpp
auto config_id = doc.get_obj_id(am::root, "config");
am::json::import_json(doc, json{{"timeout", 30}}, *config_id);
```

### Exporting a Document to JSON

`export_json` recursively walks the Automerge document tree and produces a
`nlohmann::json` value:

```cpp
auto exported = am::json::export_json(doc);
std::cout << exported.dump(2) << std::endl;
```

Output:
```json
{
  "active": true,
  "config": {
    "host": "localhost",
    "port": 8080
  },
  "name": "automerge-cpp",
  "stars": 42,
  "tags": [
    "crdt",
    "collaborative",
    "c++23"
  ],
  "version": "0.5.0"
}
```

The export handles all Automerge object types:

| Automerge Type | JSON Type |
|----------------|-----------|
| Map / Table | Object |
| List | Array |
| Text | String |
| `std::int64_t` | Number (integer) |
| `std::uint64_t` | Number (unsigned) |
| `double` | Number (float) |
| `bool` | Boolean |
| `Null` | null |
| `std::string` | String |
| `Counter` | Number (plain value, lossy) |
| `Timestamp` | Number (millis, lossy) |
| `Bytes` | String (base64-encoded) |

### Subtree Export

Export a specific subtree by passing its `ObjId`:

```cpp
auto config_id = doc.get_obj_id(am::root, "config");
auto config_json = am::json::export_json(doc, *config_id);
// {"host": "localhost", "port": 8080}
```

### Historical Export

Export the document as it was at a specific point in time:

```cpp
auto heads = doc.get_heads();
// ... more changes happen ...
auto snapshot = am::json::export_json_at(doc, heads);
```

### Round-Trip Fidelity

For standard JSON types (strings, numbers, booleans, null, objects, arrays),
import and export are lossless:

```cpp
auto input = json::parse(R"({"key": "value", "nums": [1, 2, 3]})");
auto doc = am::Document{};
am::json::import_json(doc, input);
auto output = am::json::export_json(doc);
assert(input == output);  // true
```

Counter and Timestamp values export as plain numbers. For full type-preserving
round-trips, use the [ADL serialization](#adl-serialization) functions instead.

---

## JSON Pointer (RFC 6901)

[JSON Pointer](https://www.rfc-editor.org/rfc/rfc6901) provides a string syntax
for addressing values within a JSON-like document. automerge-cpp implements
this for reading, writing, and deleting values deep in the document tree.

### Reading with get_pointer

```cpp
auto doc = am::Document{};
am::json::import_json(doc, json{
    {"config", {{"database", {{"port", 5432}, {"host", "localhost"}}}}},
    {"items", {1, 2, 3}}
});

// Nested map access
auto port = am::json::get_pointer(doc, "/config/database/port");
// port has value: ScalarValue{int64_t{5432}}

// List index access
auto first = am::json::get_pointer(doc, "/items/0");
// first has value: ScalarValue{int64_t{1}}

// Empty pointer returns the root
auto root_val = am::json::get_pointer(doc, "");
// root_val has value: ObjType::map

// Missing paths return nullopt
auto missing = am::json::get_pointer(doc, "/nonexistent/key");
// missing == std::nullopt
```

### Writing with put_pointer

`put_pointer` sets a scalar value at the given path. Intermediate map objects
are created automatically when they don't exist:

```cpp
// Set a value at a nested path
am::json::put_pointer(doc, "/config/database/port", am::ScalarValue{std::int64_t{3306}});

// Create intermediate maps automatically
am::json::put_pointer(doc, "/new/nested/path", am::ScalarValue{std::string{"hello"}});
// Creates /new (map), /new/nested (map), /new/nested/path = "hello"
```

### Deleting with delete_pointer

```cpp
am::json::delete_pointer(doc, "/config/database/host");
// The "host" key is removed from the database map

am::json::delete_pointer(doc, "/items/0");
// Removes the first element from the items list
```

### Pointer Escaping

Per RFC 6901, `~` and `/` in key names are escaped:

| Character | Escaped |
|-----------|---------|
| `~` | `~0` |
| `/` | `~1` |

```cpp
// A key named "a/b" is addressed as "/a~1b"
// A key named "m~n" is addressed as "/m~0n"
doc.transact([](auto& tx) {
    tx.put(am::root, "a/b", 42);
});
auto val = am::json::get_pointer(doc, "/a~1b");  // 42
```

### List Access

List elements are addressed by zero-based numeric index. The special segment
`-` refers to the position past the last element (append):

```cpp
auto doc = am::Document{};
am::json::import_json(doc, json{{"items", {10, 20, 30}}});

// Read by index
auto second = am::json::get_pointer(doc, "/items/1");  // 20

// Append to list with "-"
am::json::put_pointer(doc, "/items/-", am::ScalarValue{std::int64_t{40}});
// items is now [10, 20, 30, 40]
```

---

## JSON Patch (RFC 6902)

[JSON Patch](https://www.rfc-editor.org/rfc/rfc6902) is a format for describing
changes to a JSON document as a sequence of operations. automerge-cpp supports
applying patches and generating diffs.

### Applying a Patch

```cpp
am::json::apply_json_patch(doc, json::parse(R"([
    {"op": "add",     "path": "/tags/-",    "value": "json"},
    {"op": "replace", "path": "/version",   "value": "2.0.0"},
    {"op": "remove",  "path": "/deprecated"},
    {"op": "test",    "path": "/name",      "value": "automerge-cpp"}
])"));
```

### Supported Operations

| Operation | Description | Fields |
|-----------|-------------|--------|
| `add` | Add a value (or insert into a list) | `path`, `value` |
| `remove` | Remove a value | `path` |
| `replace` | Replace an existing value | `path`, `value` |
| `move` | Move a value from one path to another | `from`, `path` |
| `copy` | Copy a value from one path to another | `from`, `path` |
| `test` | Assert a value equals the expected | `path`, `value` |

**add** creates or replaces map keys, and inserts into lists:
```cpp
am::json::apply_json_patch(doc, json::parse(R"([
    {"op": "add", "path": "/name", "value": "new-name"},
    {"op": "add", "path": "/items/1", "value": "inserted"},
    {"op": "add", "path": "/items/-", "value": "appended"}
])"));
```

**move** relocates a value (remove from source, add at destination):
```cpp
am::json::apply_json_patch(doc, json::parse(R"([
    {"op": "move", "from": "/old_key", "path": "/new_key"}
])"));
```

**copy** duplicates a value (including nested objects):
```cpp
am::json::apply_json_patch(doc, json::parse(R"([
    {"op": "copy", "from": "/config", "path": "/config_backup"}
])"));
```

**test** asserts that a value matches, throwing `std::runtime_error` on mismatch:
```cpp
am::json::apply_json_patch(doc, json::parse(R"([
    {"op": "test", "path": "/version", "value": "2.0.0"}
])"));
// Throws if /version is not "2.0.0"
```

### Generating a Diff

`diff_json_patch` computes the RFC 6902 diff between two documents by exporting
both to JSON and using `nlohmann::json::diff()`:

```cpp
auto doc1 = am::Document{};
am::json::import_json(doc1, json{{"x", 1}, {"y", 2}});

auto doc2 = doc1.fork();
doc2.transact([](am::Transaction& tx) {
    tx.put(am::root, "x", std::int64_t{99});
    tx.delete_key(am::root, "y");
});

auto diff = am::json::diff_json_patch(doc1, doc2);
// [
//   {"op": "replace", "path": "/x", "value": 99},
//   {"op": "remove", "path": "/y"}
// ]
```

### Atomicity

All operations in a patch are applied within a single Automerge transaction.
If any operation fails (e.g., a `test` assertion), the entire patch is rolled back
and an exception is thrown:

```cpp
try {
    am::json::apply_json_patch(doc, json::parse(R"([
        {"op": "replace", "path": "/version", "value": "3.0.0"},
        {"op": "test", "path": "/name", "value": "wrong-name"}
    ])"));
} catch (const std::runtime_error& e) {
    // "test: value mismatch" — version was NOT changed to 3.0.0
}
```

---

## JSON Merge Patch (RFC 7386)

[JSON Merge Patch](https://www.rfc-editor.org/rfc/rfc7386) is a simpler
alternative to JSON Patch. It describes changes as a partial JSON document
where:

- **Present keys** with non-null values are set or replaced
- **Present keys** with `null` values are deleted
- **Missing keys** are left unchanged
- **Nested objects** are merged recursively

### Applying a Merge Patch

```cpp
auto doc = am::Document{};
am::json::import_json(doc, json{
    {"name", "Alice"},
    {"age", 30},
    {"config", {{"theme", "dark"}, {"lang", "en"}}}
});

am::json::apply_merge_patch(doc, json{
    {"age", 31},                   // replace
    {"email", "alice@example.com"}, // add
    {"config", {{"theme", "light"}}}, // merge into nested object
});

auto result = am::json::export_json(doc);
// {
//   "name": "Alice",          ← unchanged (not in patch)
//   "age": 31,                ← replaced
//   "email": "alice@...",     ← added
//   "config": {
//     "theme": "light",       ← replaced
//     "lang": "en"            ← unchanged (not in config patch)
//   }
// }
```

Deleting with `null`:

```cpp
am::json::apply_merge_patch(doc, json{
    {"email", nullptr},  // delete the "email" key
});
```

You can also apply a merge patch to a subtree:

```cpp
auto config_id = doc.get_obj_id(am::root, "config");
am::json::apply_merge_patch(doc, json{{"lang", "fr"}}, *config_id);
```

### Generating a Merge Patch

```cpp
auto doc1 = am::Document{};
am::json::import_json(doc1, json{{"x", 1}, {"y", 2}, {"z", 3}});

auto doc2 = doc1.fork();
doc2.transact([](am::Transaction& tx) {
    tx.put(am::root, "x", std::int64_t{99});
    tx.delete_key(am::root, "y");
});

auto patch = am::json::generate_merge_patch(doc1, doc2);
// {"x": 99, "y": null}
```

### Merge Patch vs JSON Patch

| | JSON Patch (RFC 6902) | Merge Patch (RFC 7386) |
|---|---|---|
| Format | Array of operations | Partial JSON document |
| Delete | `{"op": "remove", "path": "..."}` | `{"key": null}` |
| Move/Copy | Supported | Not supported |
| Test assertion | Supported | Not supported |
| Atomicity | All-or-nothing (via test) | Always succeeds |
| List operations | Insert at index, append | Replace entire array |

Use JSON Patch when you need move, copy, test, or list-level precision. Use
Merge Patch for simple partial updates.

---

## Flatten and Unflatten

Flatten converts a nested document into a flat `std::map` of JSON Pointer paths
to leaf values. Unflatten reverses the operation.

### Flattening a Document

```cpp
auto doc = am::Document{};
am::json::import_json(doc, json{
    {"name", "Alice"},
    {"scores", {10, 20, 30}},
    {"config", {{"theme", "dark"}}}
});

auto flat = am::json::flatten(doc);
for (const auto& [path, value] : flat) {
    std::printf("  %s = %s\n", path.c_str(), value.dump().c_str());
}
```

Output:
```
  /config/theme = "dark"
  /name = "Alice"
  /scores/0 = 10
  /scores/1 = 20
  /scores/2 = 30
```

Only leaf (scalar) values are included. Nested objects and lists are traversed
but don't appear as entries themselves. Paths are sorted lexicographically (they
are stored in a `std::map`). Special characters in keys are escaped per RFC 6901.

You can flatten a subtree:

```cpp
auto config_id = doc.get_obj_id(am::root, "config");
auto config_flat = am::json::flatten(doc, *config_id);
// {"/theme": "dark"}
```

### Unflattening into a Document

```cpp
auto flat = std::map<std::string, json>{
    {"/name", "Bob"},
    {"/settings/color", "blue"},
    {"/settings/size", 12},
};

auto doc = am::Document{};
am::json::unflatten(doc, flat);
auto result = am::json::export_json(doc);
// {"name": "Bob", "settings": {"color": "blue", "size": 12}}
```

Intermediate maps are created automatically. Flatten/unflatten round-trips
preserve the document structure:

```cpp
auto original = am::Document{};
am::json::import_json(original, json{{"a", {{"b", 1}}}, {"c", 2}});
auto flat = am::json::flatten(original);

auto restored = am::Document{};
am::json::unflatten(restored, flat);
assert(am::json::export_json(original) == am::json::export_json(restored));
```

---

## ADL Serialization

The `<automerge-cpp/json.hpp>` header provides `to_json`/`from_json` overloads
that let you use nlohmann/json's ADL (Argument-Dependent Lookup) serialization
with automerge types. This means you can write `json j = my_value;` directly.

### Scalar Types

```cpp
// Implicit conversion via ADL
json j1 = am::ScalarValue{std::int64_t{42}};     // 42
json j2 = am::ScalarValue{3.14};                  // 3.14
json j3 = am::ScalarValue{true};                  // true
json j4 = am::ScalarValue{std::string{"hello"}};  // "hello"
json j5 = am::ScalarValue{am::Null{}};            // null
```

### Tagged Format for Round-Trip Fidelity

Counter, Timestamp, and Bytes are not standard JSON types. To preserve type
information during round-trips, they use a tagged object format:

```cpp
// Counter → tagged object
json j = am::ScalarValue{am::Counter{42}};
// {"__type": "counter", "value": 42}

// Timestamp → tagged object
json j = am::ScalarValue{am::Timestamp{1706745600000}};
// {"__type": "timestamp", "value": 1706745600000}

// Bytes → tagged object with base64 value
json j = am::ScalarValue{am::Bytes{std::byte{0xDE}, std::byte{0xAD}}};
// {"__type": "bytes", "value": "3q0="}
```

The `from_json` function recognizes these tags and restores the correct type:

```cpp
auto sv = am::ScalarValue{};
am::from_json(json{{"__type", "counter"}, {"value", 42}}, sv);
// sv is now Counter{42}
```

This is distinct from `export_json`, which exports Counters as plain numbers
for JSON compatibility. Use ADL serialization when you need type-preserving
round-trips; use `export_json` when you need standard JSON output.

### Identity Types

`ActorId` and `ChangeHash` serialize as hex strings:

```cpp
auto actor = am::ActorId{};  // 16 bytes
json j = actor;
// "00000000000000000000000000000000"

auto actor2 = am::ActorId{};
am::from_json(j, actor2);  // round-trips correctly
```

`OpId` and `ObjId` serialize as structured objects:

```cpp
json j = am::OpId{.counter = 5, .actor = am::ActorId{}};
// {"counter": 5, "actor": "00000000000000000000000000000000"}

json j = am::root;  // ObjId for root
// "root"
```

### Compound Types

```cpp
// Change
auto changes = doc.get_changes();
json change_json = changes.back();
// {"actor": "...", "seq": 1, "start_op": 1, "timestamp": 0, "deps": [...], "ops": 3}

// Patch
auto [result, patches] = doc.transact_with_patches([](am::Transaction& tx) {
    tx.put(am::root, "key", 42);
});
json patch_json = patches.front();
// {"obj": "root", "key": "key", "action": {"type": "put", ...}}

// Mark
// {"start": 0, "end": 5, "name": "bold", "value": true}
```

### from_json Type Inference

When deserializing a `ScalarValue` from JSON without a `__type` tag, the type
is inferred from the JSON value:

| JSON Type | Resulting ScalarValue |
|-----------|----------------------|
| `null` | `Null{}` |
| `true`/`false` | `bool` |
| Integer (fits in int64) | `std::int64_t` |
| Integer (> int64 max) | `std::uint64_t` |
| Floating point | `double` |
| String | `std::string` |
| Object with `__type` | Counter, Timestamp, or Bytes |

---

## CRDT-Aware Workflows

The JSON integration works naturally with Automerge's CRDT features: forking,
merging, conflict resolution, and binary serialization.

### Fork, Merge, and Diff

A powerful pattern is to fork a document, make changes independently, then use
`diff_json_patch` to see what changed before merging:

```cpp
auto doc = am::Document{};
am::json::import_json(doc, json{
    {"name", "automerge-cpp"},
    {"stars", 42},
    {"active", true}
});

// Fork for Bob's changes
auto bob = doc.fork();
bob.transact([](am::Transaction& tx) {
    tx.put(am::root, "stars", std::int64_t{100});
    tx.put(am::root, "active", false);
});

// See what Bob changed (as RFC 6902 patch)
auto diff = am::json::diff_json_patch(doc, bob);
std::cout << diff.dump(2) << std::endl;
// [
//   {"op": "replace", "path": "/active", "value": false},
//   {"op": "replace", "path": "/stars", "value": 100}
// ]

// Merge Bob's changes — conflict-free
doc.merge(bob);
```

### Collaborative JSON Editing

Multiple actors can independently import, patch, and merge:

```cpp
// Alice creates the document
auto alice = am::Document{};
am::json::import_json(alice, json{{"title", "Meeting Notes"}, {"items", json::array()}});

// Bob forks and adds items
auto bob = alice.fork();
am::json::apply_json_patch(bob, json::parse(R"([
    {"op": "add", "path": "/items/-", "value": "Discuss roadmap"},
    {"op": "add", "path": "/items/-", "value": "Review PRs"}
])"));

// Alice adds items concurrently
am::json::apply_json_patch(alice, json::parse(R"([
    {"op": "add", "path": "/items/-", "value": "Plan sprint"}
])"));

// Merge — all items are preserved
alice.merge(bob);
auto result = am::json::export_json(alice);
// items contains all three entries (order depends on actor IDs)
```

### Save, Load, and Verify

JSON export is useful for verifying save/load round-trips:

```cpp
// Save to binary
auto bytes = doc.save();

// Load from binary
auto loaded = am::Document::load(bytes);

// Verify with JSON comparison
auto original_json = am::json::export_json(doc);
auto loaded_json = am::json::export_json(*loaded);
assert(original_json == loaded_json);
```

---

## Type Mapping Reference

### JSON to Automerge (import)

| JSON | Automerge |
|------|-----------|
| `{}` (object) | Map (recursive) |
| `[]` (array) | List (recursive) |
| `"string"` | `std::string` |
| `42` (integer) | `std::int64_t` |
| `1.5` (float) | `double` |
| `true`/`false` | `bool` |
| `null` | `Null` |
| `18446744073709551615` (> int64 max) | `std::uint64_t` |

### Automerge to JSON (export)

| Automerge | JSON |
|-----------|------|
| Map / Table | `{}` (object) |
| List | `[]` (array) |
| Text | `"string"` |
| `std::string` | `"string"` |
| `std::int64_t` | number |
| `std::uint64_t` | number |
| `double` | number |
| `bool` | `true`/`false` |
| `Null` | `null` |
| `Counter` | number (plain `.value`) |
| `Timestamp` | number (plain `.millis_since_epoch`) |
| `Bytes` | string (base64-encoded) |

### ADL Serialization (tagged format)

| Automerge | JSON (ADL `to_json`) |
|-----------|---------------------|
| `Counter{42}` | `{"__type": "counter", "value": 42}` |
| `Timestamp{1234}` | `{"__type": "timestamp", "value": 1234}` |
| `Bytes{0xDE, 0xAD}` | `{"__type": "bytes", "value": "3q0="}` |
| `ActorId` | `"0123456789abcdef..."` (32 hex chars) |
| `ChangeHash` | `"abcdef012345..."` (64 hex chars) |

---

## API Reference

Include `<automerge-cpp/json.hpp>`. Functions are split across two namespaces:

- **`automerge_cpp`** — ADL serialization (`to_json`/`from_json`), required for implicit conversion
- **`automerge_cpp::json`** — all other interop functions (import, export, pointer, patch, merge patch, flatten)

### Import/Export (`automerge_cpp::json`)

| Function | Description |
|----------|-------------|
| `export_json(doc, obj)` | Export document (or subtree) as JSON |
| `export_json_at(doc, heads, obj)` | Export at historical point |
| `import_json(doc, j, target)` | Import JSON into document (new transaction) |
| `import_json(tx, j, target)` | Import JSON within existing transaction |

### JSON Pointer (RFC 6901) (`automerge_cpp::json`)

| Function | Description |
|----------|-------------|
| `get_pointer(doc, ptr)` | Read value at pointer path |
| `put_pointer(doc, ptr, val)` | Write scalar at pointer path |
| `delete_pointer(doc, ptr)` | Delete value at pointer path |

### JSON Patch (RFC 6902) (`automerge_cpp::json`)

| Function | Description |
|----------|-------------|
| `apply_json_patch(doc, patch)` | Apply array of patch operations |
| `diff_json_patch(before, after)` | Generate diff between two documents |

### JSON Merge Patch (RFC 7386) (`automerge_cpp::json`)

| Function | Description |
|----------|-------------|
| `apply_merge_patch(doc, patch, target)` | Apply merge patch |
| `generate_merge_patch(before, after)` | Generate merge patch from diff |

### Flatten/Unflatten (`automerge_cpp::json`)

| Function | Description |
|----------|-------------|
| `flatten(doc, obj)` | Flatten to map of pointer paths |
| `unflatten(doc, flat, target)` | Unflatten from pointer path map |

### ADL Serialization (`automerge_cpp`)

| Function | Types |
|----------|-------|
| `to_json` | `Null`, `Counter`, `Timestamp`, `ScalarValue`, `ActorId`, `ChangeHash`, `OpId`, `ObjId`, `Change`, `Patch`, `Mark`, `Cursor` |
| `from_json` | `ScalarValue`, `ActorId`, `ChangeHash` |

### Child Object Lookup

| Function | Description |
|----------|-------------|
| `doc.get_obj_id(obj, key)` | Get ObjId for a map child |
| `doc.get_obj_id(obj, index)` | Get ObjId for a list child |

---

## Building

The JSON integration is built automatically when you build automerge-cpp.
No extra flags are needed.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release \
    -DAUTOMERGE_CPP_BUILD_TESTS=ON \
    -DAUTOMERGE_CPP_BUILD_EXAMPLES=ON
cmake --build build
```

Run the JSON interop demo:

```bash
./build/examples/json_interop_demo
```

Run the JSON tests (98 tests):

```bash
./build/tests/automerge_cpp_tests --gtest_filter="Json*:GetObjId*"
```
