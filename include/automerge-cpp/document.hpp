/// @file document.hpp
/// @brief The Document class -- the primary API for automerge-cpp.

#pragma once

#include <automerge-cpp/change.hpp>
#include <automerge-cpp/cursor.hpp>
#include <automerge-cpp/error.hpp>
#include <automerge-cpp/mark.hpp>
#include <automerge-cpp/patch.hpp>
#include <automerge-cpp/sync_state.hpp>
#include <automerge-cpp/transaction.hpp>
#include <automerge-cpp/types.hpp>
#include <automerge-cpp/value.hpp>

#include <automerge-cpp/thread_pool.hpp>

#include <concepts>
#include <cstddef>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace automerge_cpp {

namespace detail {
struct DocState;

template <typename T> struct IsErrorExpected : std::false_type {};

template <typename T> struct IsErrorExpected<std::expected<T, Error>> : std::true_type {};

template <typename T>
inline constexpr bool is_error_expected_v = IsErrorExpected<std::remove_cvref_t<T>>::value;

template <typename T> struct TransactionResult {
    using type = std::expected<T, Error>;
};

template <typename T> struct TransactionResult<std::expected<T, Error>> {
    using type = std::expected<T, Error>;
};

template <typename T>
using transaction_result_t = typename TransactionResult<std::remove_cvref_t<T>>::type;

template <typename T> struct TransactionPatchResult {
    using type = std::expected<std::pair<T, std::vector<Patch>>, Error>;
};

template <typename T> struct TransactionPatchResult<std::expected<T, Error>> {
    using type = std::expected<std::pair<T, std::vector<Patch>>, Error>;
};

template <> struct TransactionPatchResult<std::expected<void, Error>> {
    using type = std::expected<std::vector<Patch>, Error>;
};

template <typename T>
using transaction_patch_result_t = typename TransactionPatchResult<std::remove_cvref_t<T>>::type;
}  // namespace detail

/// A CRDT document that supports concurrent editing and deterministic merge.
///
/// Document is the primary user-facing type in automerge-cpp. It owns the
/// CRDT state (objects, operations, change history) and provides a
/// transactional mutation API. Documents can be forked, merged, saved to
/// binary, loaded from binary, and synchronized with peers.
///
/// All mutations go through Transaction objects obtained via transact()
/// or transact_with_patches(). Reads can be performed directly on the
/// Document.
///
/// @code
/// auto doc = Document{};
/// doc.transact([](auto& tx) {
///     tx.put(root, "greeting", std::string{"hello"});
/// });
/// auto val = doc.get(root, "greeting");
/// @endcode
class Document {
public:
    /// Construct a new empty document with a random actor ID.
    /// Creates an internal thread pool with hardware_concurrency() threads.
    Document();

    /// Construct with an explicit thread count.
    /// The document receives a random actor ID.
    /// @param num_threads Number of threads for internal parallelism.
    ///   0 = hardware_concurrency(), 1 = sequential (no pool).
    explicit Document(unsigned int num_threads);

    /// Construct with an externally-owned thread pool.
    /// The pool is shared (via shared_ptr) and can be reused across documents.
    /// The document receives a random actor ID.
    /// @param pool The thread pool to use. nullptr = sequential (no pool).
    explicit Document(std::shared_ptr<thread_pool> pool);

    ~Document();

    Document(Document&&) noexcept;
    auto operator=(Document&&) noexcept -> Document&;

    /// Deep-copy a document. The copy is independent and shares no state.
    Document(const Document&);
    /// Deep-copy assignment.
    auto operator=(const Document&) -> Document&;

    // -- Identity -------------------------------------------------------------

    /// Get the actor ID of this document.
    auto actor_id() const -> const ActorId&;

    /// Set the actor ID used for subsequent local changes.
    void set_actor_id(ActorId id);

    // -- Mutation -------------------------------------------------------------

    /// Execute a function within a transaction.
    ///
    /// All operations performed on the Transaction are applied atomically
    /// when the function returns. If the function throws, the transaction
    /// is rolled back.
    /// @param fn A function that receives a Transaction reference.
    /// @param options Optional metadata for the committed change.
    auto transact(const std::function<void(Transaction&)>& fn, CommitOptions options = {})
        -> std::expected<void, Error>;

    /// Execute a function within a transaction and return its result.
    ///
    /// @code
    /// auto list_id = *doc.transact([](Transaction& tx) {
    ///     return tx.put_object(root, "items", ObjType::list);
    /// });
    /// @endcode
    template <typename Fn>
        requires std::invocable<Fn, Transaction&> &&
                 (!std::is_void_v<std::invoke_result_t<Fn, Transaction&>>)
    auto transact(Fn&& fn, CommitOptions options = {})
        -> detail::transaction_result_t<std::invoke_result_t<Fn, Transaction&>>;

    /// Execute a void function within a transaction (template overload).
    template <typename Fn>
        requires std::invocable<Fn, Transaction&> &&
                 std::is_void_v<std::invoke_result_t<Fn, Transaction&>> &&
                 (!std::convertible_to<Fn, std::function<void(Transaction&)>>)
    auto transact(Fn&& fn, CommitOptions options = {}) -> std::expected<void, Error>;

    // -- Reading: map operations ----------------------------------------------

    /// Get the winning value at a map key.
    /// @param obj The map object to read from.
    /// @param key The key to look up.
    /// @return The value, or nullopt if the key doesn't exist.
    auto get(const ObjId& obj, std::string_view key) const
        -> std::expected<std::optional<Value>, Error>;

    /// Get all concurrent values at a map key (for conflict inspection).
    /// @param obj The map object to read from.
    /// @param key The key to look up.
    /// @return All values at this key (empty if key doesn't exist).
    auto get_all(const ObjId& obj, std::string_view key) const
        -> std::expected<std::vector<Value>, Error>;

    // -- Reading: list operations ---------------------------------------------

    /// Get the value at a list index.
    /// @param obj The list object to read from.
    /// @param index The index to look up.
    /// @return The value, or an error if the index is out of bounds.
    auto get(const ObjId& obj, std::size_t index) const
        -> std::expected<std::optional<Value>, Error>;

    // -- Reading: ranges ------------------------------------------------------

    /// Get all keys in a map, sorted lexicographically.
    auto keys(const ObjId& obj) const -> std::expected<std::vector<std::string>, Error>;

    /// Get all values in a map (in key order) or list (in index order).
    auto values(const ObjId& obj) const -> std::expected<std::vector<Value>, Error>;

    /// Get the number of entries in a map or elements in a list. Text elements are Unicode
    /// scalar values.
    auto length(const ObjId& obj) const -> std::expected<std::size_t, Error>;

    // -- Reading: text --------------------------------------------------------

    /// Get the text content of a text object as a string.
    auto text(const ObjId& obj) const -> std::expected<std::string, Error>;

    // -- Typed getters --------------------------------------------------------

    /// Get a typed scalar value from a map key.
    ///
    /// Follows the nlohmann/json `get<T>()` pattern.
    /// @code
    /// auto name = doc.get<std::string>(root, "name");
    /// auto age  = doc.get<std::int64_t>(root, "age");
    /// @endcode
    template <typename T>
    auto get(const ObjId& obj, std::string_view key) const
        -> std::expected<std::optional<T>, Error> {
        auto value = get(obj, key);
        if (!value) {
            return std::unexpected{value.error()};
        }
        return get_scalar<T>(*value);
    }

    /// Get a typed scalar value from a list index.
    /// @code
    /// auto item = doc.get<std::string>(list_id, std::size_t{0});
    /// @endcode
    template <typename T>
    auto get(const ObjId& obj, std::size_t index) const -> std::expected<std::optional<T>, Error> {
        auto value = get(obj, index);
        if (!value) {
            return std::unexpected{value.error()};
        }
        return get_scalar<T>(*value);
    }

    // -- Convenience: root access ---------------------------------------------

    /// Get a value from the root map by key.
    /// @code
    /// auto name = doc["name"];
    /// @endcode
    [[nodiscard]] auto operator[](std::string_view key) const
        -> std::expected<std::optional<Value>, Error> {
        return get(root, key);
    }

    // -- Path-based access ----------------------------------------------------

    /// Get a value at a nested path from root.
    /// Each argument is either a string (map key) or size_t (list index).
    /// @code
    /// auto port = doc.get_path("config", "database", "port");
    /// auto item = doc.get_path("todos", std::size_t{0}, "title");
    /// @endcode
    template <typename... Props>
        requires(sizeof...(Props) > 0)
    auto get_path(Props&&... props) const -> std::expected<std::optional<Value>, Error>;

    // -- Object type query ----------------------------------------------------

    /// Get the type of an object (map, list, text, table).
    /// @return The object type, or an error if the object doesn't exist.
    auto object_type(const ObjId& obj) const -> std::expected<ObjType, Error>;

    // -- Child object lookup --------------------------------------------------

    /// Get the ObjId of a nested object at a map key.
    /// @param obj The parent map object.
    /// @param key The key to look up.
    /// @return The child ObjId, or nullopt if the key doesn't exist or isn't an object.
    auto get_obj_id(const ObjId& obj, std::string_view key) const
        -> std::expected<std::optional<ObjId>, Error>;

    /// Get the ObjId of a nested object at a list index.
    /// @param obj The parent list object.
    /// @param index The index to look up.
    /// @return The child ObjId, or nullopt if the value isn't an object.
    auto get_obj_id(const ObjId& obj, std::size_t index) const
        -> std::expected<std::optional<ObjId>, Error>;

    // -- Fork and Merge -------------------------------------------------------

    /// Create an independent copy with a new random actor ID.
    auto fork() const -> Document;

    /// Merge another document's unseen changes into this one.
    ///
    /// Merge is commutative, associative, and idempotent.
    /// @return An error if a received change contradicts this document's history.
    auto merge(const Document& other) -> std::expected<void, Error>;

    /// Merge another document and return patches for the visible state transition.
    auto merge_with_patches(const Document& other) -> std::expected<std::vector<Patch>, Error>;

    /// Get all changes in this document's history.
    auto get_changes() const -> std::vector<Change>;

    /// Get changes not reachable from the supplied heads.
    auto get_changes_since(const std::vector<ChangeHash>& heads) const
        -> std::expected<std::vector<Change>, Error>;

    /// Get changes present in another document but absent from this document.
    auto get_changes_added(const Document& other) const -> std::vector<Change>;

    /// Get the latest change authored by this document's current actor.
    auto get_last_local_change() const -> std::optional<Change>;

    /// Apply a set of changes from another document.
    ///
    /// Duplicate changes are ignored. Changes whose dependencies are not yet
    /// present are retained until their dependencies arrive. All supplied
    /// changes are validated before the document is modified.
    /// @return An error if a change is malformed or contradicts this document's
    ///   actor sequence history.
    auto apply_changes(const std::vector<Change>& changes) -> std::expected<void, Error>;

    /// Apply external changes and return patches for the visible state transition.
    ///
    /// Patches include queued changes that become ready because of this call.
    auto apply_changes_with_patches(const std::vector<Change>& changes)
        -> std::expected<std::vector<Patch>, Error>;

    /// Get the current DAG leaf hashes (heads).
    auto get_heads() const -> std::vector<ChangeHash>;

    /// Get hashes needed to resolve queued changes and the supplied heads.
    ///
    /// Hashes for changes already queued locally are omitted.
    auto get_missing_deps(const std::vector<ChangeHash>& heads) const -> std::vector<ChangeHash>;

    // -- Binary Serialization -------------------------------------------------

    /// Serialize the complete history as upstream-compatible change chunks.
    /// Also advances the cursor used by save_incremental().
    auto save() const -> std::vector<std::byte>;

    /// Serialize changes added since the latest save or incremental save.
    /// @return Concatenated upstream-compatible change chunks, or an empty
    ///   vector when there are no new changes.
    auto save_incremental() const -> std::vector<std::byte>;

    /// Serialize the current document as one upstream compact document chunk.
    auto save_compact() const -> std::vector<std::byte>;

    /// Apply change chunks or an upstream document chunk to this document.
    /// @return The number of changes applied immediately. Changes waiting for
    ///   unavailable dependencies are retained by the checked apply path.
    auto load_incremental(std::span<const std::byte> data) -> std::expected<std::size_t, Error>;

    /// Load a document from binary data.
    ///
    /// Supports both v2 (chunk-based) and v1 (row-based) formats with
    /// automatic detection.
    /// @param data The binary data to load from.
    /// @return The loaded document, or an error if the data is invalid.
    static auto load(std::span<const std::byte> data) -> std::expected<Document, Error>;

    /// Load one upstream compact document chunk as a shallow checkpoint.
    ///
    /// The visible state and checkpoint heads are retained. Changes and operations before the
    /// checkpoint are not kept in the resident history.
    static auto load_shallow(std::span<const std::byte> data) -> std::expected<Document, Error>;

    /// Load a document and return patches that materialize its visible state.
    ///
    /// The output document is replaced only after the input is validated.
    /// @param data The binary data to load from.
    /// @param document Receives the loaded document on success.
    /// @return Patches from an empty document to the loaded state, or an error.
    static auto load_with_patches(std::span<const std::byte> data, Document& document)
        -> std::expected<std::vector<Patch>, Error>;

    // -- Sync Protocol --------------------------------------------------------

    /// Generate the next sync message to send to a peer.
    /// @param sync_state The per-peer sync state (modified in place).
    /// @return The message to send, or nullopt if no message is needed.
    auto generate_sync_message(SyncState& sync_state) const -> std::optional<SyncMessage>;

    /// Process a sync message received from a peer.
    /// @param sync_state The per-peer sync state (modified in place).
    /// @param message The received message to process.
    /// @return An error if a received change is malformed or contradicts this
    ///   document's actor sequence history.
    auto receive_sync_message(SyncState& sync_state, const SyncMessage& message)
        -> std::expected<void, Error>;

    /// Process a sync message and return patches for the visible state transition.
    auto receive_sync_message_with_patches(SyncState& sync_state, const SyncMessage& message)
        -> std::expected<std::vector<Patch>, Error>;

    // -- Patches --------------------------------------------------------------

    /// Execute a transaction and return patches describing the changes.
    ///
    /// Patches describe the externally visible effects of the transaction
    /// (puts, inserts, deletes, increments, text splices).
    /// @param fn A function that receives a Transaction reference.
    /// @param options Optional metadata for the committed change.
    /// @return The patches produced by the transaction.
    auto transact_with_patches(const std::function<void(Transaction&)>& fn,
                               CommitOptions options = {})
        -> std::expected<std::vector<Patch>, Error>;

    /// Execute a transaction, return both the function result and patches.
    /// @code
    /// auto [list_id, patches] = *doc.transact_with_patches([](Transaction& tx) {
    ///     return tx.put_object(root, "items", ObjType::list);
    /// });
    /// @endcode
    template <typename Fn>
        requires std::invocable<Fn, Transaction&> &&
                 (!std::is_void_v<std::invoke_result_t<Fn, Transaction&>>)
    auto transact_with_patches(Fn&& fn, CommitOptions options = {})
        -> detail::transaction_patch_result_t<std::invoke_result_t<Fn, Transaction&>>;

    // -- Historical reads (time travel) ---------------------------------------

    /// Get a map value as it was at a given point in history.
    /// @param obj The map object.
    /// @param key The key to look up.
    /// @param heads The DAG heads defining the point in time.
    auto get_at(const ObjId& obj, std::string_view key, const std::vector<ChangeHash>& heads) const
        -> std::expected<std::optional<Value>, Error>;

    /// Get a list value as it was at a given point in history.
    auto get_at(const ObjId& obj, std::size_t index, const std::vector<ChangeHash>& heads) const
        -> std::expected<std::optional<Value>, Error>;

    /// Get map keys at a given point in history.
    auto keys_at(const ObjId& obj, const std::vector<ChangeHash>& heads) const
        -> std::expected<std::vector<std::string>, Error>;

    /// Get values at a given point in history.
    auto values_at(const ObjId& obj, const std::vector<ChangeHash>& heads) const
        -> std::expected<std::vector<Value>, Error>;

    /// Get length at a given point in history.
    auto length_at(const ObjId& obj, const std::vector<ChangeHash>& heads) const
        -> std::expected<std::size_t, Error>;

    /// Get text content at a given point in history.
    auto text_at(const ObjId& obj, const std::vector<ChangeHash>& heads) const
        -> std::expected<std::string, Error>;

    // -- Cursors --------------------------------------------------------------

    /// Create a cursor at a position in a list or text.
    /// @param obj The list or text object.
    /// @param index The current list-element or text-scalar index to anchor the cursor to.
    /// @return A cursor, or an error if the index is out of bounds.
    auto cursor(const ObjId& obj, std::size_t index) const -> std::expected<Cursor, Error>;

    /// Resolve a cursor to its current index.
    /// @param obj The list or text object.
    /// @param cursor The cursor to resolve.
    /// @return The current list-element or text-scalar index, or nullopt if the element was
    /// deleted.
    auto resolve_cursor(const ObjId& obj, const Cursor& cursor) const
        -> std::expected<std::optional<std::size_t>, Error>;

    // -- Rich text marks ------------------------------------------------------

    /// Get all marks on a text or list object.
    auto marks(const ObjId& obj) const -> std::expected<std::vector<Mark>, Error>;

    /// Get marks at a given point in history.
    auto marks_at(const ObjId& obj, const std::vector<ChangeHash>& heads) const
        -> std::expected<std::vector<Mark>, Error>;

    /// Get the thread pool (may be nullptr if sequential mode).
    auto get_thread_pool() const -> std::shared_ptr<thread_pool>;

    // -- Locking control ------------------------------------------------------

    /// Enable or disable internal read locking.
    ///
    /// When enabled (default), every read method acquires a shared_lock
    /// for safe concurrent access with writers. When disabled, read
    /// methods skip the lock entirely — the caller must guarantee no
    /// concurrent writes during reads. Disabling gives near-linear
    /// read scaling across cores by eliminating cache-line contention
    /// on the shared_mutex reader count.
    void set_read_locking(bool enabled);

    /// Check whether internal read locking is enabled.
    auto read_locking() const -> bool;

private:
    /// RAII guard that conditionally acquires a shared_lock.
    struct ReadGuard {
        std::shared_lock<std::shared_mutex> lock_;
        bool engaged_;

        explicit ReadGuard(std::shared_mutex& mtx, bool engage)
            : lock_{mtx, std::defer_lock}, engaged_{engage} {
            if (engaged_) lock_.lock();
        }
    };

    auto read_guard() const -> ReadGuard;

    /// Internal: convert pending ops to patches.
    static auto ops_to_patches_internal(const std::vector<Op>& ops) -> std::vector<Patch>;

    /// Update peer bookkeeping after received changes have been admitted.
    void update_sync_state_after_receive(SyncState& sync_state, const SyncMessage& message);

    /// Internal: walk a path of Props to resolve a nested value.
    auto get_path_impl(std::span<const Prop> path) const
        -> std::expected<std::optional<Value>, Error>;

    std::unique_ptr<detail::DocState> state_;
    std::shared_ptr<thread_pool> pool_;
    mutable std::vector<ChangeHash> save_heads_;
    mutable std::shared_mutex mutex_;
    bool read_locking_ = true;
};

// -- Template implementations (must be in header) ----------------------------

template <typename Fn>
    requires std::invocable<Fn, Transaction&> &&
             (!std::is_void_v<std::invoke_result_t<Fn, Transaction&>>)
auto Document::transact(Fn&& fn, CommitOptions options)
    -> detail::transaction_result_t<std::invoke_result_t<Fn, Transaction&>> {
    auto lock = std::unique_lock{mutex_};
    auto tx = Transaction{*state_};
    try {
        auto result = fn(tx);
        if constexpr (detail::is_error_expected_v<decltype(result)>) {
            if (!result) {
                static_cast<void>(tx.rollback());
                return std::unexpected{result.error()};
            }
        }
        const auto committed = tx.commit(std::move(options));
        if (!committed) {
            return std::unexpected{committed.error()};
        }
        if constexpr (detail::is_error_expected_v<decltype(result)>) {
            return result;
        } else {
            return std::expected<std::remove_cvref_t<decltype(result)>, Error>{std::move(result)};
        }
    } catch (...) {
        static_cast<void>(tx.rollback());
        throw;
    }
}

template <typename Fn>
    requires std::invocable<Fn, Transaction&> &&
             std::is_void_v<std::invoke_result_t<Fn, Transaction&>> &&
             (!std::convertible_to<Fn, std::function<void(Transaction&)>>)
auto Document::transact(Fn&& fn, CommitOptions options) -> std::expected<void, Error> {
    auto lock = std::unique_lock{mutex_};
    auto tx = Transaction{*state_};
    try {
        fn(tx);
        return tx.commit(std::move(options));
    } catch (...) {
        static_cast<void>(tx.rollback());
        throw;
    }
}

template <typename Fn>
    requires std::invocable<Fn, Transaction&> &&
             (!std::is_void_v<std::invoke_result_t<Fn, Transaction&>>)
auto Document::transact_with_patches(Fn&& fn, CommitOptions options)
    -> detail::transaction_patch_result_t<std::invoke_result_t<Fn, Transaction&>> {
    auto lock = std::unique_lock{mutex_};
    auto tx = Transaction{*state_};
    try {
        auto result = fn(tx);
        if constexpr (detail::is_error_expected_v<decltype(result)>) {
            if (!result) {
                static_cast<void>(tx.rollback());
                return std::unexpected{result.error()};
            }
        }
        auto ops = tx.pending_ops_;
        const auto committed = tx.commit(std::move(options));
        if (!committed) {
            return std::unexpected{committed.error()};
        }
        auto patches = ops_to_patches_internal(ops);
        if constexpr (detail::is_error_expected_v<decltype(result)>) {
            using Value = typename std::remove_cvref_t<decltype(result)>::value_type;
            if constexpr (std::is_void_v<Value>) {
                return patches;
            } else {
                return std::pair<Value, std::vector<Patch>>{std::move(*result), std::move(patches)};
            }
        } else {
            return std::pair<std::remove_cvref_t<decltype(result)>, std::vector<Patch>>{
                std::move(result), std::move(patches)};
        }
    } catch (...) {
        static_cast<void>(tx.rollback());
        throw;
    }
}

template <typename... Props>
    requires(sizeof...(Props) > 0)
auto Document::get_path(Props&&... props) const -> std::expected<std::optional<Value>, Error> {
    auto path = std::vector<Prop>{};
    path.reserve(sizeof...(Props));
    auto has_null_key = false;
    auto to_prop = overload{
        [](std::string_view s) -> Prop { return std::string{s}; },
        [&has_null_key](const char* s) -> Prop {
            if (s == nullptr) {
                has_null_key = true;
                return std::string{};
            }
            return std::string{s};
        },
        [](const std::string& s) -> Prop { return s; },
        [](std::size_t i) -> Prop { return i; },
        [](int i) -> Prop { return static_cast<std::size_t>(i); },
    };
    (path.push_back(to_prop(std::forward<Props>(props))), ...);
    if (has_null_key) {
        return std::unexpected{Error{ErrorKind::invalid_key, "Path keys must not be null"}};
    }
    return get_path_impl(path);
}

}  // namespace automerge_cpp
