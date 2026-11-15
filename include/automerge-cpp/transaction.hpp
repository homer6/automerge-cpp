/// @file transaction.hpp
/// @brief Transaction class for mutating documents.

#pragma once

#include <automerge-cpp/error.hpp>
#include <automerge-cpp/op.hpp>
#include <automerge-cpp/types.hpp>
#include <automerge-cpp/value.hpp>

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <initializer_list>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace automerge_cpp {

namespace detail {
struct DocState;
struct ObjectState;
struct TransactionRollback;
} // namespace detail

/// Optional metadata attached to a committed change.
struct CommitOptions {
    std::optional<std::string> message; ///< Human-readable description of the change.
    std::optional<std::int64_t> time;   ///< Unix timestamp in milliseconds.

    auto operator==(const CommitOptions&) const -> bool = default;
};

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
    ~Transaction();

    Transaction(const Transaction&) = delete;
    auto operator=(const Transaction&) -> Transaction& = delete;
    Transaction(Transaction&&) = delete;
    auto operator=(Transaction&&) -> Transaction& = delete;

    /// Discard every operation made by this transaction.
    ///
    /// Restores document data, heads, sequence numbers, and operation counters
    /// to their values when the transaction began. Calling rollback more than
    /// once returns ErrorKind::inactive_transaction. A rolled-back transaction cannot be
    /// committed.
    auto rollback() -> std::expected<void, Error>;

    /// Commit this transaction with optional change metadata.
    ///
    /// Has no effect if the transaction has no operations. Committing an inactive transaction
    /// returns ErrorKind::inactive_transaction.
    /// @param options Message and timestamp to store on the committed change.
    auto commit(CommitOptions options = {}) -> std::expected<void, Error>;

    /// Set a scalar value at a map key.
    /// @param obj The map object to modify.
    /// @param key The key to set.
    /// @param val The scalar value to store.
    auto put(const ObjId& obj, std::string_view key, ScalarValue val) -> std::expected<void, Error>;

    /// Create a nested object at a map key.
    /// @param obj The map object to modify.
    /// @param key The key to set.
    /// @param type The type of nested object to create.
    /// @return The ObjId of the newly created object.
    auto put_object(const ObjId& obj, std::string_view key, ObjType type)
        -> std::expected<ObjId, Error>;

    /// Delete a map key.
    /// @param obj The map object to modify.
    /// @param key The key to delete.
    auto delete_key(const ObjId& obj, std::string_view key) -> std::expected<void, Error>;

    /// Insert a scalar value into a list at the given index.
    /// @param obj The list object to modify.
    /// @param index The position to insert at (0 = beginning).
    /// @param val The scalar value to insert.
    auto insert(const ObjId& obj, std::size_t index, ScalarValue val) -> std::expected<void, Error>;

    /// Insert a nested object into a list at the given index.
    /// @param obj The list object to modify.
    /// @param index The position to insert at.
    /// @param type The type of nested object to create.
    /// @return The ObjId of the newly created object.
    auto insert_object(const ObjId& obj, std::size_t index, ObjType type)
        -> std::expected<ObjId, Error>;

    /// Set a scalar value at a list index (overwrite).
    /// @param obj The list object to modify.
    /// @param index The position to overwrite.
    /// @param val The new scalar value.
    auto set(const ObjId& obj, std::size_t index, ScalarValue val) -> std::expected<void, Error>;

    /// Delete an element at a list index.
    /// @param obj The list object to modify.
    /// @param index The position to delete.
    auto delete_index(const ObjId& obj, std::size_t index) -> std::expected<void, Error>;

    /// Splice text: delete Unicode scalar values and/or insert valid UTF-8 text.
    /// @param obj The text object to modify.
    /// @param pos The starting position.
    /// @param del The number of characters to delete.
    /// @param text The text to insert at the position.
    /// @return Success, or an error if text is not valid UTF-8.
    auto splice_text(const ObjId& obj, std::size_t pos, std::size_t del, std::string_view text)
        -> std::expected<void, Error>;

    /// Increment a counter value at a map key.
    /// @param obj The map object containing the counter.
    /// @param key The key of the counter.
    /// @param delta The amount to increment (may be negative).
    auto increment(const ObjId& obj, std::string_view key, std::int64_t delta)
        -> std::expected<void, Error>;

    /// Apply a rich-text mark to a range.
    /// @param obj The text object to annotate.
    /// @param start Start index (inclusive).
    /// @param end End index (exclusive).
    /// @param name The mark name (e.g. "bold", "link").
    /// @param value The mark value (e.g. true, a URL string).
    auto mark(const ObjId& obj,
              std::size_t start,
              std::size_t end,
              std::string_view name,
              ScalarValue value) -> std::expected<void, Error>;

    // -- Scalar convenience overloads (map key) -------------------------------

    /// Put a std::string at a map key.
    auto put(const ObjId& obj, std::string_view key, std::string val)
        -> std::expected<void, Error> {
        return put(obj, key, ScalarValue{std::move(val)});
    }
    /// Put a string literal at a map key.
    auto put(const ObjId& obj, std::string_view key, const char* val)
        -> std::expected<void, Error> {
        if (!val) {
            return std::unexpected{
                Error{ErrorKind::invalid_value_type, "string pointer must not be null"}};
        }
        return put(obj, key, ScalarValue{std::string{val}});
    }
    /// Put a string_view at a map key (copies into the document).
    auto put(const ObjId& obj, std::string_view key, std::string_view val)
        -> std::expected<void, Error> {
        return put(obj, key, ScalarValue{std::string{val}});
    }
    /// Put an int at a map key (promotes to int64_t).
    auto put(const ObjId& obj, std::string_view key, int val) -> std::expected<void, Error> {
        return put(obj, key, ScalarValue{static_cast<std::int64_t>(val)});
    }
    /// Put an int64_t at a map key.
    auto put(const ObjId& obj, std::string_view key, std::int64_t val)
        -> std::expected<void, Error> {
        return put(obj, key, ScalarValue{val});
    }
    /// Put a uint64_t at a map key.
    auto put(const ObjId& obj, std::string_view key, std::uint64_t val)
        -> std::expected<void, Error> {
        return put(obj, key, ScalarValue{val});
    }
    /// Put a double at a map key.
    auto put(const ObjId& obj, std::string_view key, double val) -> std::expected<void, Error> {
        return put(obj, key, ScalarValue{val});
    }
    /// Put a bool at a map key.
    auto put(const ObjId& obj, std::string_view key, bool val) -> std::expected<void, Error> {
        return put(obj, key, ScalarValue{val});
    }
    /// Put a Null at a map key.
    auto put(const ObjId& obj, std::string_view key, Null val) -> std::expected<void, Error> {
        return put(obj, key, ScalarValue{val});
    }
    /// Put a Counter at a map key.
    auto put(const ObjId& obj, std::string_view key, Counter val) -> std::expected<void, Error> {
        return put(obj, key, ScalarValue{val});
    }
    /// Put a Timestamp at a map key.
    auto put(const ObjId& obj, std::string_view key, Timestamp val) -> std::expected<void, Error> {
        return put(obj, key, ScalarValue{val});
    }

    /// Create a nested object at a map key (convenience overload for put_object).
    /// @param obj The map object to modify.
    /// @param key The key to set.
    /// @param type The type of nested object to create (ObjType::map, ObjType::list, etc.).
    /// @return The ObjId of the newly created object.
    auto put(const ObjId& obj, std::string_view key, ObjType type) -> std::expected<ObjId, Error> {
        return put_object(obj, key, type);
    }

    // -- Scalar convenience overloads (list insert) ---------------------------

    /// Insert a std::string into a list.
    auto insert(const ObjId& obj, std::size_t index, std::string val)
        -> std::expected<void, Error> {
        return insert(obj, index, ScalarValue{std::move(val)});
    }
    /// Insert a string literal into a list.
    auto insert(const ObjId& obj, std::size_t index, const char* val)
        -> std::expected<void, Error> {
        if (!val) {
            return std::unexpected{
                Error{ErrorKind::invalid_value_type, "string pointer must not be null"}};
        }
        return insert(obj, index, ScalarValue{std::string{val}});
    }
    /// Insert a string_view into a list.
    auto insert(const ObjId& obj, std::size_t index, std::string_view val)
        -> std::expected<void, Error> {
        return insert(obj, index, ScalarValue{std::string{val}});
    }
    /// Insert an int into a list (promotes to int64_t).
    auto insert(const ObjId& obj, std::size_t index, int val) -> std::expected<void, Error> {
        return insert(obj, index, ScalarValue{static_cast<std::int64_t>(val)});
    }
    /// Insert an int64_t into a list.
    auto insert(const ObjId& obj, std::size_t index, std::int64_t val)
        -> std::expected<void, Error> {
        return insert(obj, index, ScalarValue{val});
    }
    /// Insert a uint64_t into a list.
    auto insert(const ObjId& obj, std::size_t index, std::uint64_t val)
        -> std::expected<void, Error> {
        return insert(obj, index, ScalarValue{val});
    }
    /// Insert a double into a list.
    auto insert(const ObjId& obj, std::size_t index, double val) -> std::expected<void, Error> {
        return insert(obj, index, ScalarValue{val});
    }
    /// Insert a bool into a list.
    auto insert(const ObjId& obj, std::size_t index, bool val) -> std::expected<void, Error> {
        return insert(obj, index, ScalarValue{val});
    }

    /// Insert a nested object into a list (convenience overload for insert_object).
    auto insert(const ObjId& obj, std::size_t index, ObjType type) -> std::expected<ObjId, Error> {
        return insert_object(obj, index, type);
    }

    // -- Initializer list overloads (List / Map) ------------------------------

    /// Create a list at a map key and populate it with initial values.
    /// @code
    /// auto items = tx.put(root, "items", List{"Milk", "Eggs", "Bread"});
    /// @endcode
    auto put(const ObjId& obj, std::string_view key, const List& init)
        -> std::expected<ObjId, Error> {
        for (const auto& value : init.values) {
            const auto valid = validate_scalar(value);
            if (!valid) {
                return std::unexpected{valid.error()};
            }
        }
        auto list = put_object(obj, key, ObjType::list);
        if (!list) {
            return std::unexpected{list.error()};
        }
        std::size_t idx = 0;
        for (const auto& val : init.values) {
            static_cast<void>(insert(*list, idx++, val));
        }
        return list;
    }

    /// Create a map at a map key and populate it with initial entries.
    /// @code
    /// auto config = tx.put(root, "config", Map{{"port", 8080}, {"host", "localhost"}});
    /// @endcode
    auto put(const ObjId& obj, std::string_view key, const Map& init)
        -> std::expected<ObjId, Error> {
        for (const auto& [entry_key, value] : init.entries) {
            const auto valid_key = validate_key(entry_key);
            if (!valid_key) {
                return std::unexpected{valid_key.error()};
            }
            const auto valid_value = validate_scalar(value);
            if (!valid_value) {
                return std::unexpected{valid_value.error()};
            }
        }
        auto map = put_object(obj, key, ObjType::map);
        if (!map) {
            return std::unexpected{map.error()};
        }
        for (const auto& [entry_key, value] : init.entries) {
            static_cast<void>(put(*map, std::string_view{entry_key}, value));
        }
        return map;
    }

    /// Insert a list at a list index and populate it with initial values.
    auto insert(const ObjId& obj, std::size_t index, const List& init)
        -> std::expected<ObjId, Error> {
        for (const auto& value : init.values) {
            const auto valid = validate_scalar(value);
            if (!valid) {
                return std::unexpected{valid.error()};
            }
        }
        auto list = insert_object(obj, index, ObjType::list);
        if (!list) {
            return std::unexpected{list.error()};
        }
        std::size_t idx = 0;
        for (const auto& val : init.values) {
            static_cast<void>(insert(*list, idx++, val));
        }
        return list;
    }

    /// Insert a map at a list index and populate it with initial entries.
    auto insert(const ObjId& obj, std::size_t index, const Map& init)
        -> std::expected<ObjId, Error> {
        for (const auto& [entry_key, value] : init.entries) {
            const auto valid_key = validate_key(entry_key);
            if (!valid_key) {
                return std::unexpected{valid_key.error()};
            }
            const auto valid_value = validate_scalar(value);
            if (!valid_value) {
                return std::unexpected{valid_value.error()};
            }
        }
        auto map = insert_object(obj, index, ObjType::map);
        if (!map) {
            return std::unexpected{map.error()};
        }
        for (const auto& [entry_key, value] : init.entries) {
            static_cast<void>(put(*map, std::string_view{entry_key}, value));
        }
        return map;
    }

    // -- Raw initializer list overloads (auto-detect list vs map) -------------

    /// Create a list from a bare initializer list of values.
    /// @code
    /// auto items = tx.put(root, "items", {"Milk", "Eggs", "Bread"});
    /// @endcode
    auto put(const ObjId& obj, std::string_view key, std::initializer_list<ScalarValue> values)
        -> std::expected<ObjId, Error> {
        return put(obj, key, List{values});
    }

    /// Create a map from a bare initializer list of key-value pairs.
    /// @code
    /// auto config = tx.put(root, "config", {{"port", 8080}, {"host", "localhost"}});
    /// @endcode
    auto put(const ObjId& obj,
             std::string_view key,
             std::initializer_list<std::pair<std::string_view, ScalarValue>> entries)
        -> std::expected<ObjId, Error> {
        return put(obj, key, Map{entries});
    }

    /// Insert a list from a bare initializer list of values.
    auto insert(const ObjId& obj, std::size_t index, std::initializer_list<ScalarValue> values)
        -> std::expected<ObjId, Error> {
        return insert(obj, index, List{values});
    }

    /// Insert a map from a bare initializer list of key-value pairs.
    auto insert(const ObjId& obj,
                std::size_t index,
                std::initializer_list<std::pair<std::string_view, ScalarValue>> entries)
        -> std::expected<ObjId, Error> {
        return insert(obj, index, Map{entries});
    }

    // -- STL container overloads (create objects from containers) --------------

    /// Create a map at a map key from any associative container (std::map, std::unordered_map,
    /// etc.).
    /// @code
    /// auto dims = std::map<std::string, int>{{"w", 800}, {"h", 600}};
    /// auto obj = tx.put(root, "dims", dims);
    /// @endcode
    template <typename AssocContainer>
        requires requires { typename AssocContainer::mapped_type; } &&
                 std::convertible_to<typename AssocContainer::key_type, std::string_view> &&
                 std::convertible_to<typename AssocContainer::mapped_type, ScalarValue>
    auto put(const ObjId& obj, std::string_view key, const AssocContainer& container)
        -> std::expected<ObjId, Error> {
        for (const auto& entry : container) {
            const auto valid_key = validate_key(std::string_view{entry.first});
            if (!valid_key) {
                return std::unexpected{valid_key.error()};
            }
            const auto valid_value = validate_scalar(ScalarValue{entry.second});
            if (!valid_value) {
                return std::unexpected{valid_value.error()};
            }
        }
        auto map = put_object(obj, key, ObjType::map);
        if (!map) {
            return std::unexpected{map.error()};
        }
        std::ranges::for_each(container, [&](const auto& entry) {
            static_cast<void>(put(*map, std::string_view{entry.first}, ScalarValue{entry.second}));
        });
        return map;
    }

    /// Create a list at a map key from any range (std::vector, std::set, std::deque, etc.).
    /// @code
    /// auto tags = std::vector<std::string>{"crdt", "cpp"};
    /// auto list = tx.put(root, "tags", tags);
    /// @endcode
    template <std::ranges::input_range R>
        requires std::convertible_to<std::ranges::range_value_t<R>, ScalarValue> &&
                 (!requires { typename std::remove_cvref_t<R>::mapped_type; }) &&
                 (!std::is_convertible_v<R, std::string_view>)
    auto put(const ObjId& obj, std::string_view key, R&& range) -> std::expected<ObjId, Error> {
        auto values = std::vector<ScalarValue>{};
        for (auto&& value : range) {
            values.emplace_back(value);
        }
        for (const auto& value : values) {
            const auto valid = validate_scalar(value);
            if (!valid) {
                return std::unexpected{valid.error()};
            }
        }
        auto list = put_object(obj, key, ObjType::list);
        if (!list) {
            return std::unexpected{list.error()};
        }
        std::size_t idx = 0;
        for (auto&& value : values) {
            static_cast<void>(insert(*list, idx++, std::move(value)));
        }
        return list;
    }

    /// Insert a map into a list from any associative container.
    template <typename AssocContainer>
        requires requires { typename AssocContainer::mapped_type; } &&
                 std::convertible_to<typename AssocContainer::key_type, std::string_view> &&
                 std::convertible_to<typename AssocContainer::mapped_type, ScalarValue>
    auto insert(const ObjId& obj, std::size_t index, const AssocContainer& container)
        -> std::expected<ObjId, Error> {
        for (const auto& entry : container) {
            const auto valid_key = validate_key(std::string_view{entry.first});
            if (!valid_key) {
                return std::unexpected{valid_key.error()};
            }
            const auto valid_value = validate_scalar(ScalarValue{entry.second});
            if (!valid_value) {
                return std::unexpected{valid_value.error()};
            }
        }
        auto map = insert_object(obj, index, ObjType::map);
        if (!map) {
            return std::unexpected{map.error()};
        }
        std::ranges::for_each(container, [&](const auto& entry) {
            static_cast<void>(put(*map, std::string_view{entry.first}, ScalarValue{entry.second}));
        });
        return map;
    }

    /// Insert a list into a list from any range.
    template <std::ranges::input_range R>
        requires std::convertible_to<std::ranges::range_value_t<R>, ScalarValue> &&
                 (!requires { typename std::remove_cvref_t<R>::mapped_type; }) &&
                 (!std::is_convertible_v<R, std::string_view>)
    auto insert(const ObjId& obj, std::size_t index, R&& range) -> std::expected<ObjId, Error> {
        auto values = std::vector<ScalarValue>{};
        for (auto&& value : range) {
            values.emplace_back(value);
        }
        for (const auto& value : values) {
            const auto valid = validate_scalar(value);
            if (!valid) {
                return std::unexpected{valid.error()};
            }
        }
        auto list = insert_object(obj, index, ObjType::list);
        if (!list) {
            return std::unexpected{list.error()};
        }
        std::size_t idx = 0;
        for (auto&& value : values) {
            static_cast<void>(insert(*list, idx++, std::move(value)));
        }
        return list;
    }

    // -- Scalar convenience overloads (list set) ------------------------------

    /// Set a std::string at a list index.
    auto set(const ObjId& obj, std::size_t index, std::string val) -> std::expected<void, Error> {
        return set(obj, index, ScalarValue{std::move(val)});
    }
    /// Set a string literal at a list index.
    auto set(const ObjId& obj, std::size_t index, const char* val) -> std::expected<void, Error> {
        if (!val) {
            return std::unexpected{
                Error{ErrorKind::invalid_value_type, "string pointer must not be null"}};
        }
        return set(obj, index, ScalarValue{std::string{val}});
    }
    /// Set a string_view at a list index.
    auto set(const ObjId& obj, std::size_t index, std::string_view val)
        -> std::expected<void, Error> {
        return set(obj, index, ScalarValue{std::string{val}});
    }
    /// Set an int at a list index (promotes to int64_t).
    auto set(const ObjId& obj, std::size_t index, int val) -> std::expected<void, Error> {
        return set(obj, index, ScalarValue{static_cast<std::int64_t>(val)});
    }
    /// Set an int64_t at a list index.
    auto set(const ObjId& obj, std::size_t index, std::int64_t val) -> std::expected<void, Error> {
        return set(obj, index, ScalarValue{val});
    }
    /// Set a uint64_t at a list index.
    auto set(const ObjId& obj, std::size_t index, std::uint64_t val) -> std::expected<void, Error> {
        return set(obj, index, ScalarValue{val});
    }
    /// Set a double at a list index.
    auto set(const ObjId& obj, std::size_t index, double val) -> std::expected<void, Error> {
        return set(obj, index, ScalarValue{val});
    }
    /// Set a bool at a list index.
    auto set(const ObjId& obj, std::size_t index, bool val) -> std::expected<void, Error> {
        return set(obj, index, ScalarValue{val});
    }

    // -- Batch operations -----------------------------------------------------

    /// Batch insert scalars into a list from an initializer list.
    /// @param obj The list object to modify.
    /// @param start The starting index for insertions.
    /// @param values The scalar values to insert sequentially.
    auto insert_all(const ObjId& obj, std::size_t start, std::initializer_list<ScalarValue> values)
        -> std::expected<void, Error> {
        const auto active = validate_active();
        if (!active) {
            return std::unexpected{active.error()};
        }
        const auto object = validate_object(obj, {ObjType::list, ObjType::text});
        if (!object) {
            return std::unexpected{object.error()};
        }
        if (start > state_length(obj)) {
            return std::unexpected{
                Error{ErrorKind::index_out_of_bounds, "batch insertion index is out of bounds"}};
        }
        for (const auto& value : values) {
            const auto valid = validate_scalar(value);
            if (!valid) {
                return std::unexpected{valid.error()};
            }
        }
        auto idx = start;
        for (const auto& val : values) {
            static_cast<void>(insert(obj, idx++, val));
        }
        return {};
    }

    /// Batch put key-value pairs into a map from an initializer list.
    /// @param obj The map object to modify.
    /// @param entries Key-value pairs to set.
    auto put_all(const ObjId& obj,
                 std::initializer_list<std::pair<std::string_view, ScalarValue>> entries)
        -> std::expected<void, Error> {
        const auto active = validate_active();
        if (!active) {
            return std::unexpected{active.error()};
        }
        const auto object = validate_object(obj, {ObjType::map, ObjType::table});
        if (!object) {
            return std::unexpected{object.error()};
        }
        for (const auto& [key, value] : entries) {
            const auto valid_key = validate_key(key);
            if (!valid_key) {
                return std::unexpected{valid_key.error()};
            }
            const auto valid_value = validate_scalar(value);
            if (!valid_value) {
                return std::unexpected{valid_value.error()};
            }
        }
        for (const auto& [key, val] : entries) {
            static_cast<void>(put(obj, key, val));
        }
        return {};
    }

    /// Populate a map from any associative container (std::map, std::unordered_map, etc.).
    template <typename Map>
        requires requires(const Map& m) {
            { m.begin()->first } -> std::convertible_to<std::string_view>;
            { m.begin()->second } -> std::convertible_to<ScalarValue>;
        }
    auto put_map(const ObjId& obj, const Map& map) -> std::expected<void, Error> {
        const auto active = validate_active();
        if (!active) {
            return std::unexpected{active.error()};
        }
        const auto object = validate_object(obj, {ObjType::map, ObjType::table});
        if (!object) {
            return std::unexpected{object.error()};
        }
        for (const auto& [key, value] : map) {
            const auto valid_key = validate_key(std::string_view{key});
            if (!valid_key) {
                return std::unexpected{valid_key.error()};
            }
            const auto valid_value = validate_scalar(ScalarValue{value});
            if (!valid_value) {
                return std::unexpected{valid_value.error()};
            }
        }
        for (const auto& [key, val] : map) {
            static_cast<void>(put(obj, std::string_view{key}, ScalarValue{val}));
        }
        return {};
    }

    /// Insert elements from a vector (or any sized range of ScalarValue-convertible values).
    template <std::ranges::input_range R>
        requires std::convertible_to<std::ranges::range_value_t<R>, ScalarValue>
    auto insert_range(const ObjId& obj, std::size_t start, R&& range)
        -> std::expected<void, Error> {
        auto values = std::vector<ScalarValue>{};
        for (auto&& value : range) {
            values.emplace_back(value);
        }
        const auto active = validate_active();
        if (!active) {
            return std::unexpected{active.error()};
        }
        const auto object = validate_object(obj, {ObjType::list, ObjType::text});
        if (!object) {
            return std::unexpected{object.error()};
        }
        if (start > state_length(obj)) {
            return std::unexpected{
                Error{ErrorKind::index_out_of_bounds, "range insertion index is out of bounds"}};
        }
        for (const auto& value : values) {
            const auto valid = validate_scalar(value);
            if (!valid) {
                return std::unexpected{valid.error()};
            }
        }
        auto idx = start;
        for (auto&& value : values) {
            static_cast<void>(insert(obj, idx++, std::move(value)));
        }
        return {};
    }

    // --- Read methods (no locking, safe because transact holds exclusive lock) ---

    /// Read a scalar value at a map key within the transaction.
    auto get(const ObjId& obj, std::string_view key) const
        -> std::expected<std::optional<Value>, Error>;

    /// Read a scalar value at a list index within the transaction.
    auto get(const ObjId& obj, std::size_t index) const -> std::expected<Value, Error>;

    /// Get the object type of an ObjId within the transaction.
    auto object_type(const ObjId& obj) const -> std::expected<ObjType, Error>;

    /// Get the child ObjId for a map key within the transaction.
    auto get_obj_id(const ObjId& obj, std::string_view key) const
        -> std::expected<std::optional<ObjId>, Error>;

    /// Get the child ObjId for a list index within the transaction.
    auto get_obj_id(const ObjId& obj, std::size_t index) const
        -> std::expected<std::optional<ObjId>, Error>;

    /// Get the length of a list/text object within the transaction.
    auto length(const ObjId& obj) const -> std::expected<std::size_t, Error>;

    /// Get all keys of a map object within the transaction.
    auto keys(const ObjId& obj) const -> std::expected<std::vector<std::string>, Error>;

    /// Get text content within the transaction.
    auto text(const ObjId& obj) const -> std::expected<std::string, Error>;

private:
    auto validate_active() const -> std::expected<void, Error>;
    auto validate_key(std::string_view key) const -> std::expected<void, Error>;
    auto validate_scalar(const ScalarValue& value) const -> std::expected<void, Error>;
    auto validate_object(const ObjId& obj, std::initializer_list<ObjType> allowed_types) const
        -> std::expected<const detail::ObjectState*, Error>;
    auto validate_index(const ObjId& obj, std::size_t index, bool allow_end) const
        -> std::expected<void, Error>;
    auto state_length(const ObjId& obj) const -> std::size_t;

    detail::DocState& state_;
    std::vector<Op> pending_ops_;
    std::unique_ptr<detail::TransactionRollback> rollback_state_;
};

} // namespace automerge_cpp
