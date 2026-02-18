#pragma once

// DEFLATE compression/decompression for column data.
//
// Columns larger than the threshold are compressed using raw DEFLATE
// (no zlib/gzip header), matching the upstream Rust implementation.
// The deflate bit in the ColumnSpec indicates whether a column is compressed.
//
// Internal header — not installed.

#include <cstddef>
#include <optional>
#include <vector>

#include <zlib.h>

namespace automerge_cpp::storage {

// Columns smaller than this are not compressed.
inline constexpr std::size_t deflate_threshold = 256;

// Compress data using raw DEFLATE (no zlib/gzip header).
inline auto deflate_compress(const std::vector<std::byte>& input)
    -> std::optional<std::vector<std::byte>> {

    if (input.empty()) return std::vector<std::byte>{};

    auto stream = z_stream{};
    // windowBits = -15 for raw deflate (negative = no header)
    auto ret = ::deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                              -15, 8, Z_DEFAULT_STRATEGY);
    if (ret != Z_OK) return std::nullopt;

    auto bound = ::deflateBound(&stream, static_cast<uLong>(input.size()));
    auto output = std::vector<std::byte>(bound);

    stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(input.data()));
    stream.avail_in = static_cast<uInt>(input.size());
    stream.next_out = reinterpret_cast<Bytef*>(output.data());
    stream.avail_out = static_cast<uInt>(bound);

    ret = ::deflate(&stream, Z_FINISH);
    ::deflateEnd(&stream);

    if (ret != Z_STREAM_END) return std::nullopt;

    output.resize(stream.total_out);
    return output;
}

// Decompress raw DEFLATE data (no zlib/gzip header).
// max_output_size limits decompressed output to prevent memory bombs.
inline auto deflate_decompress(const std::vector<std::byte>& input,
                                std::size_t max_output_size = std::size_t{64} * 1024 * 1024)
    -> std::optional<std::vector<std::byte>> {

    if (input.empty()) return std::vector<std::byte>{};

    // Start with 4x the input size, grow if needed
    auto output_size = std::min(input.size() * 4, max_output_size);
    auto output = std::vector<std::byte>(output_size);

    auto stream = z_stream{};
    stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(input.data()));
    stream.avail_in = static_cast<uInt>(input.size());
    stream.next_out = reinterpret_cast<Bytef*>(output.data());
    stream.avail_out = static_cast<uInt>(output_size);

    // windowBits = -15 for raw deflate (negative = no header)
    auto ret = ::inflateInit2(&stream, -15);
    if (ret != Z_OK) return std::nullopt;

    ret = ::inflate(&stream, Z_FINISH);

    if (ret == Z_BUF_ERROR || ret == Z_OK) {
        // Need more space
        while (ret != Z_STREAM_END && output_size < max_output_size) {
            auto written = stream.total_out;
            output_size = std::min(output_size * 2, max_output_size);
            output.resize(output_size);
            stream.next_out = reinterpret_cast<Bytef*>(output.data() + written);
            stream.avail_out = static_cast<uInt>(output_size - written);
            ret = ::inflate(&stream, Z_FINISH);
        }
    }

    ::inflateEnd(&stream);

    if (ret != Z_STREAM_END) return std::nullopt;

    output.resize(stream.total_out);
    return output;
}

}  // namespace automerge_cpp::storage
