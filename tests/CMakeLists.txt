include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
)
FetchContent_MakeAvailable(googletest)

add_executable(automerge_cpp_tests
    test_main.cpp
    error_test.cpp
    types_test.cpp
    value_test.cpp
    op_test.cpp
    change_test.cpp
    document_test.cpp
    json_test.cpp
    leb128_test.cpp
    rle_test.cpp
    delta_encoder_test.cpp
    sha256_test.cpp
    column_spec_test.cpp
    chunk_test.cpp
    change_op_columns_test.cpp
    change_hash_fixture_test.cpp
    sync_wire_format_test.cpp
)

target_link_libraries(automerge_cpp_tests
    PRIVATE
        automerge-cpp::automerge-cpp
        GTest::gtest
        ZLIB::ZLIB
)

target_compile_definitions(automerge_cpp_tests
    PRIVATE
        AUTOMERGE_CPP_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

include(GoogleTest)
gtest_discover_tests(automerge_cpp_tests)

add_executable(automerge_cpp_fixture_writer fixture_writer.cpp)
target_link_libraries(automerge_cpp_fixture_writer PRIVATE automerge-cpp::automerge-cpp)

set(AUTOMERGE_CPP_REVERSE_FIXTURE_DIR "${CMAKE_CURRENT_BINARY_DIR}/reverse-fixtures")
add_test(
    NAME automerge_cpp_write_reverse_fixtures
    COMMAND automerge_cpp_fixture_writer "${AUTOMERGE_CPP_REVERSE_FIXTURE_DIR}"
)
set_tests_properties(
    automerge_cpp_write_reverse_fixtures
    PROPERTIES LABELS compatibility
)

find_program(AUTOMERGE_CPP_CARGO_EXECUTABLE cargo)
if(AUTOMERGE_CPP_CARGO_EXECUTABLE)
    add_test(
        NAME automerge_cpp_verify_reverse_fixtures
        COMMAND
            "${CMAKE_COMMAND}" -E env
            "CARGO_TARGET_DIR=${CMAKE_BINARY_DIR}/cargo-target"
            "${AUTOMERGE_CPP_CARGO_EXECUTABLE}" run --quiet
            --manifest-path "${CMAKE_SOURCE_DIR}/tests/fixtures/generator/Cargo.toml"
            -- verify-cpp "${AUTOMERGE_CPP_REVERSE_FIXTURE_DIR}"
    )
    set_tests_properties(
        automerge_cpp_verify_reverse_fixtures
        PROPERTIES
            DEPENDS automerge_cpp_write_reverse_fixtures
            LABELS "compatibility;cargo"
    )
endif()
