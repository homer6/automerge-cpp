#pragma once

// Raw column data container and column header parser/writer.
//
// A RawColumn is a (spec, bytes) pair representing one column in a chunk.
// RawColumns is a collection that can parse/write the column header table
// preceding column data in a chunk.
//
// Internal header — not installed.

#include "column_spec.hpp"
#include "../../encoding/leb128.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace automerge_cpp::storage {

// A single column: its spec and raw byte content.
struct RawColumn {
    ColumnSpec spec;
    std::vector<std::byte> data;
};

// Parse column headers from a byte stream.
// Format: repeated (ULEB128 spec, ULEB128 length) pairs until a spec
// with column_id less than the previous (or end of data).
// After the headers, the column data follows sequentially.
inline auto parse_raw_columns(std::span<const std::byte> input, std::size_t& pos)
    -> std::vector<RawColumn> {

    auto columns = std::vector<RawColumn>{};

    // First pass: read all (spec, length) pairs
    struct ColHeader {
        ColumnSpec spec;
        std::uint64_t length;
    };
    auto headers = std::vector<ColHeader>{};

    auto prev_spec_u32 = std::uint32_t{0};

    while (pos < input.size()) {
        auto spec_result = encoding::decode_uleb128(input.subspan(pos));
        if (!spec_result) break;

        auto spec_u32 = static_cast<std::uint32_t>(spec_result->value);
        auto spec = ColumnSpec::from_u32(spec_u32);

        // Column specs must be in ascending order; if not, we've hit the end
        if (!headers.empty() && spec_u32 <= prev_spec_u32) break;

        pos += spec_result->bytes_read;

        auto len_result = encoding::decode_uleb128(input.subspan(pos));
        if (!len_result) break;
        pos += len_result->bytes_read;

        headers.push_back(ColHeader{.spec = spec, .length = len_result->value});
        prev_spec_u32 = spec_u32;
    }

    // Second pass: extract column data
    columns.reserve(headers.size());
    for (const auto& hdr : headers) {
        auto len = static_cast<std::size_t>(hdr.length);
        if (pos + len > input.size()) break;

        auto col = RawColumn{
            .spec = hdr.spec,
            .data = std::vector<std::byte>(input.begin() + static_cast<std::ptrdiff_t>(pos),
                                           input.begin() + static_cast<std::ptrdiff_t>(pos + len)),
        };
        pos += len;
        columns.push_back(std::move(col));
    }

    return columns;
}

// Write column headers and data to output.
inline void write_raw_columns(const std::vector<RawColumn>& columns,
                               std::vector<std::byte>& output) {
    // Write (spec, length) pairs
    for (const auto& col : columns) {
        encoding::encode_uleb128(col.spec.to_u32(), output);
        encoding::encode_uleb128(col.data.size(), output);
    }

    // Write column data
    for (const auto& col : columns) {
        output.insert(output.end(), col.data.begin(), col.data.end());
    }
}

}  // namespace automerge_cpp::storage
