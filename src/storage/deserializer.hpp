#pragma once

// Byte stream deserializer for the Automerge binary format.
// Internal header — not installed.

#include <automerge-cpp/error.hpp>
#include <automerge-cpp/types.hpp>
#include <automerge-cpp/value.hpp>
#include "../encoding/leb128.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace automerge_cpp::storage {

class Deserializer {
public:
    explicit Deserializer(std::span<const std::byte> data)
        : data_{data}, pos_{0} {}

    auto remaining() const -> std::size_t { return data_.size() - pos_; }
    auto pos() const -> std::size_t { return pos_; }
    auto at_end() const -> bool { return pos_ >= data_.size(); }

    auto read_byte() -> std::optional<std::byte> {
        if (pos_ >= data_.size()) return std::nullopt;
        return data_[pos_++];
    }

    auto read_u8() -> std::optional<std::uint8_t> {
        auto b = read_byte();
        if (!b) return std::nullopt;
        return static_cast<std::uint8_t>(*b);
    }

    auto read_bytes(std::size_t n) -> std::optional<std::span<const std::byte>> {
        if (pos_ + n > data_.size()) return std::nullopt;
        auto result = data_.subspan(pos_, n);
        pos_ += n;
        return result;
    }

    auto read_raw_bytes(void* dest, std::size_t n) -> bool {
        if (pos_ + n > data_.size()) return false;
        std::memcpy(dest, &data_[pos_], n);
        pos_ += n;
        return true;
    }

    auto read_uleb128() -> std::optional<std::uint64_t> {
        auto result = encoding::decode_uleb128(data_.subspan(pos_));
        if (!result) return std::nullopt;
        pos_ += result->bytes_read;
        return result->value;
    }

    auto read_sleb128() -> std::optional<std::int64_t> {
        auto result = encoding::decode_sleb128(data_.subspan(pos_));
        if (!result) return std::nullopt;
        pos_ += result->bytes_read;
        return result->value;
    }

    auto read_string() -> std::optional<std::string> {
        auto len = read_uleb128();
        if (!len) return std::nullopt;
        auto bytes = read_bytes(static_cast<std::size_t>(*len));
        if (!bytes) return std::nullopt;
        return std::string{reinterpret_cast<const char*>(bytes->data()), bytes->size()};
    }

    auto read_actor_id() -> std::optional<ActorId> {
        auto bytes = read_bytes(ActorId::size);
        if (!bytes) return std::nullopt;
        ActorId id{};
        std::memcpy(id.bytes.data(), bytes->data(), ActorId::size);
        return id;
    }

    auto read_change_hash() -> std::optional<ChangeHash> {
        auto bytes = read_bytes(ChangeHash::size);
        if (!bytes) return std::nullopt;
        ChangeHash h{};
        std::memcpy(h.bytes.data(), bytes->data(), ChangeHash::size);
        return h;
    }

    auto read_op_id(const std::vector<ActorId>& actor_table) -> std::optional<OpId> {
        auto counter = read_uleb128();
        if (!counter) return std::nullopt;
        auto actor_idx = read_uleb128();
        if (!actor_idx) return std::nullopt;
        if (*actor_idx >= actor_table.size()) return std::nullopt;
        return OpId{*counter, actor_table[*actor_idx]};
    }

    auto read_obj_id(const std::vector<ActorId>& actor_table) -> std::optional<ObjId> {
        auto tag = read_u8();
        if (!tag) return std::nullopt;
        if (*tag == 0) return ObjId{};  // root
        auto op_id = read_op_id(actor_table);
        if (!op_id) return std::nullopt;
        return ObjId{*op_id};
    }

    auto read_prop() -> std::optional<Prop> {
        auto tag = read_u8();
        if (!tag) return std::nullopt;
        if (*tag == 0) {
            auto s = read_string();
            if (!s) return std::nullopt;
            return map_key(std::move(*s));
        } else {
            auto idx = read_uleb128();
            if (!idx) return std::nullopt;
            return list_index(static_cast<std::size_t>(*idx));
        }
    }

    auto read_scalar_value(std::uint8_t tag) -> std::optional<ScalarValue> {
        switch (tag) {
            case 1: return ScalarValue{Null{}};
            case 2: {
                auto b = read_u8();
                if (!b) return std::nullopt;
                return ScalarValue{*b != 0};
            }
            case 3: {
                auto v = read_sleb128();
                if (!v) return std::nullopt;
                return ScalarValue{*v};
            }
            case 4: {
                auto v = read_uleb128();
                if (!v) return std::nullopt;
                return ScalarValue{*v};
            }
            case 5: {
                double v{};
                if (!read_raw_bytes(&v, sizeof(v))) return std::nullopt;
                return ScalarValue{v};
            }
            case 6: {
                auto v = read_sleb128();
                if (!v) return std::nullopt;
                return ScalarValue{Counter{*v}};
            }
            case 7: {
                auto v = read_sleb128();
                if (!v) return std::nullopt;
                return ScalarValue{Timestamp{*v}};
            }
            case 8: {
                auto s = read_string();
                if (!s) return std::nullopt;
                return ScalarValue{std::move(*s)};
            }
            case 9: {
                auto len = read_uleb128();
                if (!len) return std::nullopt;
                auto bytes = read_bytes(static_cast<std::size_t>(*len));
                if (!bytes) return std::nullopt;
                auto result = Bytes{};
                result.assign(bytes->begin(), bytes->end());
                return ScalarValue{std::move(result)};
            }
            default: return std::nullopt;
        }
    }

    auto read_value() -> std::optional<Value> {
        auto tag = read_u8();
        if (!tag) return std::nullopt;
        if (*tag == 0) {
            auto obj_type = read_u8();
            if (!obj_type) return std::nullopt;
            return Value{static_cast<ObjType>(*obj_type)};
        }
        auto sv = read_scalar_value(*tag);
        if (!sv) return std::nullopt;
        return Value{std::move(*sv)};
    }

private:
    std::span<const std::byte> data_;
    std::size_t pos_;
};

}  // namespace automerge_cpp::storage
