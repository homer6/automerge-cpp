#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace automerge_cpp {

enum class ErrorKind : std::uint8_t {
    invalid_document,
    invalid_change,
    invalid_obj_id,
    encoding_error,
    decoding_error,
    sync_error,
    invalid_operation,
};

constexpr auto to_string_view(ErrorKind kind) noexcept -> std::string_view {
    switch (kind) {
        case ErrorKind::invalid_document:  return "invalid_document";
        case ErrorKind::invalid_change:    return "invalid_change";
        case ErrorKind::invalid_obj_id:    return "invalid_obj_id";
        case ErrorKind::encoding_error:    return "encoding_error";
        case ErrorKind::decoding_error:    return "decoding_error";
        case ErrorKind::sync_error:        return "sync_error";
        case ErrorKind::invalid_operation: return "invalid_operation";
    }
    return "unknown";
}

struct Error {
    ErrorKind kind;
    std::string message;

    Error(ErrorKind k, std::string msg)
        : kind{k}, message{std::move(msg)} {}

    auto operator==(const Error& other) const -> bool = default;
};

}  // namespace automerge_cpp
