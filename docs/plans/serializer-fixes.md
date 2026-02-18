# Fix Serializer Limitations: Complete Actor Table & insert_object Round-Trip

## Context

Three serializer limitations were discovered during test coverage parity work.
All three violate the style guide principle "Make illegal states unrepresentable"
— the actor table can exist in an incomplete state, and `insert_object` ops
lose their `ObjType` identity during save/load. This causes `Document::load()`
to return `nullopt` for documents containing:
- `increment` ops targeting nested objects (actors missing from table)
- `insert_object` into lists (ObjType decoded as scalar uint64)
- Scalar-vs-ObjType conflicts after fork+merge (combination of both issues)

## Changes

### 1. Complete the actor table — `src/doc_state.hpp`

**Lines 571-575** (inner loop of `ensure_actor_table()`): After extracting
`op.id.actor`, also extract actors from `op.obj`, `op.insert_after`, and
`op.pred`. These are the exact fields that `find_actor_idx()` looks up during
encoding (`change_op_columns.hpp` lines 112, 125, 134, 158).

```cpp
for (const auto& op : change.operations) {
    if (cached_actor_set_.insert(op.id.actor).second)
        cached_actor_table_.push_back(op.id.actor);
    // op.obj
    if (!op.obj.is_root()) {
        const auto& obj_op = std::get<OpId>(op.obj.inner);
        if (cached_actor_set_.insert(obj_op.actor).second)
            cached_actor_table_.push_back(obj_op.actor);
    }
    // op.insert_after
    if (op.insert_after) {
        if (cached_actor_set_.insert(op.insert_after->actor).second)
            cached_actor_table_.push_back(op.insert_after->actor);
    }
    // op.pred
    for (const auto& p : op.pred) {
        if (cached_actor_set_.insert(p.actor).second)
            cached_actor_table_.push_back(p.actor);
    }
}
```

### 2. Fix encoder — `src/storage/columns/change_op_columns.hpp`

**Lines 65-67** (`op_to_action_code`): For `OpType::insert`, check if value
holds `ObjType` and return action code 0 (map/table) or 2 (list/text) instead
of always returning 1.

```cpp
case OpType::insert: {
    if (const auto* ot = std::get_if<ObjType>(&op.value)) {
        return (*ot == ObjType::list || *ot == ObjType::text) ? 2 : 0;
    }
    return 1;
}
case OpType::splice_text:
    return 1;
```

### 3. Fix decoder — `src/storage/columns/change_op_columns.hpp`

**Lines 335-343** (the `if (is_insert)` block): Check action_code to recover
ObjType for insert_object ops, using the same recovery logic as the existing
`case 0:` / `case 2:` blocks.

```cpp
if (is_insert) {
    if (action_code == 0 || action_code == 2) {
        // insert_object: recover ObjType from encoded uint value
        op.action = OpType::insert;
        if (const auto* sv = std::get_if<ScalarValue>(&*value)) {
            if (const auto* v = std::get_if<std::uint64_t>(sv)) {
                op.value = Value{static_cast<ObjType>(*v)};
            } else {
                op.value = Value{action_code == 0 ? ObjType::map : ObjType::list};
            }
        } else {
            op.value = Value{action_code == 0 ? ObjType::map : ObjType::list};
        }
    } else {
        op.action = OpType::insert;
        op.value = *value;
        if (const auto* sv = std::get_if<ScalarValue>(&op.value)) {
            if (std::holds_alternative<std::string>(*sv)) {
                op.action = OpType::splice_text;
            }
        }
    }
}
```

### 4. New columnar tests — `tests/change_op_columns_test.cpp`

- `insert_object_map_round_trip` — insert op with `Value{ObjType::map}` round-trips
- `insert_object_list_round_trip` — insert op with `Value{ObjType::list}` round-trips
- `multi_actor_op_fields_round_trip` — op with actors in obj, pred fields encodes/decodes

### 5. New integration tests — `tests/document_test.cpp`

- `insert_object_into_list_survives_save_load` — insert_object + put into nested map + save/load
- `counter_in_nested_map_survives_save_load` — put_object + counter + increment + save/load
- `scalar_vs_objtype_conflict_survives_save_load` — fork + scalar/ObjType conflict + save/load

### 6. Restore workaround tests to proper form

These tests were simplified to avoid the serializer bugs. Restore them:
- `counter_with_multiple_increments_survives_save_load` -> `counter_in_nested_map_survives_save_load` (use nested map with increment)
- `save_restore_complex_with_conflicts` -> restore the nested-object conflict version

### 7. Update plan doc and CLAUDE.md

Update `docs/plans/test-coverage-parity.md` to remove "Known Serializer Limitations" section.
Update CLAUDE.md test counts.

## Files Modified

| File | Change |
|------|--------|
| `src/doc_state.hpp` | Complete actor table in `ensure_actor_table()` |
| `src/storage/columns/change_op_columns.hpp` | Fix encoder + decoder for insert_object |
| `tests/change_op_columns_test.cpp` | 3 new columnar round-trip tests |
| `tests/document_test.cpp` | 3 new integration tests + 2 restored tests |
| `docs/plans/test-coverage-parity.md` | Remove limitations section |

## Verification

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```

All existing 347 tests + ~6 new tests must pass. No regressions.
