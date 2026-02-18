#pragma once

// Value metadata encoding for the Automerge columnar format.
//
// Each value is encoded as a (type_tag, raw_bytes) pair across two columns:
//   value_meta: ULEB128 encoded as (byte_length << 4) | type_tag
//   value_raw:  the raw bytes of the value
//
// Type tags (upstream compatible):
//   0 = null, 1 = false, 2 = true, 3 = uint, 4 = int, 5 = f64,
//   6 = utf8 string, 7 = bytes, 8 = counter, 9 = timestamp
//
// Internal header — not installed.

#include <automerge-cpp/value.hpp>
#include "../../encoding/leb128.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <vector>

namespace automerge_cpp::storage {

// Upstream value type tags.
enum class ValueTag : std::uint8_t {
    null_tag      = 0,
    false_tag     = 1,
    true_tag      = 2,
    uint_tag      = 3,
    int_tag       = 4,
    f64_tag       = 5,
    utf8_tag      = 6,
    bytes_tag     = 7,
    counter_tag   = 8,
    timestamp_tag = 9,
};

// Encode a ScalarValue into value_meta (ULEB128) and value_raw bytes.
inline void encode_scalar_value(const ScalarValue& sv,
                                 std::vector<std::byte>& meta_out,
                                 std::vector<std::byte>& raw_out) {
    auto raw_start = raw_out.size();

    std::visit([&](const auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, Null> || std::is_same_v<T, bool>) {
            // null and bool: no raw bytes, encoded in tag
        } else if constexpr (std::is_same_v<T, std::uint64_t>) {
            encoding::encode_uleb128(v, raw_out);
        } else if constexpr (std::is_same_v<T, std::int64_t>) {
            encoding::encode_sleb128(v, raw_out);
        } else if constexpr (std::is_same_v<T, double>) {
            auto bytes = std::array<std::byte, 8>{};
            std::memcpy(bytes.data(), &v, 8);
            raw_out.insert(raw_out.end(), bytes.begin(), bytes.end());
        } else if constexpr (std::is_same_v<T, Counter>) {
            encoding::encode_sleb128(v.value, raw_out);
        } else if constexpr (std::is_same_v<T, Timestamp>) {
            encoding::encode_sleb128(v.millis_since_epoch, raw_out);
        } else if constexpr (std::is_same_v<T, std::string>) {
            raw_out.insert(raw_out.end(),
                reinterpret_cast<const std::byte*>(v.data()),
                reinterpret_cast<const std::byte*>(v.data() + v.size()));
        } else if constexpr (std::is_same_v<T, Bytes>) {
            raw_out.insert(raw_out.end(), v.begin(), v.end());
        }
    }, sv);

    auto raw_len = raw_out.size() - raw_start;

    auto tag = std::visit([](const auto& v) -> ValueTag {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, Null>)         return ValueTag::null_tag;
        else if constexpr (std::is_same_v<T, bool>)    return v ? ValueTag::true_tag : ValueTag::false_tag;
        else if constexpr (std::is_same_v<T, std::uint64_t>) return ValueTag::uint_tag;
        else if constexpr (std::is_same_v<T, std::int64_t>)  return ValueTag::int_tag;
        else if constexpr (std::is_same_v<T, double>)        return ValueTag::f64_tag;
        else if constexpr (std::is_same_v<T, Counter>)       return ValueTag::counter_tag;
        else if constexpr (std::is_same_v<T, Timestamp>)     return ValueTag::timestamp_tag;
        else if constexpr (std::is_same_v<T, std::string>)   return ValueTag::utf8_tag;
        else if constexpr (std::is_same_v<T, Bytes>)         return ValueTag::bytes_tag;
        else { static_assert(!sizeof(T), "unhandled ScalarValue type"); }
    }, sv);

    // Encode meta: (raw_length << 4) | tag
    // Tag occupies the low 4 bits; length occupies the remaining bits.
    auto meta_val = (raw_len << 4) | static_cast<std::uint64_t>(tag);
    encoding::encode_uleb128(meta_val, meta_out);
}

// Encode a Value (ScalarValue or ObjType) into value_meta and value_raw.
// ObjType is encoded as uint with the ObjType value, so it can be
// reconstructed on decode.
inline void encode_value(const Value& val,
                          std::vector<std::byte>& meta_out,
                          std::vector<std::byte>& raw_out) {
    if (const auto* sv = std::get_if<ScalarValue>(&val)) {
        encode_scalar_value(*sv, meta_out, raw_out);
    } else {
        // ObjType — encode as uint with the enum value
        auto ot = std::get<ObjType>(val);
        auto raw_start = raw_out.size();
        encoding::encode_uleb128(static_cast<std::uint64_t>(ot), raw_out);
        auto raw_len = raw_out.size() - raw_start;
        auto meta_val = (raw_len << 4) | static_cast<std::uint64_t>(ValueTag::uint_tag);
        encoding::encode_uleb128(meta_val, meta_out);
    }
}

// Decode a value from meta and raw column data at given positions.
inline auto decode_value_from_columns(
    std::span<const std::byte> meta_data, std::size_t& meta_pos,
    std::span<const std::byte> raw_data, std::size_t& raw_pos)
    -> std::optional<Value> {

    auto meta_result = encoding::decode_uleb128(meta_data.subspan(meta_pos));
    if (!meta_result) return std::nullopt;
    meta_pos += meta_result->bytes_read;

    auto tag = static_cast<ValueTag>(meta_result->value & 0x0F);
    auto raw_len = static_cast<std::size_t>(meta_result->value >> 4);

    if (raw_pos + raw_len > raw_data.size()) return std::nullopt;

    auto raw_span = raw_data.subspan(raw_pos, raw_len);

    switch (tag) {
        case ValueTag::null_tag:
            raw_pos += raw_len;
            return Value{ScalarValue{Null{}}};
        case ValueTag::false_tag:
            raw_pos += raw_len;
            return Value{ScalarValue{false}};
        case ValueTag::true_tag:
            raw_pos += raw_len;
            return Value{ScalarValue{true}};
        case ValueTag::uint_tag: {
            auto r = encoding::decode_uleb128(raw_span);
            if (!r) return std::nullopt;
            raw_pos += raw_len;
            return Value{ScalarValue{r->value}};
        }
        case ValueTag::int_tag: {
            auto r = encoding::decode_sleb128(raw_span);
            if (!r) return std::nullopt;
            raw_pos += raw_len;
            return Value{ScalarValue{r->value}};
        }
        case ValueTag::f64_tag: {
            if (raw_len != 8) return std::nullopt;
            double v{};
            std::memcpy(&v, &raw_data[raw_pos], 8);
            raw_pos += 8;
            return Value{ScalarValue{v}};
        }
        case ValueTag::utf8_tag: {
            auto s = raw_len > 0
                ? std::string{reinterpret_cast<const char*>(raw_span.data()), raw_len}
                : std::string{};
            raw_pos += raw_len;
            return Value{ScalarValue{std::move(s)}};
        }
        case ValueTag::bytes_tag: {
            auto b = raw_len > 0
                ? Bytes{raw_span.begin(), raw_span.end()}
                : Bytes{};
            raw_pos += raw_len;
            return Value{ScalarValue{std::move(b)}};
        }
        case ValueTag::counter_tag: {
            auto r = encoding::decode_sleb128(raw_span);
            if (!r) return std::nullopt;
            raw_pos += raw_len;
            return Value{ScalarValue{Counter{r->value}}};
        }
        case ValueTag::timestamp_tag: {
            auto r = encoding::decode_sleb128(raw_span);
            if (!r) return std::nullopt;
            raw_pos += raw_len;
            return Value{ScalarValue{Timestamp{r->value}}};
        }
    }
    return std::nullopt;
}

}  // namespace automerge_cpp::storage
