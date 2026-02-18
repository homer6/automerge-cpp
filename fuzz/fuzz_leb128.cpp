// Fuzz target for LEB128 codec — exercises decode edge cases
// (overflow, truncation, maximum-length encodings).

#include "src/encoding/leb128.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    const auto span = std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(data), size);

    // Decode unsigned LEB128
    auto u = automerge_cpp::encoding::decode_uleb128(span);
    (void)u;

    // Decode signed LEB128
    auto s = automerge_cpp::encoding::decode_sleb128(span);
    (void)s;

    return 0;
}
