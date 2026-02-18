// Fuzz target for change chunk parsing — exercises columnar deserialization
// with a plausible actor table.

#include <automerge-cpp/types.hpp>
#include "src/storage/change_chunk.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    const auto span = std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(data), size);

    // Provide a single zero actor so the actor table is non-empty
    const auto actor = automerge_cpp::ActorId{};
    const auto actors = std::vector<automerge_cpp::ActorId>{actor};

    auto result = automerge_cpp::storage::parse_change_chunk(span, actors);
    (void)result;

    return 0;
}
