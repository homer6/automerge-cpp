# Patches from remote changes

Remote changes can update a document through change application, merge, sync,
or loading. Each entry point has a patch-returning variant:

```cpp
auto applied = document.apply_changes_with_patches(changes);
auto merged = document.merge_with_patches(other);
auto received = document.receive_sync_message_with_patches(sync_state, message);

auto loaded = Document{};
auto materialized = Document::load_with_patches(bytes, loaded);
```

The return type is `std::expected<std::vector<Patch>, Error>`. The static load
function writes the loaded document to its `Document&` argument only after the
input is valid. Its patches describe the transition from an empty document to
the loaded visible state.

## Visible transition semantics

Patches describe the visible state immediately before and after the call, not
just the operations supplied as arguments. This distinction matters when a
change was already waiting for a missing dependency: the call that supplies
the dependency returns patches for both the dependency and every queued change
that becomes applicable.

Map updates return the winning value in `PatchPut`. Its `conflict` member is
true whenever concurrent values remain at that property, matching the local
patch representation. Conflict creation or resolution produces a put patch
even when the winning value itself is unchanged. Counter-only changes use
`PatchIncrement`.

Sequence differences use `PatchInsert`, `PatchDelete`, and `PatchPut`. Text
differences use `PatchSpliceText`. A call that changes history or the pending
queue without changing visible data returns an empty vector.

Validation failures return `Error` and preserve the document. The original
`apply_changes`, `merge`, `receive_sync_message`, and `load` methods remain the
non-observing paths. They do not copy visible state or calculate a diff.

## Verification

Run the focused tests and paired application benchmarks:

```bash
./build/tests/automerge_cpp_tests --gtest_filter='Document.*with_patches*'
./build/benchmarks/automerge_cpp_benchmarks \
    --benchmark_filter='bm_apply_changes_(without|with)_patches'
```

Run the complete suite before committing:

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```

## Changing patch generation

Patch generation is implemented as a visible-object diff in
`src/document.cpp`. Keep it behind the patch-returning methods so callers of
the original methods do not pay observation costs. Add a transition test for
every new patch action or conflict rule, and keep load patches sufficient to
materialize all visible map, list, and text content.
