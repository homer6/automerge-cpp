#pragma once

// Boolean run-length encoder/decoder for the Automerge columnar format.
//
// Encoding: alternating run-length counts of false/true values.
// Always starts with a count of false values (possibly 0).
// Each count is ULEB128 encoded.
//
// Internal header — not installed.

#include "leb128.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace automerge_cpp::encoding {

// -- Boolean Encoder ----------------------------------------------------------

class BooleanEncoder {
public:
    void append(bool value) {
        if (value == current_value_) {
            ++count_;
        } else {
            encode_uleb128(count_, data_);
            count_ = 1;
            current_value_ = value;
        }
    }

    void finish() {
        if (count_ > 0) {
            encode_uleb128(count_, data_);
        }
    }

    auto data() const -> const std::vector<std::byte>& { return data_; }
    auto take() -> std::vector<std::byte> { return std::move(data_); }

private:
    std::vector<std::byte> data_;
    bool current_value_ = false;  // always start counting false
    std::uint64_t count_ = 0;
};

// -- Boolean Decoder ----------------------------------------------------------

class BooleanDecoder {
public:
    explicit BooleanDecoder(std::span<const std::byte> data)
        : data_{data}, pos_{0} {}

    auto next() -> std::optional<bool> {
        while (remaining_ == 0) {
            if (pos_ >= data_.size()) return std::nullopt;
            auto r = decode_uleb128(data_.subspan(pos_));
            if (!r) return std::nullopt;
            pos_ += r->bytes_read;
            remaining_ = r->value;
            if (remaining_ == 0) {
                // Zero count means switch value
                current_value_ = !current_value_;
            }
        }
        --remaining_;
        auto result = current_value_;
        if (remaining_ == 0) {
            current_value_ = !current_value_;
        }
        return result;
    }

    auto done() const -> bool { return pos_ >= data_.size() && remaining_ == 0; }

private:
    std::span<const std::byte> data_;
    std::size_t pos_;
    bool current_value_ = false;
    std::uint64_t remaining_ = 0;
};

// -- MaybeBooleanEncoder (nullable booleans) ----------------------------------
// Uses BooleanEncoder for the values and a separate BooleanEncoder for
// the "has value" flag.

class MaybeBooleanEncoder {
public:
    void append(bool value) {
        has_encoder_.append(true);
        value_encoder_.append(value);
    }

    void append_null() {
        has_encoder_.append(false);
        value_encoder_.append(false);  // placeholder
    }

    void finish() {
        has_encoder_.finish();
        value_encoder_.finish();
    }

    auto has_data() const -> const std::vector<std::byte>& { return has_encoder_.data(); }
    auto value_data() const -> const std::vector<std::byte>& { return value_encoder_.data(); }

private:
    BooleanEncoder has_encoder_;
    BooleanEncoder value_encoder_;
};

}  // namespace automerge_cpp::encoding
