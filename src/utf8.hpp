#pragma once

#include <automerge-cpp/error.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string_view>
#include <vector>

namespace automerge_cpp::detail {

inline auto utf8_byte(std::string_view text, std::size_t index) -> std::uint8_t {
    return static_cast<std::uint8_t>(text[index]);
}

inline auto is_utf8_continuation(std::uint8_t byte) -> bool {
    return (byte & 0xc0U) == 0x80U;
}

inline auto utf8_scalar_width(std::string_view text, std::size_t offset)
    -> std::optional<std::size_t> {
    const auto remaining = text.size() - offset;
    const auto lead = utf8_byte(text, offset);
    if (lead <= 0x7fU) {
        return 1;
    }
    if (lead >= 0xc2U && lead <= 0xdfU && remaining >= 2 &&
        is_utf8_continuation(utf8_byte(text, offset + 1))) {
        return 2;
    }
    if (remaining >= 3) {
        const auto second = utf8_byte(text, offset + 1);
        const auto third = utf8_byte(text, offset + 2);
        const auto valid_second =
            (lead == 0xe0U && second >= 0xa0U && second <= 0xbfU) ||
            ((lead >= 0xe1U && lead <= 0xecU) && is_utf8_continuation(second)) ||
            (lead == 0xedU && second >= 0x80U && second <= 0x9fU) ||
            ((lead >= 0xeeU && lead <= 0xefU) && is_utf8_continuation(second));
        if (valid_second && is_utf8_continuation(third)) {
            return 3;
        }
    }
    if (remaining >= 4) {
        const auto second = utf8_byte(text, offset + 1);
        const auto third = utf8_byte(text, offset + 2);
        const auto fourth = utf8_byte(text, offset + 3);
        const auto valid_second =
            (lead == 0xf0U && second >= 0x90U && second <= 0xbfU) ||
            ((lead >= 0xf1U && lead <= 0xf3U) && is_utf8_continuation(second)) ||
            (lead == 0xf4U && second >= 0x80U && second <= 0x8fU);
        if (valid_second && is_utf8_continuation(third) && is_utf8_continuation(fourth)) {
            return 4;
        }
    }
    return std::nullopt;
}

inline auto split_utf8_scalars(std::string_view text)
    -> std::expected<std::vector<std::string_view>, Error> {
    auto scalars = std::vector<std::string_view>{};
    scalars.reserve(text.size());
    auto offset = std::size_t{0};
    while (offset < text.size()) {
        const auto width = utf8_scalar_width(text, offset);
        if (!width) {
            return std::unexpected{Error{ErrorKind::invalid_utf8, "text must be valid UTF-8"}};
        }
        scalars.push_back(text.substr(offset, *width));
        offset += *width;
    }
    return scalars;
}

inline auto is_single_utf8_scalar(std::string_view text) -> bool {
    const auto scalars = split_utf8_scalars(text);
    return scalars && scalars->size() == 1;
}

} // namespace automerge_cpp::detail
