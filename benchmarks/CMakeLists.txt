include(FetchContent)

FetchContent_Declare(
    googlebenchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG v1.9.1
)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googlebenchmark)

add_executable(automerge_cpp_benchmarks
    placeholder_benchmark.cpp
)

target_link_libraries(automerge_cpp_benchmarks
    PRIVATE
        automerge-cpp::automerge-cpp
        benchmark::benchmark
        benchmark::benchmark_main
)
