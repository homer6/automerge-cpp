/// @file error.hpp
/// @brief Error types for the automerge-cpp library.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace automerge_cpp {

/// Categories of errors that can occur in the library.
enum class ErrorKind : std::uint8_t {
    invalid_document,     ///< The document data is malformed or corrupt.
    invalid_change,       ///< A change could not be parsed or applied.
    invalid_obj_id,       ///< An ObjId does not refer to a known object.
    invalid_object_type,  ///< An object does not support the requested operation.
    index_out_of_bounds,  ///< A list or text index is outside its valid range.
    invalid_key,          ///< A map key or mark name is not valid UTF-8.
    invalid_utf8,         ///< Text input is not valid UTF-8.
    invalid_cursor,       ///< A cursor does not belong to the requested object.
    invalid_mark,         ///< A rich-text mark range is invalid.
    invalid_value_type,   ///< A stored value does not support the requested operation.
    inactive_transaction, ///< A transaction was used after commit or rollback.
    encoding_error,       ///< An error occurred during binary encoding.
    decoding_error,       ///< An error occurred during binary decoding.
    sync_error,           ///< An error occurred during the sync protocol.
    invalid_operation,    ///< An operation is invalid in the current context.
};

/// Convert an ErrorKind to its string representation.
constexpr auto to_string_view(ErrorKind kind) noexcept -> std::string_view {
    switch (kind) {
    case ErrorKind::invalid_document:
        return "invalid_document";
    case ErrorKind::invalid_change:
        return "invalid_change";
    case ErrorKind::invalid_obj_id:
        return "invalid_obj_id";
    case ErrorKind::invalid_object_type:
        return "invalid_object_type";
    case ErrorKind::index_out_of_bounds:
        return "index_out_of_bounds";
    case ErrorKind::invalid_key:
        return "invalid_key";
    case ErrorKind::invalid_utf8:
        return "invalid_utf8";
    case ErrorKind::invalid_cursor:
        return "invalid_cursor";
    case ErrorKind::invalid_mark:
        return "invalid_mark";
    case ErrorKind::invalid_value_type:
        return "invalid_value_type";
    case ErrorKind::inactive_transaction:
        return "inactive_transaction";
    case ErrorKind::encoding_error:
        return "encoding_error";
    case ErrorKind::decoding_error:
        return "decoding_error";
    case ErrorKind::sync_error:
        return "sync_error";
    case ErrorKind::invalid_operation:
        return "invalid_operation";
    }
    return "unknown";
}

/// A structured error with a category and a human-readable message.
struct Error {
    ErrorKind kind;      ///< The category of this error.
    std::string message; ///< A human-readable description.

    /// Construct an Error with the given kind and message.
    Error(ErrorKind k, std::string msg) : kind{k}, message{std::move(msg)} {}

    auto operator==(const Error& other) const -> bool = default;
};

} // namespace automerge_cpp
