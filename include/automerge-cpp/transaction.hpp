/// @file transaction.hpp
/// @brief Transaction class for mutating documents.

#pragma once

#include <automerge-cpp/op.hpp>
#include <automerge-cpp/types.hpp>
#include <automerge-cpp/value.hpp>

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <initializer_list>
#include <ranges>
#include <string_view>
#include <type_traits>
#include <utility>
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

    // -- Scalar convenience overloads (map key) -------------------------------

    /// Put a std::string at a map key.
    void put(const ObjId& obj, std::string_view key, std::string val) {
        put(obj, key, ScalarValue{std::move(val)});
    }
    /// Put a string literal at a map key.
    void put(const ObjId& obj, std::string_view key, const char* val) {
        put(obj, key, ScalarValue{std::string{val}});
    }
    /// Put a string_view at a map key (copies into the document).
    void put(const ObjId& obj, std::string_view key, std::string_view val) {
        put(obj, key, ScalarValue{std::string{val}});
    }
    /// Put an int at a map key (promotes to int64_t).
    void put(const ObjId& obj, std::string_view key, int val) {
        put(obj, key, ScalarValue{static_cast<std::int64_t>(val)});
    }
    /// Put an int64_t at a map key.
    void put(const ObjId& obj, std::string_view key, std::int64_t val) {
        put(obj, key, ScalarValue{val});
    }
    /// Put a uint64_t at a map key.
    void put(const ObjId& obj, std::string_view key, std::uint64_t val) {
        put(obj, key, ScalarValue{val});
    }
    /// Put a double at a map key.
    void put(const ObjId& obj, std::string_view key, double val) {
        put(obj, key, ScalarValue{val});
    }
    /// Put a bool at a map key.
    void put(const ObjId& obj, std::string_view key, bool val) {
        put(obj, key, ScalarValue{val});
    }
    /// Put a Null at a map key.
    void put(const ObjId& obj, std::string_view key, Null val) {
        put(obj, key, ScalarValue{val});
    }
    /// Put a Counter at a map key.
    void put(const ObjId& obj, std::string_view key, Counter val) {
        put(obj, key, ScalarValue{val});
    }
    /// Put a Timestamp at a map key.
    void put(const ObjId& obj, std::string_view key, Timestamp val) {
        put(obj, key, ScalarValue{val});
    }

    /// Create a nested object at a map key (convenience overload for put_object).
    /// @param obj The map object to modify.
    /// @param key The key to set.
    /// @param type The type of nested object to create (ObjType::map, ObjType::list, etc.).
    /// @return The ObjId of the newly created object.
    auto put(const ObjId& obj, std::string_view key, ObjType type) -> ObjId {
        return put_object(obj, key, type);
    }

    // -- Scalar convenience overloads (list insert) ---------------------------

    /// Insert a std::string into a list.
    void insert(const ObjId& obj, std::size_t index, std::string val) {
        insert(obj, index, ScalarValue{std::move(val)});
    }
    /// Insert a string literal into a list.
    void insert(const ObjId& obj, std::size_t index, const char* val) {
        insert(obj, index, ScalarValue{std::string{val}});
    }
    /// Insert a string_view into a list.
    void insert(const ObjId& obj, std::size_t index, std::string_view val) {
        insert(obj, index, ScalarValue{std::string{val}});
    }
    /// Insert an int into a list (promotes to int64_t).
    void insert(const ObjId& obj, std::size_t index, int val) {
        insert(obj, index, ScalarValue{static_cast<std::int64_t>(val)});
    }
    /// Insert an int64_t into a list.
    void insert(const ObjId& obj, std::size_t index, std::int64_t val) {
        insert(obj, index, ScalarValue{val});
    }
    /// Insert a uint64_t into a list.
    void insert(const ObjId& obj, std::size_t index, std::uint64_t val) {
        insert(obj, index, ScalarValue{val});
    }
    /// Insert a double into a list.
    void insert(const ObjId& obj, std::size_t index, double val) {
        insert(obj, index, ScalarValue{val});
    }
    /// Insert a bool into a list.
    void insert(const ObjId& obj, std::size_t index, bool val) {
        insert(obj, index, ScalarValue{val});
    }

    /// Insert a nested object into a list (convenience overload for insert_object).
    auto insert(const ObjId& obj, std::size_t index, ObjType type) -> ObjId {
        return insert_object(obj, index, type);
    }

    // -- Initializer list overloads (List / Map) ------------------------------

    /// Create a list at a map key and populate it with initial values.
    /// @code
    /// auto items = tx.put(root, "items", List{"Milk", "Eggs", "Bread"});
    /// @endcode
    auto put(const ObjId& obj, std::string_view key, const List& init) -> ObjId {
        auto list = put_object(obj, key, ObjType::list);
        std::size_t idx = 0;
        for (const auto& val : init.values) insert(list, idx++, val);
        return list;
    }

    /// Create a map at a map key and populate it with initial entries.
    /// @code
    /// auto config = tx.put(root, "config", Map{{"port", 8080}, {"host", "localhost"}});
    /// @endcode
    auto put(const ObjId& obj, std::string_view key, const Map& init) -> ObjId {
        auto map = put_object(obj, key, ObjType::map);
        for (const auto& [k, v] : init.entries) put(map, std::string_view{k}, v);
        return map;
    }

    /// Insert a list at a list index and populate it with initial values.
    auto insert(const ObjId& obj, std::size_t index, const List& init) -> ObjId {
        auto list = insert_object(obj, index, ObjType::list);
        std::size_t idx = 0;
        for (const auto& val : init.values) insert(list, idx++, val);
        return list;
    }

    /// Insert a map at a list index and populate it with initial entries.
    auto insert(const ObjId& obj, std::size_t index, const Map& init) -> ObjId {
        auto map = insert_object(obj, index, ObjType::map);
        for (const auto& [k, v] : init.entries) put(map, std::string_view{k}, v);
        return map;
    }

    // -- Raw initializer list overloads (auto-detect list vs map) -------------

    /// Create a list from a bare initializer list of values.
    /// @code
    /// auto items = tx.put(root, "items", {"Milk", "Eggs", "Bread"});
    /// @endcode
    auto put(const ObjId& obj, std::string_view key,
             std::initializer_list<ScalarValue> values) -> ObjId {
        auto list = put_object(obj, key, ObjType::list);
        std::size_t idx = 0;
        for (const auto& val : values) insert(list, idx++, val);
        return list;
    }

    /// Create a map from a bare initializer list of key-value pairs.
    /// @code
    /// auto config = tx.put(root, "config", {{"port", 8080}, {"host", "localhost"}});
    /// @endcode
    auto put(const ObjId& obj, std::string_view key,
             std::initializer_list<std::pair<std::string_view, ScalarValue>> entries) -> ObjId {
        auto map = put_object(obj, key, ObjType::map);
        for (const auto& [k, v] : entries) put(map, k, v);
        return map;
    }

    /// Insert a list from a bare initializer list of values.
    auto insert(const ObjId& obj, std::size_t index,
                std::initializer_list<ScalarValue> values) -> ObjId {
        auto list = insert_object(obj, index, ObjType::list);
        std::size_t idx = 0;
        for (const auto& val : values) insert(list, idx++, val);
        return list;
    }

    /// Insert a map from a bare initializer list of key-value pairs.
    auto insert(const ObjId& obj, std::size_t index,
                std::initializer_list<std::pair<std::string_view, ScalarValue>> entries) -> ObjId {
        auto map = insert_object(obj, index, ObjType::map);
        for (const auto& [k, v] : entries) put(map, std::string_view{k}, v);
        return map;
    }

    // -- STL container overloads (create objects from containers) --------------

    /// Create a map at a map key from any associative container (std::map, std::unordered_map, etc.).
    /// @code
    /// auto dims = std::map<std::string, int>{{"w", 800}, {"h", 600}};
    /// auto obj = tx.put(root, "dims", dims);
    /// @endcode
    template <typename AssocContainer>
        requires requires { typename AssocContainer::mapped_type; }
              && std::convertible_to<typename AssocContainer::key_type, std::string_view>
              && std::convertible_to<typename AssocContainer::mapped_type, ScalarValue>
    auto put(const ObjId& obj, std::string_view key, const AssocContainer& container) -> ObjId {
        auto map = put_object(obj, key, ObjType::map);
        std::ranges::for_each(container, [&](const auto& entry) {
            put(map, std::string_view{entry.first}, ScalarValue{entry.second});
        });
        return map;
    }

    /// Create a list at a map key from any range (std::vector, std::set, std::deque, etc.).
    /// @code
    /// auto tags = std::vector<std::string>{"crdt", "cpp"};
    /// auto list = tx.put(root, "tags", tags);
    /// @endcode
    template <std::ranges::input_range R>
        requires std::convertible_to<std::ranges::range_value_t<R>, ScalarValue>
              && (!requires { typename std::remove_cvref_t<R>::mapped_type; })
              && (!std::is_convertible_v<R, std::string_view>)
    auto put(const ObjId& obj, std::string_view key, R&& range) -> ObjId {
        auto list = put_object(obj, key, ObjType::list);
        std::size_t idx = 0;
        for (auto&& val : range) {
            insert(list, idx++, ScalarValue{val});
        }
        return list;
    }

    /// Insert a map into a list from any associative container.
    template <typename AssocContainer>
        requires requires { typename AssocContainer::mapped_type; }
              && std::convertible_to<typename AssocContainer::key_type, std::string_view>
              && std::convertible_to<typename AssocContainer::mapped_type, ScalarValue>
    auto insert(const ObjId& obj, std::size_t index, const AssocContainer& container) -> ObjId {
        auto map = insert_object(obj, index, ObjType::map);
        std::ranges::for_each(container, [&](const auto& entry) {
            put(map, std::string_view{entry.first}, ScalarValue{entry.second});
        });
        return map;
    }

    /// Insert a list into a list from any range.
    template <std::ranges::input_range R>
        requires std::convertible_to<std::ranges::range_value_t<R>, ScalarValue>
              && (!requires { typename std::remove_cvref_t<R>::mapped_type; })
              && (!std::is_convertible_v<R, std::string_view>)
    auto insert(const ObjId& obj, std::size_t index, R&& range) -> ObjId {
        auto list = insert_object(obj, index, ObjType::list);
        std::size_t idx = 0;
        for (auto&& val : range) {
            insert(list, idx++, ScalarValue{val});
        }
        return list;
    }

    // -- Scalar convenience overloads (list set) ------------------------------

    /// Set a std::string at a list index.
    void set(const ObjId& obj, std::size_t index, std::string val) {
        set(obj, index, ScalarValue{std::move(val)});
    }
    /// Set a string literal at a list index.
    void set(const ObjId& obj, std::size_t index, const char* val) {
        set(obj, index, ScalarValue{std::string{val}});
    }
    /// Set a string_view at a list index.
    void set(const ObjId& obj, std::size_t index, std::string_view val) {
        set(obj, index, ScalarValue{std::string{val}});
    }
    /// Set an int at a list index (promotes to int64_t).
    void set(const ObjId& obj, std::size_t index, int val) {
        set(obj, index, ScalarValue{static_cast<std::int64_t>(val)});
    }
    /// Set an int64_t at a list index.
    void set(const ObjId& obj, std::size_t index, std::int64_t val) {
        set(obj, index, ScalarValue{val});
    }
    /// Set a uint64_t at a list index.
    void set(const ObjId& obj, std::size_t index, std::uint64_t val) {
        set(obj, index, ScalarValue{val});
    }
    /// Set a double at a list index.
    void set(const ObjId& obj, std::size_t index, double val) {
        set(obj, index, ScalarValue{val});
    }
    /// Set a bool at a list index.
    void set(const ObjId& obj, std::size_t index, bool val) {
        set(obj, index, ScalarValue{val});
    }

    // -- Batch operations -----------------------------------------------------

    /// Batch insert scalars into a list from an initializer list.
    /// @param obj The list object to modify.
    /// @param start The starting index for insertions.
    /// @param values The scalar values to insert sequentially.
    void insert_all(const ObjId& obj, std::size_t start,
                    std::initializer_list<ScalarValue> values) {
        auto idx = start;
        for (const auto& val : values) {
            insert(obj, idx++, val);
        }
    }

    /// Batch put key-value pairs into a map from an initializer list.
    /// @param obj The map object to modify.
    /// @param entries Key-value pairs to set.
    void put_all(const ObjId& obj,
                 std::initializer_list<std::pair<std::string_view, ScalarValue>> entries) {
        for (const auto& [key, val] : entries) {
            put(obj, key, val);
        }
    }

    /// Populate a map from any associative container (std::map, std::unordered_map, etc.).
    template <typename Map>
        requires requires(const Map& m) {
            { m.begin()->first } -> std::convertible_to<std::string_view>;
            { m.begin()->second } -> std::convertible_to<ScalarValue>;
        }
    void put_map(const ObjId& obj, const Map& map) {
        for (const auto& [key, val] : map) {
            put(obj, std::string_view{key}, ScalarValue{val});
        }
    }

    /// Insert elements from a vector (or any sized range of ScalarValue-convertible values).
    template <std::ranges::input_range R>
        requires std::convertible_to<std::ranges::range_value_t<R>, ScalarValue>
    void insert_range(const ObjId& obj, std::size_t start, R&& range) {
        auto idx = start;
        for (auto&& val : range) {
            insert(obj, idx++, ScalarValue{std::forward<decltype(val)>(val)});
        }
    }

    // --- Read methods (no locking, safe because transact holds exclusive lock) ---

    /// Read a scalar value at a map key within the transaction.
    auto get(const ObjId& obj, std::string_view key) const -> std::optional<Value>;

    /// Read a scalar value at a list index within the transaction.
    auto get(const ObjId& obj, std::size_t index) const -> std::optional<Value>;

    /// Get the object type of an ObjId within the transaction.
    auto object_type(const ObjId& obj) const -> std::optional<ObjType>;

    /// Get the child ObjId for a map key within the transaction.
    auto get_obj_id(const ObjId& obj, std::string_view key) const -> std::optional<ObjId>;

    /// Get the child ObjId for a list index within the transaction.
    auto get_obj_id(const ObjId& obj, std::size_t index) const -> std::optional<ObjId>;

    /// Get the length of a list/text object within the transaction.
    auto length(const ObjId& obj) const -> std::size_t;

    /// Get all keys of a map object within the transaction.
    auto keys(const ObjId& obj) const -> std::vector<std::string>;

    /// Get text content within the transaction.
    auto text(const ObjId& obj) const -> std::string;

private:
    void commit();

    detail::DocState& state_;
    std::vector<Op> pending_ops_;
    std::uint64_t start_op_{0};
};

}  // namespace automerge_cpp
