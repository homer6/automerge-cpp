#pragma once

// Column type and specification for the Automerge columnar format.
//
// Each column in a chunk has a 32-bit spec encoded as ULEB128:
//   bits [3:0] = ColumnType (3 bits type + 1 bit deflate flag)
//   bits [31:4] = column ID
//
// The spec u32 is: (column_id << 4) | (deflate << 3) | column_type
//
// Internal header — not installed.

#include <cstdint>
#include <optional>

namespace automerge_cpp::storage {

// The 8 column encoding types (3 bits, values 0-7).
enum class ColumnType : std::uint8_t {
    group_card   = 0,  // group cardinality (ULEB128 RLE)
    actor_id     = 1,  // actor index (ULEB128 RLE)
    integer_key  = 2,  // integer key (ULEB128 Delta-RLE)
    delta_int    = 2,  // alias for integer_key
    boolean      = 3,  // boolean (alternating run-length)
    string_rle   = 4,  // string (RLE with LEB128 length-prefix)
    value_meta   = 5,  // value metadata: (type_tag << 4) | length
    value_raw    = 6,  // raw value bytes
    integer_rle  = 7,  // integer (ULEB128 RLE, non-delta)
};

// A column specification: identifies a column by its ID, type, and deflate flag.
struct ColumnSpec {
    std::uint32_t column_id{0};
    ColumnType type{ColumnType::group_card};
    bool deflate{false};

    // Encode to the u32 bitfield format.
    constexpr auto to_u32() const -> std::uint32_t {
        return (column_id << 4) |
               (static_cast<std::uint32_t>(deflate) << 3) |
               static_cast<std::uint32_t>(type);
    }

    // Decode from the u32 bitfield format.
    static constexpr auto from_u32(std::uint32_t raw) -> ColumnSpec {
        return ColumnSpec{
            .column_id = raw >> 4,
            .type = static_cast<ColumnType>(raw & 0x07),
            .deflate = (raw & 0x08) != 0,
        };
    }

    auto operator==(const ColumnSpec&) const -> bool = default;
};

// Well-known column IDs used in change and document chunks.
namespace column_id {
    inline constexpr std::uint32_t obj_actor   = 0;
    inline constexpr std::uint32_t obj_counter = 0;
    inline constexpr std::uint32_t key_actor   = 1;
    inline constexpr std::uint32_t key_counter = 1;
    inline constexpr std::uint32_t key_string  = 1;
    inline constexpr std::uint32_t id_actor    = 2;
    inline constexpr std::uint32_t id_counter  = 2;
    inline constexpr std::uint32_t insert      = 3;
    inline constexpr std::uint32_t action      = 4;
    inline constexpr std::uint32_t value_meta  = 5;
    inline constexpr std::uint32_t value_raw   = 5;
    inline constexpr std::uint32_t pred_group  = 7;
    inline constexpr std::uint32_t pred_actor  = 7;
    inline constexpr std::uint32_t pred_counter = 7;
    inline constexpr std::uint32_t succ_group  = 8;
    inline constexpr std::uint32_t succ_actor  = 8;
    inline constexpr std::uint32_t succ_counter = 8;
    inline constexpr std::uint32_t expand      = 9;
    inline constexpr std::uint32_t mark_name   = 10;
}  // namespace column_id

// Standard column specs for change op columns (in order).
namespace change_op_columns {
    inline constexpr auto obj_actor    = ColumnSpec{column_id::obj_actor,    ColumnType::actor_id,    false};
    inline constexpr auto obj_counter  = ColumnSpec{column_id::obj_counter,  ColumnType::delta_int,   false};
    inline constexpr auto key_actor    = ColumnSpec{column_id::key_actor,    ColumnType::actor_id,    false};
    inline constexpr auto key_counter  = ColumnSpec{column_id::key_counter,  ColumnType::delta_int,   false};
    inline constexpr auto key_string   = ColumnSpec{column_id::key_string,   ColumnType::string_rle,  false};
    inline constexpr auto insert       = ColumnSpec{column_id::insert,       ColumnType::boolean,     false};
    inline constexpr auto action       = ColumnSpec{column_id::action,       ColumnType::integer_rle, false};
    inline constexpr auto value_meta   = ColumnSpec{column_id::value_meta,   ColumnType::value_meta,  false};
    inline constexpr auto value_raw    = ColumnSpec{column_id::value_raw,    ColumnType::value_raw,   false};
    inline constexpr auto pred_group   = ColumnSpec{column_id::pred_group,   ColumnType::group_card,  false};
    inline constexpr auto pred_actor   = ColumnSpec{column_id::pred_actor,   ColumnType::actor_id,    false};
    inline constexpr auto pred_counter = ColumnSpec{column_id::pred_counter, ColumnType::delta_int,   false};
    inline constexpr auto expand       = ColumnSpec{column_id::expand,       ColumnType::boolean,     false};
    inline constexpr auto mark_name    = ColumnSpec{column_id::mark_name,    ColumnType::string_rle,  false};
}  // namespace change_op_columns

}  // namespace automerge_cpp::storage
