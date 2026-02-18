#pragma once

// Byte stream serializer for the Automerge binary format.
// Internal header — not installed.

#include <automerge-cpp/types.hpp>
#include <automerge-cpp/value.hpp>
#include "../encoding/leb128.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace automerge_cpp::storage {

class Serializer {
public:
    void write_byte(std::byte b) {
        data_.push_back(b);
    }

    void write_u8(std::uint8_t v) {
        data_.push_back(static_cast<std::byte>(v));
    }

    void write_bytes(std::span<const std::byte> bytes) {
        data_.insert(data_.end(), bytes.begin(), bytes.end());
    }

    void write_raw_bytes(const void* ptr, std::size_t n) {
        auto* p = static_cast<const std::byte*>(ptr);
        data_.insert(data_.end(), p, p + n);
    }

    void write_uleb128(std::uint64_t value) {
        encoding::encode_uleb128(value, data_);
    }

    void write_sleb128(std::int64_t value) {
        encoding::encode_sleb128(value, data_);
    }

    void write_string(std::string_view s) {
        write_uleb128(s.size());
        for (auto c : s) {
            data_.push_back(static_cast<std::byte>(c));
        }
    }

    void write_actor_id(const ActorId& id) {
        write_bytes(id.bytes);
    }

    void write_change_hash(const ChangeHash& h) {
        write_bytes(h.bytes);
    }

    void write_op_id(const OpId& id, const std::vector<ActorId>& actor_table) {
        write_uleb128(id.counter);
        // Write actor index in the actor table
        for (std::size_t i = 0; i < actor_table.size(); ++i) {
            if (actor_table[i] == id.actor) {
                write_uleb128(i);
                return;
            }
        }
        // Should not reach here if actor table is correctly built
        write_uleb128(0);
    }

    void write_obj_id(const ObjId& id, const std::vector<ActorId>& actor_table) {
        if (id.is_root()) {
            write_u8(0);  // root marker
        } else {
            write_u8(1);  // non-root
            write_op_id(std::get<OpId>(id.inner), actor_table);
        }
    }

    void write_prop(const Prop& prop) {
        std::visit([this](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::string>) {
                write_u8(0);  // string key
                write_string(v);
            } else {
                write_u8(1);  // index
                write_uleb128(v);
            }
        }, prop);
    }

    void write_scalar_value(const ScalarValue& sv) {
        std::visit([this](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, Null>) {
                write_u8(1);
            } else if constexpr (std::is_same_v<T, bool>) {
                write_u8(2);
                write_u8(v ? 1 : 0);
            } else if constexpr (std::is_same_v<T, std::int64_t>) {
                write_u8(3);
                write_sleb128(v);
            } else if constexpr (std::is_same_v<T, std::uint64_t>) {
                write_u8(4);
                write_uleb128(v);
            } else if constexpr (std::is_same_v<T, double>) {
                write_u8(5);
                write_raw_bytes(&v, sizeof(v));
            } else if constexpr (std::is_same_v<T, Counter>) {
                write_u8(6);
                write_sleb128(v.value);
            } else if constexpr (std::is_same_v<T, Timestamp>) {
                write_u8(7);
                write_sleb128(v.millis_since_epoch);
            } else if constexpr (std::is_same_v<T, std::string>) {
                write_u8(8);
                write_string(v);
            } else if constexpr (std::is_same_v<T, Bytes>) {
                write_u8(9);
                write_uleb128(v.size());
                write_bytes(v);
            }
        }, sv);
    }

    void write_value(const Value& val) {
        std::visit([this](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, ObjType>) {
                write_u8(0);  // ObjType
                write_u8(static_cast<std::uint8_t>(v));
            } else {
                write_scalar_value(v);
            }
        }, val);
    }

    auto data() const -> const std::vector<std::byte>& { return data_; }
    auto take() -> std::vector<std::byte> { return std::move(data_); }

private:
    std::vector<std::byte> data_;
};

}  // namespace automerge_cpp::storage
