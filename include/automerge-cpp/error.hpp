/// @file error.hpp
/// @brief Error types for the automerge-cpp library.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace automerge_cpp {

/// Categories of errors that can occur in the library.
enum class ErrorKind : std::uint8_t {
    invalid_document,   ///< The document data is malformed or corrupt.
    invalid_change,     ///< A change could not be parsed or applied.
    invalid_obj_id,     ///< An ObjId does not refer to a known object.
    encoding_error,     ///< An error occurred during binary encoding.
    decoding_error,     ///< An error occurred during binary decoding.
    sync_error,         ///< An error occurred during the sync protocol.
    invalid_operation,  ///< An operation is invalid in the current context.
};

/// Convert an ErrorKind to its string representation.
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

/// A structured error with a category and a human-readable message.
struct Error {
    ErrorKind kind;      ///< The category of this error.
    std::string message; ///< A human-readable description.

    /// Construct an Error with the given kind and message.
    Error(ErrorKind k, std::string msg)
        : kind{k}, message{std::move(msg)} {}

    auto operator==(const Error& other) const -> bool = default;
};

}  // namespace automerge_cpp
