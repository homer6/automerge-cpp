#pragma once

// Run-length encoder/decoder for the Automerge columnar format.
//
// Encoding scheme (matches upstream Rust):
//   positive N = run of N copies of the next value
//   negative N = literal run of |N| distinct values
//   zero       = null run (count follows as next value)
//
// Values are encoded/decoded using LEB128.
// Internal header — not installed.

#include "leb128.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace automerge_cpp::encoding {

// -- RLE Encoder --------------------------------------------------------------

template <typename T>
class RleEncoder {
public:
    void append(const T& value) {
        if (run_value_ && *run_value_ == value) {
            ++run_count_;
        } else {
            flush_run();
            run_value_ = value;
            run_count_ = 1;
        }
    }

    void append_null() {
        flush_run();
        flush_literals();
        ++null_count_;
    }

    void finish() {
        flush_run();
        flush_literals();
        flush_nulls();
    }

    auto data() const -> const std::vector<std::byte>& { return data_; }
    auto take() -> std::vector<std::byte> { return std::move(data_); }

private:
    void flush_nulls() {
        if (null_count_ > 0) {
            encode_sleb128(0, data_);  // null marker
            encode_uleb128(null_count_, data_);
            null_count_ = 0;
        }
    }

    void flush_run() {
        flush_nulls();
        if (!run_value_) return;

        if (run_count_ == 1) {
            // Buffer as potential literal run
            literal_buffer_.push_back(*run_value_);
        } else {
            // First flush any pending literals
            flush_literals();
            // Emit run: positive count + value
            encode_sleb128(static_cast<std::int64_t>(run_count_), data_);
            encode_value(*run_value_);
        }
        run_value_.reset();
        run_count_ = 0;
    }

    void flush_literals() {
        if (literal_buffer_.empty()) return;
        // Emit literal run: negative count + values
        encode_sleb128(-static_cast<std::int64_t>(literal_buffer_.size()), data_);
        for (const auto& v : literal_buffer_) {
            encode_value(v);
        }
        literal_buffer_.clear();
    }

    void encode_value(const T& value) {
        if constexpr (std::is_same_v<T, std::uint64_t>) {
            encode_uleb128(value, data_);
        } else if constexpr (std::is_same_v<T, std::int64_t>) {
            encode_sleb128(value, data_);
        } else if constexpr (std::is_same_v<T, std::string>) {
            encode_uleb128(value.size(), data_);
            for (auto c : value) {
                data_.push_back(static_cast<std::byte>(c));
            }
        } else {
            // Generic: treat as uint64
            encode_uleb128(static_cast<std::uint64_t>(value), data_);
        }
    }

    std::vector<std::byte> data_;
    std::optional<T> run_value_;
    std::uint64_t run_count_ = 0;
    std::uint64_t null_count_ = 0;
    std::vector<T> literal_buffer_;
};

// -- RLE Decoder --------------------------------------------------------------

template <typename T>
class RleDecoder {
public:
    explicit RleDecoder(std::span<const std::byte> data)
        : data_{data}, pos_{0} {}

    auto next() -> std::optional<std::optional<T>> {
        // Return from current run/literal state
        if (null_remaining_ > 0) {
            --null_remaining_;
            return std::optional<T>{std::nullopt};
        }

        if (run_remaining_ > 0) {
            --run_remaining_;
            return std::optional<T>{run_value_};
        }

        if (literal_remaining_ > 0) {
            --literal_remaining_;
            auto val = decode_value();
            if (!val) return std::nullopt;  // truncated
            return std::optional<T>{*val};
        }

        // Read next control word
        if (pos_ >= data_.size()) return std::nullopt;  // end of stream

        auto control = decode_sleb128(data_.subspan(pos_));
        if (!control) return std::nullopt;
        pos_ += control->bytes_read;

        if (control->value == 0) {
            // Null run: next value is count
            auto count_result = decode_uleb128(data_.subspan(pos_));
            if (!count_result) return std::nullopt;
            pos_ += count_result->bytes_read;
            null_remaining_ = count_result->value;
            if (null_remaining_ == 0) return std::nullopt;
            --null_remaining_;
            return std::optional<T>{std::nullopt};
        } else if (control->value > 0) {
            // Run: count copies of next value
            auto val = decode_value();
            if (!val) return std::nullopt;
            run_value_ = *val;
            run_remaining_ = static_cast<std::uint64_t>(control->value) - 1;
            return std::optional<T>{*val};
        } else {
            // Literal run: |count| distinct values
            literal_remaining_ = static_cast<std::uint64_t>(-control->value) - 1;
            auto val = decode_value();
            if (!val) return std::nullopt;
            return std::optional<T>{*val};
        }
    }

    auto done() const -> bool { return pos_ >= data_.size() && run_remaining_ == 0 &&
                                       literal_remaining_ == 0 && null_remaining_ == 0; }

private:
    auto decode_value() -> std::optional<T> {
        if constexpr (std::is_same_v<T, std::uint64_t>) {
            auto r = decode_uleb128(data_.subspan(pos_));
            if (!r) return std::nullopt;
            pos_ += r->bytes_read;
            return r->value;
        } else if constexpr (std::is_same_v<T, std::int64_t>) {
            auto r = decode_sleb128(data_.subspan(pos_));
            if (!r) return std::nullopt;
            pos_ += r->bytes_read;
            return r->value;
        } else if constexpr (std::is_same_v<T, std::string>) {
            auto len_r = decode_uleb128(data_.subspan(pos_));
            if (!len_r) return std::nullopt;
            pos_ += len_r->bytes_read;
            auto len = static_cast<std::size_t>(len_r->value);
            if (pos_ + len > data_.size()) return std::nullopt;
            auto str = std::string{reinterpret_cast<const char*>(&data_[pos_]), len};
            pos_ += len;
            return str;
        } else {
            auto r = decode_uleb128(data_.subspan(pos_));
            if (!r) return std::nullopt;
            pos_ += r->bytes_read;
            return static_cast<T>(r->value);
        }
    }

    std::span<const std::byte> data_;
    std::size_t pos_;
    T run_value_{};
    std::uint64_t run_remaining_ = 0;
    std::uint64_t literal_remaining_ = 0;
    std::uint64_t null_remaining_ = 0;
};

}  // namespace automerge_cpp::encoding
