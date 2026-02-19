# Phase 12B–12G: nlohmann/json Interoperability Implementation

**Parent plan**: [nlohmann-json-interop.md](nlohmann-json-interop.md) (full Phase 12 design)
**Status**: In progress
**Depends on**: Phase 12A (Modern C++ API — complete, 485 tests passing)

---

## Context

Phase 12A is complete: scalar overloads, `List{}`/`Map{}` initializers,
`get<T>()`, `operator[]`, `get_path()`, `overload{}`, batch ops, STL container overloads.
All 485 tests pass. The existing `examples/json_interop_demo.cpp` has manual static helpers
for import/export but **cannot recursively export nested objects** because `Document` lacks
a public `get_obj_id()` method (it shows `"[nested object]"` placeholders).

This plan implements the remaining phases from the parent plan:
- **12B**: ADL serialization (`to_json`/`from_json` for all automerge types)
- **12C**: Document import/export (`export_json`, `import_json`, `Document::from_json`)
- **12D**: JSON Pointer (RFC 6901) — `get_pointer`, `put_pointer`, `delete_pointer`
- **12E**: JSON Patch (RFC 6902) — `apply_json_patch`, `diff_json_patch`
- **12F**: JSON Merge Patch (RFC 7386) + `flatten`/`unflatten`
- **12G**: Documentation updates

---

## Step 1: Add `get_obj_id` to Document (prerequisite for recursive export)

**File**: `include/automerge-cpp/document.hpp`

Add two public methods:

```cpp
auto get_obj_id(const ObjId& obj, std::string_view key) const -> std::optional<ObjId>;
auto get_obj_id(const ObjId& obj, std::size_t index) const -> std::optional<ObjId>;
```

**File**: `src/document.cpp`

Implement by delegating to existing `DocState` methods:
- `state_->get_obj_id_for_key(obj, std::string{key})` (already exists at `src/doc_state.hpp:280`)
- `state_->get_obj_id_for_index(obj, index)` (already exists at `src/doc_state.hpp:299`)

Both acquire the `read_guard()`.

## Step 2: Create `include/automerge-cpp/json.hpp` (public header)

New file. All interop declarations, guarded by `#pragma once`. Includes `<nlohmann/json.hpp>`.
Everything in `namespace automerge_cpp`.

### ADL serialization (Phase 12B)

Free functions found via ADL:

```cpp
// Scalar types
void to_json(nlohmann::json& j, Null);
void to_json(nlohmann::json& j, const Counter& c);
void to_json(nlohmann::json& j, const Timestamp& t);
void to_json(nlohmann::json& j, const ScalarValue& sv);
void from_json(const nlohmann::json& j, ScalarValue& sv);

// Identity types (hex string representation)
void to_json(nlohmann::json& j, const ActorId& id);
void from_json(const nlohmann::json& j, ActorId& id);
void to_json(nlohmann::json& j, const ChangeHash& h);
void from_json(const nlohmann::json& j, ChangeHash& h);
void to_json(nlohmann::json& j, const OpId& id);
void to_json(nlohmann::json& j, const ObjId& id);

// Compound types
void to_json(nlohmann::json& j, const Change& c);
void to_json(nlohmann::json& j, const Patch& p);
void to_json(nlohmann::json& j, const Mark& m);
```

**Tagged format for round-trip fidelity** (Counter/Timestamp are not plain JSON numbers):
- `Counter{42}` -> `{"__type": "counter", "value": 42}`
- `Timestamp{1234567890}` -> `{"__type": "timestamp", "value": 1234567890}`
- `Bytes{...}` -> `{"__type": "bytes", "value": "<base64>"}`
- All other scalars map naturally (Null->null, bool->boolean, int64->number, etc.)

`from_json` for `ScalarValue`: check for `__type` tag first, then infer from JSON type.

### Document export/import (Phase 12C)

```cpp
auto export_json(const Document& doc, const ObjId& obj = root) -> nlohmann::json;
auto export_json_at(const Document& doc, const std::vector<ChangeHash>& heads,
                    const ObjId& obj = root) -> nlohmann::json;

void import_json(Document& doc, const nlohmann::json& j, const ObjId& target = root);
void import_json(Transaction& tx, const nlohmann::json& j, const ObjId& target = root);
static auto Document::from_json(const nlohmann::json& j) -> Document;
```

Export walks the tree recursively using `get_obj_id()` + `object_type()` + `keys()`/`length()`.
Import walks the JSON recursively, calling `put`/`put_object`/`insert`/`insert_object`.

### JSON Pointer (Phase 12D)

```cpp
auto get_pointer(const Document& doc, std::string_view pointer) -> std::optional<Value>;
void put_pointer(Document& doc, std::string_view pointer, ScalarValue val);
void delete_pointer(Document& doc, std::string_view pointer);
```

Note: `put_pointer`/`delete_pointer` take `Document&` (not `Transaction&`) — they wrap
in `doc.transact()` internally for a clean API. The pointer is parsed by splitting on `/`
with `~0`->`~` and `~1`->`/` unescaping per RFC 6901.

### JSON Patch (Phase 12E)

```cpp
void apply_json_patch(Document& doc, const nlohmann::json& patch);
auto diff_json_patch(const Document& before, const Document& after) -> nlohmann::json;
```

`apply_json_patch` runs all ops in a single transaction. Supports: add, remove, replace,
move, copy, test. Uses `export_json` + `nlohmann::json::diff()` for `diff_json_patch`.

### JSON Merge Patch (Phase 12F)

```cpp
void apply_merge_patch(Document& doc, const nlohmann::json& patch,
                       const ObjId& target = root);
auto generate_merge_patch(const Document& before, const Document& after) -> nlohmann::json;
```

### Flatten/Unflatten (Phase 12F)

```cpp
auto flatten(const Document& doc, const ObjId& obj = root)
    -> std::map<std::string, nlohmann::json>;
void unflatten(Document& doc, const std::map<std::string, nlohmann::json>& flat,
               const ObjId& target = root);
```

Returns map of JSON Pointer paths -> json values.

## Step 3: Create `src/json.cpp` (implementation)

All function bodies. Key implementation details:

- **`export_json`**: Recursive. For each key/index, call `doc.get()`. If result is `ObjType`,
  call `doc.get_obj_id()` to get the child `ObjId`, then check `doc.object_type()` and recurse.
  Text objects -> `doc.text()`. Map/table -> JSON object. List -> JSON array.

- **`import_json`**: Recursive within a transaction. JSON objects -> `put_object(map)` + recurse.
  JSON arrays -> `put_object(list)` + recurse. Scalars -> `put`/`insert`.

- **JSON Pointer parsing**: Split on `/`, skip leading empty segment, unescape `~1`->`/` then
  `~0`->`~`. Walk: for each segment, `doc.get()` to check if object, `doc.get_obj_id()` to
  descend. Numeric segments on lists -> `std::size_t`, `-` means append (list end).

- **`apply_json_patch`**: Single `doc.transact()`. For each op in the array, parse `op`, `path`,
  `value`, `from`. Dispatch to put_pointer/delete_pointer/get_pointer logic within the tx.

- **`diff_json_patch`**: `auto j1 = export_json(before); auto j2 = export_json(after);
  return nlohmann::json::diff(j1, j2);`

- **`apply_merge_patch`**: RFC 7386 algorithm: if patch is object, iterate keys — null values
  delete, object values recurse, others put. Non-object patches replace entirely.

- **`flatten`**: Recursive DFS, emit leaf values with their JSON Pointer path.

- **`unflatten`**: For each path, parse pointer segments, create intermediate maps/lists as needed.

## Step 4: Update CMakeLists.txt

**File**: `CMakeLists.txt`

- Add `src/json.cpp` to `target_sources`
- Add `nlohmann_json::nlohmann_json` as PUBLIC link dependency (already have
  `add_subdirectory(upstream/json)` from earlier work)

```cmake
target_sources(automerge-cpp PRIVATE
    src/document.cpp
    src/transaction.cpp
    src/json.cpp
)

target_link_libraries(automerge-cpp
    PUBLIC Threads::Threads nlohmann_json::nlohmann_json
    PRIVATE ZLIB::ZLIB
)
```

## Step 5: Create `tests/json_test.cpp`

**File**: `tests/json_test.cpp` (~100-130 tests)

**File**: `tests/CMakeLists.txt` — add `json_test.cpp` to test executable, add
`nlohmann_json::nlohmann_json` to test link libraries.

Test groups:

**ADL Serialization (~25 tests)**:
- `to_json` / `from_json` round-trip for each ScalarValue alternative
- Counter/Timestamp tagged format verification
- ActorId/ChangeHash hex round-trip
- OpId, ObjId, Change, Patch, Mark serialization

**Document Export (~15 tests)**:
- Export flat map, nested map, list, mixed types
- Export text object as string
- Export empty document
- Export after merge (conflict resolved)
- `export_json_at` historical export

**Document Import (~15 tests)**:
- Import flat JSON, nested objects, arrays
- `Document::from_json` static factory
- Round-trip: JSON -> import -> export -> compare to original
- Import with Transaction overload

**JSON Pointer (~15 tests)**:
- `get_pointer` for map keys, list indices, nested paths
- `get_pointer` with escaped chars (`~0`, `~1`)
- `get_pointer` empty string = root, "/" = root key ""
- `put_pointer` create intermediate maps
- `put_pointer` append to list with `-`
- `delete_pointer` map key, list index
- Out-of-bounds / missing paths return nullopt or throw

**JSON Patch (~20 tests)**:
- `add` to map, `add` to list, `add` with `-` append
- `remove` from map, `remove` from list
- `replace` existing key
- `move` between paths
- `copy` between paths
- `test` passes / fails
- Atomic: failure rolls back all ops
- `diff_json_patch` generates correct diff

**JSON Merge Patch (~10 tests)**:
- Set/replace scalar values
- Delete with null
- Recursive merge into nested objects
- Idempotent application
- `generate_merge_patch` between two states

**Flatten/Unflatten (~10 tests)**:
- Flatten nested document produces correct pointer paths
- Flatten list indices
- Unflatten from flat map recreates nested structure
- Round-trip: flatten -> unflatten

**get_obj_id (~5 tests)**:
- Get ObjId for map child, list child
- Returns nullopt for non-existent key/index
- Returns nullopt for scalar (not object) values

## Step 6: Rewrite `examples/json_interop_demo.cpp`

Replace the manual static helper functions with calls to the new library API:
- `import_json(doc, j)` instead of the manual `import_json_value` recursion
- `export_json(doc)` instead of `export_json_flat(doc)`
- Add `Document::from_json({...})` usage
- Add JSON Pointer access: `get_pointer(doc, "/config/port")`
- Add JSON Patch: `apply_json_patch(doc, patch_ops)`
- Add JSON Merge Patch: `apply_merge_patch(doc, partial)`
- Add flatten demo

## Step 7: Documentation (Phase 12G)

- **`docs/api.md`**: Add sections for json.hpp functions (export_json, import_json,
  get_pointer, apply_json_patch, apply_merge_patch, flatten, ADL serialization)
- **`CLAUDE.md`**: Update test counts, add json.hpp to file tree, add json_test.cpp
  to test table
- **`docs/plans/roadmap.md`**: Mark Phase 12B-12G as complete

---

## Files to modify/create

| File | Action | Change |
|------|--------|--------|
| `include/automerge-cpp/document.hpp` | MOD | Add `get_obj_id` (2 overloads), `from_json` static factory |
| `include/automerge-cpp/json.hpp` | NEW | All interop declarations |
| `src/document.cpp` | MOD | Implement `get_obj_id` (delegating to DocState) |
| `src/json.cpp` | NEW | All interop implementations |
| `CMakeLists.txt` | MOD | Add `src/json.cpp`, link nlohmann_json PUBLIC |
| `tests/json_test.cpp` | NEW | ~100-130 tests |
| `tests/CMakeLists.txt` | MOD | Add json_test.cpp, link nlohmann_json |
| `examples/json_interop_demo.cpp` | MOD | Rewrite to use library API |
| `examples/CMakeLists.txt` | MOD | Remove manual nlohmann link (now PUBLIC) |
| `docs/api.md` | MOD | Document all new APIs |
| `CLAUDE.md` | MOD | Update file tree, test counts |

## Existing code to reuse

| Code | Location |
|------|----------|
| `DocState::get_obj_id_for_key()` | `src/doc_state.hpp:280` |
| `DocState::get_obj_id_for_index()` | `src/doc_state.hpp:299` |
| `DocState::get_obj_id_at()` | `src/doc_state.hpp:313` |
| `Document::read_guard()` | Thread-safe read pattern |
| `Document::transact()` | Wrapping writes |
| `nlohmann::json::diff()` | Built-in RFC 6902 diff generation |
| `upstream/json` submodule | Already added and linked |

## Verification

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug \
    -DAUTOMERGE_CPP_BUILD_TESTS=ON \
    -DAUTOMERGE_CPP_BUILD_EXAMPLES=ON
cmake --build build
ctest --test-dir build --output-on-failure

# Run json_interop_demo to verify end-to-end
./build/examples/json_interop_demo
```
