#pragma once

// Delta encoder/decoder for the Automerge columnar format.
//
// Wraps RLE encoding on the deltas between consecutive values.
// Useful for monotonically increasing sequences (like counters or op IDs).
//
// Internal header — not installed.

#include "rle.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace automerge_cpp::encoding {

// -- Delta Encoder ------------------------------------------------------------

class DeltaEncoder {
public:
    void append(std::int64_t value) {
        auto delta = value - prev_;
        prev_ = value;
        rle_.append(delta);
    }

    void append_null() {
        rle_.append_null();
    }

    void finish() {
        rle_.finish();
    }

    auto data() const -> const std::vector<std::byte>& { return rle_.data(); }
    auto take() -> std::vector<std::byte> { return rle_.take(); }

private:
    RleEncoder<std::int64_t> rle_;
    std::int64_t prev_ = 0;
};

// -- Delta Decoder ------------------------------------------------------------

class DeltaDecoder {
public:
    explicit DeltaDecoder(std::span<const std::byte> data)
        : rle_{data} {}

    auto next() -> std::optional<std::optional<std::int64_t>> {
        auto result = rle_.next();
        if (!result) return std::nullopt;  // end of stream

        if (!*result) {
            // null value — don't update accumulator
            return std::optional<std::int64_t>{std::nullopt};
        }

        absolute_ += **result;
        return std::optional<std::int64_t>{absolute_};
    }

    auto done() const -> bool { return rle_.done(); }

private:
    RleDecoder<std::int64_t> rle_;
    std::int64_t absolute_ = 0;
};

}  // namespace automerge_cpp::encoding
