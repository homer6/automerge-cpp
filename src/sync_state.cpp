#include <automerge-cpp/sync_state.hpp>

#include "storage/change_chunk.hpp"
#include "storage/deserializer.hpp"
#include "storage/serializer.hpp"
#include "sync/bloom_filter.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace automerge_cpp {
namespace {

constexpr auto sync_message_type = std::uint8_t{0x42};
constexpr auto sync_state_type = std::uint8_t{0x43};

auto decode_error(std::string message) -> std::unexpected<Error> {
    return std::unexpected{Error{ErrorKind::decoding_error, std::move(message)}};
}

auto read_hashes(storage::Deserializer& decoder, std::string_view field)
    -> std::expected<std::vector<ChangeHash>, Error> {
    const auto count = decoder.read_uleb128();
    if (!count) {
        return decode_error("invalid " + std::string{field} + " count");
    }
    if (*count > decoder.remaining() / ChangeHash::size) {
        return decode_error("truncated " + std::string{field});
    }

    auto hashes = std::vector<ChangeHash>{};
    hashes.reserve(static_cast<std::size_t>(*count));
    for (auto index = std::uint64_t{0}; index < *count; ++index) {
        const auto hash = decoder.read_change_hash();
        if (!hash) {
            return decode_error("truncated " + std::string{field});
        }
        hashes.push_back(*hash);
    }
    return hashes;
}

void write_hashes(storage::Serializer& encoder, const std::vector<ChangeHash>& hashes) {
    encoder.write_uleb128(hashes.size());
    for (const auto& hash : hashes) {
        encoder.write_change_hash(hash);
    }
}

auto read_length_prefixed_bytes(storage::Deserializer& decoder, std::string_view field)
    -> std::expected<std::span<const std::byte>, Error> {
    const auto length = decoder.read_uleb128();
    if (!length || *length > std::numeric_limits<std::size_t>::max()) {
        return decode_error("invalid " + std::string{field} + " length");
    }
    const auto bytes = decoder.read_bytes(static_cast<std::size_t>(*length));
    if (!bytes) {
        return decode_error("truncated " + std::string{field});
    }
    return *bytes;
}

auto read_have(storage::Deserializer& decoder) -> std::expected<std::vector<Have>, Error> {
    const auto count = decoder.read_uleb128();
    if (!count || *count > decoder.remaining()) {
        return decode_error("invalid have count");
    }

    auto have = std::vector<Have>{};
    have.reserve(static_cast<std::size_t>(*count));
    for (auto index = std::uint64_t{0}; index < *count; ++index) {
        auto last_sync = read_hashes(decoder, "have last_sync");
        if (!last_sync) {
            return std::unexpected{last_sync.error()};
        }
        const auto bloom_bytes = read_length_prefixed_bytes(decoder, "bloom filter");
        if (!bloom_bytes) {
            return std::unexpected{bloom_bytes.error()};
        }
        if (!sync::BloomFilter::from_bytes(*bloom_bytes)) {
            return decode_error("invalid bloom filter");
        }
        have.push_back(Have{
            .last_sync = std::move(*last_sync),
            .bloom_bytes = {bloom_bytes->begin(), bloom_bytes->end()},
        });
    }
    return have;
}

auto read_changes(storage::Deserializer& decoder) -> std::expected<std::vector<Change>, Error> {
    const auto count = decoder.read_uleb128();
    if (!count || *count > decoder.remaining()) {
        return decode_error("invalid changes count");
    }

    auto changes = std::vector<Change>{};
    changes.reserve(static_cast<std::size_t>(*count));
    for (auto index = std::uint64_t{0}; index < *count; ++index) {
        const auto bytes = read_length_prefixed_bytes(decoder, "change chunk");
        if (!bytes) {
            return std::unexpected{bytes.error()};
        }
        auto parsed = storage::parse_change_chunk(*bytes);
        if (!parsed) {
            return decode_error("invalid change chunk");
        }
        changes.push_back(std::move(parsed->change));
    }
    return changes;
}

auto read_capabilities(storage::Deserializer& decoder)
    -> std::expected<std::optional<std::vector<std::uint8_t>>, Error> {
    if (decoder.at_end()) {
        return std::nullopt;
    }

    const auto count = decoder.read_uleb128();
    if (!count || *count > decoder.remaining()) {
        return decode_error("invalid capability count");
    }
    auto capabilities = std::vector<std::uint8_t>{};
    capabilities.reserve(static_cast<std::size_t>(*count));
    for (auto index = std::uint64_t{0}; index < *count; ++index) {
        const auto capability = decoder.read_u8();
        if (!capability) {
            return decode_error("truncated capabilities");
        }
        capabilities.push_back(*capability);
    }
    if (!decoder.at_end()) {
        return decode_error("trailing sync message bytes");
    }
    return capabilities;
}

} // namespace

auto SyncMessage::encode() const -> std::vector<std::byte> {
    auto encoder = storage::Serializer{};
    encoder.write_u8(sync_message_type);
    write_hashes(encoder, heads);
    write_hashes(encoder, need);

    encoder.write_uleb128(have.size());
    for (const auto& summary : have) {
        write_hashes(encoder, summary.last_sync);
        encoder.write_uleb128(summary.bloom_bytes.size());
        encoder.write_bytes(summary.bloom_bytes);
    }

    encoder.write_uleb128(changes.size());
    for (const auto& change : changes) {
        const auto chunk = storage::serialize_change_chunk(change);
        encoder.write_uleb128(chunk.size());
        encoder.write_bytes(chunk);
    }

    if (supported_capabilities) {
        encoder.write_uleb128(supported_capabilities->size());
        for (const auto capability : *supported_capabilities) {
            encoder.write_u8(capability);
        }
    }
    return encoder.take();
}

auto SyncMessage::decode(std::span<const std::byte> data) -> std::expected<SyncMessage, Error> {
    auto decoder = storage::Deserializer{data};
    const auto message_type = decoder.read_u8();
    if (!message_type || *message_type != sync_message_type) {
        return decode_error("expected Automerge v1 sync message type 0x42");
    }

    auto heads = read_hashes(decoder, "heads");
    if (!heads) {
        return std::unexpected{heads.error()};
    }
    auto need = read_hashes(decoder, "need");
    if (!need) {
        return std::unexpected{need.error()};
    }
    auto have = read_have(decoder);
    if (!have) {
        return std::unexpected{have.error()};
    }
    auto changes = read_changes(decoder);
    if (!changes) {
        return std::unexpected{changes.error()};
    }
    auto capabilities = read_capabilities(decoder);
    if (!capabilities) {
        return std::unexpected{capabilities.error()};
    }

    return SyncMessage{
        .heads = std::move(*heads),
        .need = std::move(*need),
        .have = std::move(*have),
        .changes = std::move(*changes),
        .supported_capabilities = std::move(*capabilities),
    };
}

auto SyncState::encode() const -> std::vector<std::byte> {
    auto encoder = storage::Serializer{};
    encoder.write_u8(sync_state_type);
    write_hashes(encoder, shared_heads_);
    return encoder.take();
}

auto SyncState::decode(std::span<const std::byte> data) -> std::optional<SyncState> {
    auto decoder = storage::Deserializer{data};
    const auto marker = decoder.read_u8();
    if (!marker || *marker != sync_state_type) {
        return std::nullopt;
    }

    auto shared_heads = read_hashes(decoder, "shared heads");
    if (!shared_heads || !decoder.at_end()) {
        return std::nullopt;
    }

    auto state = SyncState{};
    state.shared_heads_ = std::move(*shared_heads);
    state.their_have_ = std::vector<Have>{};
    return state;
}

} // namespace automerge_cpp
