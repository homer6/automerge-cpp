# Phase 9: Fuzz Testing, ASan, clang-tidy

## Context

Phases 0-8 of automerge-cpp are complete (274 tests). Phase 9 adds three quality/safety layers:
1. **Fuzz testing** — libFuzzer targets for deserialization paths
2. **ASan/UBSan** — AddressSanitizer + UndefinedBehaviorSanitizer CI job running all 274 tests
3. **clang-tidy** — static analysis configuration and CI job

All three catch different classes of bugs: fuzzing finds crash bugs in parsing, ASan finds memory errors in normal test paths, clang-tidy catches code quality issues statically.

---

## 9a: Fuzz Testing

### New files

| File | Purpose |
|------|---------|
| `fuzz/CMakeLists.txt` | libFuzzer build config: `-fsanitize=fuzzer,address,undefined` |
| `fuzz/fuzz_load.cpp` | `LLVMFuzzerTestOneInput` targeting `Document::load()` |
| `fuzz/fuzz_leb128.cpp` | Fuzz `decode_uleb128()` and `decode_sleb128()` |
| `fuzz/fuzz_change_chunk.cpp` | Fuzz `parse_change_chunk()` with random actor tables |
| `fuzz/corpus/` | Seed corpus: 8 upstream crasher files + generated valid documents |

### Modified files

| File | Change |
|------|--------|
| `CMakeLists.txt` | Add `option(AUTOMERGE_CPP_BUILD_FUZZ "Build fuzz targets" OFF)` + `add_subdirectory(fuzz)` |

### Fuzz target details

**fuzz_load.cpp** — primary target, exercises the full deserialization stack:
```cpp
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    auto span = std::span<const std::byte>(reinterpret_cast<const std::byte*>(data), size);
    auto doc = automerge_cpp::Document::load(span);
    if (doc) {
        auto saved = doc->save();  // round-trip if parse succeeds
    }
    return 0;
}
```

**fuzz_leb128.cpp** — targeted at codec edge cases (overflow, truncation):
```cpp
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    auto span = std::span<const std::byte>(reinterpret_cast<const std::byte*>(data), size);
    automerge_cpp::encoding::decode_uleb128(span);
    automerge_cpp::encoding::decode_sleb128(span);
    return 0;
}
```

**fuzz_change_chunk.cpp** — targeted at columnar parsing with a plausible actor table:
```cpp
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    auto span = std::span<const std::byte>(reinterpret_cast<const std::byte*>(data), size);
    auto actor = automerge_cpp::ActorId{};  // zero actor
    auto actors = std::vector<automerge_cpp::ActorId>{actor};
    automerge_cpp::storage::parse_change_chunk(span, actors);
    return 0;
}
```

### Seed corpus

Copy the 8 upstream crasher files from `upstream/automerge/rust/automerge/tests/fuzz-crashers/` into `fuzz/corpus/`:
- `action-is-48.automerge` (58 bytes)
- `crash-da39a3ee5e6b4b0d3255bfef95601890afd80709` (10 bytes)
- `incorrect_max_op.automerge` (126 bytes)
- `invalid_deflate_stream.automerge` (123 bytes)
- `missing_actor.automerge` (126 bytes)
- `overflow_in_length.automerge` (182 bytes)
- `too_many_deps.automerge` (134 bytes)
- `too_many_ops.automerge` (134 bytes)

### fuzz/CMakeLists.txt structure

```cmake
# Require Clang for libFuzzer
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(WARNING "Fuzz targets require Clang (libFuzzer). Skipping.")
    return()
endif()

set(FUZZ_FLAGS "-fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer")

add_executable(fuzz_load fuzz_load.cpp)
target_link_libraries(fuzz_load PRIVATE automerge-cpp::automerge-cpp ZLIB::ZLIB)
target_compile_options(fuzz_load PRIVATE ${FUZZ_FLAGS})
target_link_options(fuzz_load PRIVATE ${FUZZ_FLAGS})

# ... same pattern for fuzz_leb128, fuzz_change_chunk
```

---

## 9b: ASan/UBSan CI Job

### Modified files

| File | Change |
|------|--------|
| `.github/workflows/linux.yml` | Add `sanitizers` job: Clang + `-fsanitize=address,undefined` running all 274 tests |

### CI job

```yaml
  sanitizers:
    name: ASan + UBSan
    runs-on: ubuntu-latest
    steps:
    - uses: actions/checkout@v4
      with:
        submodules: recursive
    - name: Configure
      run: >
        cmake -B build -DCMAKE_BUILD_TYPE=Debug
        -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
        -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
        -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
        -DAUTOMERGE_CPP_BUILD_TESTS=ON
    - name: Build
      run: cmake --build build
    - name: Test
      run: ctest --test-dir build --output-on-failure
```

Key: Debug build (unoptimized) with ASan+UBSan flags passed to both compile and link. Runs the full test suite under sanitizers.

---

## 9c: clang-tidy

### New files

| File | Purpose |
|------|---------|
| `.clang-tidy` | Project-wide clang-tidy configuration |

### Modified files

| File | Change |
|------|--------|
| `.github/workflows/linux.yml` | Add `clang-tidy` job |

### .clang-tidy configuration

Conservative set of checks appropriate for a C++23 codebase. Avoid noisy checks, focus on bug-finding:

```yaml
Checks: >
  -*,
  bugprone-*,
  -bugprone-easily-swappable-parameters,
  -bugprone-exception-escape,
  clang-analyzer-*,
  cppcoreguidelines-init-variables,
  cppcoreguidelines-slicing,
  misc-redundant-expression,
  misc-unused-using-decls,
  modernize-use-nullptr,
  modernize-use-override,
  performance-*,
  -performance-avoid-endl,
  readability-braces-around-statements,
  readability-misleading-indentation,
  readability-redundant-smartptr-get
WarningsAsErrors: ''
HeaderFilterRegex: 'include/automerge-cpp/.*|src/.*'
```

### CI job

```yaml
  clang-tidy:
    name: clang-tidy
    runs-on: ubuntu-latest
    steps:
    - uses: actions/checkout@v4
      with:
        submodules: recursive
    - name: Configure
      run: >
        cmake -B build
        -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
        -DAUTOMERGE_CPP_BUILD_TESTS=OFF
    - name: Run clang-tidy
      run: >
        find src include -name '*.cpp' -o -name '*.hpp' |
        xargs clang-tidy -p build --warnings-as-errors='*'
```

Note: `WarningsAsErrors` in .clang-tidy is empty (allows local runs to see warnings without failing). The CI job uses `--warnings-as-errors='*'` to enforce zero warnings in CI.

---

## Implementation Order

```
9a: Fuzz targets + seed corpus + CMake option
    ↓
9b: ASan/UBSan CI job in linux.yml
    ↓
9c: .clang-tidy config + CI job
    ↓
Fix any clang-tidy findings in src/ and include/
    ↓
Update CLAUDE.md, README.md, docs/plans/roadmap.md
```

## Verification

After 9a:
- `cmake -B build-fuzz -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DAUTOMERGE_CPP_BUILD_FUZZ=ON && cmake --build build-fuzz`
- `./build-fuzz/fuzz/fuzz_load fuzz/corpus/ -max_total_time=60` — 60 seconds with no crashes
- All upstream crasher files are handled gracefully (return nullopt, no crash)

After 9b:
- Local ASan build: `cmake -B build-asan -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined" -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DAUTOMERGE_CPP_BUILD_TESTS=ON && cmake --build build-asan && ctest --test-dir build-asan --output-on-failure`
- All 274 tests pass under ASan+UBSan

After 9c:
- `clang-tidy -p build src/document.cpp src/transaction.cpp` — no errors
- Fix any findings before committing

After all:
- Normal build + all 274 tests still pass
- CLAUDE.md, README.md, roadmap updated
