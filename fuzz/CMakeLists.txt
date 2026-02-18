# Fuzz targets using libFuzzer (requires Clang).
# Enable with: cmake -DAUTOMERGE_CPP_BUILD_FUZZ=ON -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(WARNING "Fuzz targets require Clang (libFuzzer). Skipping.")
    return()
endif()

set(FUZZ_FLAGS -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)

# Internal headers live under src/ in the project root
set(FUZZ_INTERNAL_INCLUDE ${CMAKE_SOURCE_DIR})

# fuzz_load — exercises Document::load() + save() round-trip
add_executable(fuzz_load fuzz_load.cpp)
target_include_directories(fuzz_load PRIVATE ${FUZZ_INTERNAL_INCLUDE})
target_link_libraries(fuzz_load PRIVATE automerge-cpp::automerge-cpp ZLIB::ZLIB)
target_compile_options(fuzz_load PRIVATE ${FUZZ_FLAGS})
target_link_options(fuzz_load PRIVATE ${FUZZ_FLAGS})

# fuzz_leb128 — exercises LEB128 decode edge cases
add_executable(fuzz_leb128 fuzz_leb128.cpp)
target_include_directories(fuzz_leb128 PRIVATE ${FUZZ_INTERNAL_INCLUDE})
target_link_libraries(fuzz_leb128 PRIVATE automerge-cpp::automerge-cpp ZLIB::ZLIB)
target_compile_options(fuzz_leb128 PRIVATE ${FUZZ_FLAGS})
target_link_options(fuzz_leb128 PRIVATE ${FUZZ_FLAGS})

# fuzz_change_chunk — exercises columnar change chunk parsing
add_executable(fuzz_change_chunk fuzz_change_chunk.cpp)
target_include_directories(fuzz_change_chunk PRIVATE ${FUZZ_INTERNAL_INCLUDE})
target_link_libraries(fuzz_change_chunk PRIVATE automerge-cpp::automerge-cpp ZLIB::ZLIB)
target_compile_options(fuzz_change_chunk PRIVATE ${FUZZ_FLAGS})
target_link_options(fuzz_change_chunk PRIVATE ${FUZZ_FLAGS})

# generate_seeds — helper to create valid seed corpus files (not a fuzz target)
add_executable(generate_seeds generate_seeds.cpp)
target_link_libraries(generate_seeds PRIVATE automerge-cpp::automerge-cpp ZLIB::ZLIB)
