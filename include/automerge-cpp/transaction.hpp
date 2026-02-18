/// @file transaction.hpp
/// @brief Transaction class for mutating documents.

#pragma once

#include <automerge-cpp/op.hpp>
#include <automerge-cpp/types.hpp>
#include <automerge-cpp/value.hpp>

#include <cstddef>
#include <string_view>
#include <vector>

namespace automerge_cpp {

namespace detail { struct DocState; }

/// A mutation interface for modifying a Document.
///
/// Transactions are created exclusively by Document::transact() or
/// Document::transact_with_patches(). All mutations to a document
/// must go through a transaction, which ensures atomicity: either
/// all operations in the transaction are applied, or none are.
///
/// @code
/// doc.transact([](auto& tx) {
///     tx.put(root, "name", std::string{"Alice"});
///     tx.put(root, "age", std::int64_t{30});
/// });
/// @endcode
class Transaction {
    friend class Document;
    explicit Transaction(detail::DocState& state);

public:
    /// Set a scalar value at a map key.
    /// @param obj The map object to modify.
    /// @param key The key to set.
    /// @param val The scalar value to store.
    void put(const ObjId& obj, std::string_view key, ScalarValue val);

    /// Create a nested object at a map key.
    /// @param obj The map object to modify.
    /// @param key The key to set.
    /// @param type The type of nested object to create.
    /// @return The ObjId of the newly created object.
    auto put_object(const ObjId& obj, std::string_view key, ObjType type) -> ObjId;

    /// Delete a map key.
    /// @param obj The map object to modify.
    /// @param key The key to delete.
    void delete_key(const ObjId& obj, std::string_view key);

    /// Insert a scalar value into a list at the given index.
    /// @param obj The list object to modify.
    /// @param index The position to insert at (0 = beginning).
    /// @param val The scalar value to insert.
    void insert(const ObjId& obj, std::size_t index, ScalarValue val);

    /// Insert a nested object into a list at the given index.
    /// @param obj The list object to modify.
    /// @param index The position to insert at.
    /// @param type The type of nested object to create.
    /// @return The ObjId of the newly created object.
    auto insert_object(const ObjId& obj, std::size_t index, ObjType type) -> ObjId;

    /// Set a scalar value at a list index (overwrite).
    /// @param obj The list object to modify.
    /// @param index The position to overwrite.
    /// @param val The new scalar value.
    void set(const ObjId& obj, std::size_t index, ScalarValue val);

    /// Delete an element at a list index.
    /// @param obj The list object to modify.
    /// @param index The position to delete.
    void delete_index(const ObjId& obj, std::size_t index);

    /// Splice text: delete characters and/or insert new text.
    /// @param obj The text object to modify.
    /// @param pos The starting position.
    /// @param del The number of characters to delete.
    /// @param text The text to insert at the position.
    void splice_text(const ObjId& obj, std::size_t pos, std::size_t del,
                     std::string_view text);

    /// Increment a counter value at a map key.
    /// @param obj The map object containing the counter.
    /// @param key The key of the counter.
    /// @param delta The amount to increment (may be negative).
    void increment(const ObjId& obj, std::string_view key, std::int64_t delta);

    /// Apply a rich-text mark to a range.
    /// @param obj The text object to annotate.
    /// @param start Start index (inclusive).
    /// @param end End index (exclusive).
    /// @param name The mark name (e.g. "bold", "link").
    /// @param value The mark value (e.g. true, a URL string).
    void mark(const ObjId& obj, std::size_t start, std::size_t end,
              std::string_view name, ScalarValue value);

private:
    void commit();

    detail::DocState& state_;
    std::vector<Op> pending_ops_;
    std::uint64_t start_op_{0};
};

}  // namespace automerge_cpp
