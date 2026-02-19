cmake_minimum_required(VERSION 3.28)

project(automerge-cpp
    VERSION 0.6.0
    DESCRIPTION "A modern C++23 implementation of Automerge"
    HOMEPAGE_URL "https://github.com/homer6/automerge-cpp"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Options
option(AUTOMERGE_CPP_BUILD_TESTS "Build unit tests" OFF)
option(AUTOMERGE_CPP_BUILD_EXAMPLES "Build example programs" OFF)
option(AUTOMERGE_CPP_BUILD_BENCHMARKS "Build benchmarks" OFF)
option(AUTOMERGE_CPP_BUILD_FUZZ "Build fuzz targets (requires Clang)" OFF)

# Dependencies
find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)

# nlohmann/json (header-only, from upstream submodule)
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install OFF CACHE INTERNAL "")
add_subdirectory(upstream/json)

# Library
add_library(automerge-cpp)
add_library(automerge-cpp::automerge-cpp ALIAS automerge-cpp)

target_include_directories(automerge-cpp
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(automerge-cpp PUBLIC cxx_std_23)

target_sources(automerge-cpp PRIVATE
    src/document.cpp
    src/transaction.cpp
    src/json.cpp
)

target_link_libraries(automerge-cpp
    PUBLIC Threads::Threads nlohmann_json::nlohmann_json
    PRIVATE ZLIB::ZLIB
)

# Subdirectories
if(AUTOMERGE_CPP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(AUTOMERGE_CPP_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(AUTOMERGE_CPP_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

if(AUTOMERGE_CPP_BUILD_FUZZ)
    add_subdirectory(fuzz)
endif()
