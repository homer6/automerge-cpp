include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
)
FetchContent_MakeAvailable(googletest)

add_executable(automerge_cpp_tests
    error_test.cpp
    types_test.cpp
    value_test.cpp
    op_test.cpp
    change_test.cpp
    document_test.cpp
    leb128_test.cpp
)

target_link_libraries(automerge_cpp_tests
    PRIVATE
        automerge-cpp::automerge-cpp
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(automerge_cpp_tests)
