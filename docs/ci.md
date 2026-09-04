# Continuous integration

The hosted workflows compile the library and run its tests on Linux, macOS, Windows, and
FreeBSD. Static analysis and sanitizer builds run on Linux, and Doxygen publishes the API
reference.

## Hosted toolchains

| Job | Hosted environment | Toolchain |
|---|---|---|
| Linux GCC | Ubuntu 24.04 | GCC 14 from Ubuntu packages |
| Linux Clang | Ubuntu 24.04 | Clang 19 from apt.llvm.org |
| ASan + UBSan | Ubuntu 24.04 | Clang 19 from apt.llvm.org |
| clang-tidy | Ubuntu 24.04 | clang-tidy 19 from apt.llvm.org |
| macOS | `macos-latest` | Runner-provided Apple Clang |
| Windows | `windows-latest` | Runner-provided MSVC and zlib from vcpkg |
| FreeBSD | FreeBSD 15.1 VM | Base-system Clang 19 |
| GitHub Pages | Ubuntu 24.04 | Ubuntu Doxygen package |

The Linux Clang jobs use libstdc++ from Ubuntu, matching the project's existing Linux Clang
configuration. The clang-tidy job reads the deliberate checks in `.clang-tidy`, analyzes the
three translation units under `src/`, and follows project headers under `src/` and `include/`.
Its header filter excludes `upstream/` and other third-party headers.

## Linux jobs in Docker

Run the GCC job from the repository root with the checkout and its submodules already present:

```bash
docker run --rm -v "$PWD:/workspace" -w /workspace ubuntu:24.04 bash -lc '
  apt-get update &&
  apt-get install -y cmake g++-14 git zlib1g-dev &&
  cmake -B build-gcc -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=gcc-14 -DCMAKE_CXX_COMPILER=g++-14 \
    -DAUTOMERGE_CPP_BUILD_TESTS=ON \
    -DAUTOMERGE_CPP_BUILD_EXAMPLES=ON \
    -DAUTOMERGE_CPP_BUILD_BENCHMARKS=ON &&
  cmake --build build-gcc --config Release &&
  ctest --test-dir build-gcc -C Release --output-on-failure
'
```

Install Clang 19 from apt.llvm.org in the same Ubuntu image, then run the Clang job:

```bash
docker run --rm -v "$PWD:/workspace" -w /workspace ubuntu:24.04 bash -lc '
  apt-get update &&
  apt-get install -y cmake git wget zlib1g-dev lsb-release software-properties-common gnupg &&
  wget -qO /tmp/llvm.sh https://apt.llvm.org/llvm.sh &&
  chmod +x /tmp/llvm.sh &&
  /tmp/llvm.sh 19 &&
  cmake -B build-clang -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=clang-19 -DCMAKE_CXX_COMPILER=clang++-19 \
    -DAUTOMERGE_CPP_BUILD_TESTS=ON \
    -DAUTOMERGE_CPP_BUILD_EXAMPLES=ON \
    -DAUTOMERGE_CPP_BUILD_BENCHMARKS=ON &&
  cmake --build build-clang --config Release &&
  ctest --test-dir build-clang -C Release --output-on-failure
'
```

The sanitizer job uses that Clang installation with these commands:

```bash
cmake -B build-asan -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=clang-19 -DCMAKE_CXX_COMPILER=clang++-19 \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined" \
  -DAUTOMERGE_CPP_BUILD_TESTS=ON
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

The static-analysis job uses the same installation plus `clang-tidy-19`:

```bash
cmake -B build-tidy \
  -DCMAKE_C_COMPILER=clang-19 -DCMAKE_CXX_COMPILER=clang++-19 \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DAUTOMERGE_CPP_BUILD_TESTS=OFF
clang-tidy-19 -p build-tidy \
  src/document.cpp \
  src/transaction.cpp \
  src/json.cpp
```

## macOS

Use the Apple Clang and zlib supplied by macOS:

```bash
cmake -B build-macos -DCMAKE_BUILD_TYPE=Release \
  -DAUTOMERGE_CPP_BUILD_TESTS=ON \
  -DAUTOMERGE_CPP_BUILD_EXAMPLES=ON \
  -DAUTOMERGE_CPP_BUILD_BENCHMARKS=ON
cmake --build build-macos --config Release
ctest --test-dir build-macos -C Release --output-on-failure
```

## Windows header isolation

`src/document.cpp` uses the Windows cryptography API for actor ID generation. It defines
`NOMINMAX` before including `<windows.h>` so the Windows `min` and `max` macros cannot replace
standard-library functions such as `std::max` and `std::numeric_limits::max`.

The MSVC job can be reproduced from a Developer PowerShell with CMake, Git, and vcpkg available:

```powershell
git submodule update --init --recursive
vcpkg install zlib
cmake -B build -DAUTOMERGE_CPP_BUILD_TESTS=ON `
    -DAUTOMERGE_CPP_BUILD_EXAMPLES=ON `
    -DAUTOMERGE_CPP_BUILD_BENCHMARKS=ON `
    -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

## FreeBSD

On FreeBSD 15.1, use the base-system Clang after installing CMake and Git:

```sh
pkg install -y cmake git
cmake -B build-freebsd -DCMAKE_BUILD_TYPE=Release \
  -DAUTOMERGE_CPP_BUILD_TESTS=ON \
  -DAUTOMERGE_CPP_BUILD_EXAMPLES=ON \
  -DAUTOMERGE_CPP_BUILD_BENCHMARKS=ON
cmake --build build-freebsd --config Release
ctest --test-dir build-freebsd -C Release --output-on-failure
```

## GitHub Pages

Generate the same Doxygen output locally and verify the entry page exists:

```bash
doxygen docs/Doxyfile
test -f docs/html/index.html
```

The hosted setup step enables GitHub Pages when the repository does not yet have a Pages site.
