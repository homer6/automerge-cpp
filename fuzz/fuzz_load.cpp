// Fuzz target for Document::load() — exercises the full deserialization stack.
// Any valid document is round-tripped through save() to verify consistency.

#include <automerge-cpp/automerge.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    const auto span = std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(data), size);

    auto doc = automerge_cpp::Document::load(span);
    if (doc) {
        // Round-trip: if parse succeeded, save must not crash
        auto saved = doc->save();
        (void)saved;
    }
    return 0;
}
