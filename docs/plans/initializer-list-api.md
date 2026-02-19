# Plan: Initializer List API (`List{}`, `Map{}`) for put/insert

## Context

The current API requires verbose `put_object` + manual `insert` loops to create populated
nested objects. The user wants nlohmann/json-style initializer list ergonomics:

```cpp
// Current (verbose)
auto items = tx.put_object(root, "items", ObjType::list);
tx.insert(items, 0, "Milk");
tx.insert(items, 1, "Eggs");
tx.insert(items, 2, "Bread");

// Desired (clean, nlohmann-style)
auto items = tx.put(root, "items", List{"Milk", "Eggs", "Bread"});
auto config = tx.put(root, "config", Map{{"port", 8080}, {"host", "localhost"}});
```

## Implementation

### 1. Add `List` and `Map` types to `include/automerge-cpp/value.hpp`

```cpp
/// Initializer for creating populated lists.
struct List {
    std::vector<ScalarValue> values;
    List() = default;
    List(std::initializer_list<ScalarValue> v) : values(v) {}
};

/// Initializer for creating populated maps.
struct Map {
    std::vector<std::pair<std::string, ScalarValue>> entries;
    Map() = default;
    Map(std::initializer_list<std::pair<std::string_view, ScalarValue>> e) {
        entries.reserve(e.size());
        for (const auto& [k, v] : e) entries.emplace_back(std::string{k}, v);
    }
};
```

Implicit conversions into `ScalarValue` handle native types:
- `"Milk"` (`const char*`) -> `std::string` alternative (one conversion via variant ctor)
- `8080` (`int`) -> `std::int64_t` alternative (only non-narrowing match in C++20+)
- `3.14` -> `double`, `true` -> `bool`, etc.

### 2. Add `put`/`insert` overloads to `include/automerge-cpp/transaction.hpp`

Four new overloads (inline, forwarding to existing methods):

```cpp
auto put(const ObjId& obj, std::string_view key, List init) -> ObjId;
auto put(const ObjId& obj, std::string_view key, Map init) -> ObjId;
auto insert(const ObjId& obj, std::size_t index, List init) -> ObjId;
auto insert(const ObjId& obj, std::size_t index, Map init) -> ObjId;
```

Each creates the object via `put_object`/`insert_object`, populates it, returns ObjId.

### 3. Tests (~8 new tests in `tests/document_test.cpp`)

- `put` with `List` (populated + empty)
- `put` with `Map` (populated + empty)
- `insert` with `List` into a list
- `insert` with `Map` into a list
- Mixed types in List: `List{1, "hello", 3.14, true}`
- Nested: create a List, then put items into the resulting ObjId

### 4. Update README quick example

```cpp
auto list_id = doc.transact([](am::Transaction& tx) {
    tx.put(am::root, "title", "Shopping List");
    tx.put(am::root, "count", 0);
    return tx.put(am::root, "items", am::List{"Milk", "Eggs", "Bread"});
});
```

### 5. Update `docs/api.md` with List/Map documentation

## Files to modify

| File | Change |
|------|--------|
| `include/automerge-cpp/value.hpp` | Add `List` and `Map` structs |
| `include/automerge-cpp/transaction.hpp` | Add 4 `put`/`insert` overloads |
| `tests/document_test.cpp` | Add ~8 tests |
| `README.md` | Update quick example + batch ops section |
| `docs/api.md` | Document List/Map types and usage |

## Verification

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```
