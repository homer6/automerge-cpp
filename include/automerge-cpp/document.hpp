/// @file document.hpp
/// @brief The Document class -- the primary API for automerge-cpp.

#pragma once

#include <automerge-cpp/change.hpp>
#include <automerge-cpp/cursor.hpp>
#include <automerge-cpp/mark.hpp>
#include <automerge-cpp/patch.hpp>
#include <automerge-cpp/sync_state.hpp>
#include <automerge-cpp/transaction.hpp>
#include <automerge-cpp/types.hpp>
#include <automerge-cpp/value.hpp>

#include <automerge-cpp/thread_pool.hpp>

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace automerge_cpp {

namespace detail {
struct DocState;
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
    /// @param num_threads Number of threads for internal parallelism.
    ///   0 = hardware_concurrency(), 1 = sequential (no pool).
    explicit Document(unsigned int num_threads);

    /// Construct with an externally-owned thread pool.
    /// The pool is shared (via shared_ptr) and can be reused across documents.
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

    /// Set the actor ID. Must be called before any mutations.
    void set_actor_id(ActorId id);

    // -- Mutation -------------------------------------------------------------

    /// Execute a function within a transaction.
    ///
    /// All operations performed on the Transaction are applied atomically
    /// when the function returns. If the function throws, the transaction
    /// is rolled back.
    /// @param fn A function that receives a Transaction reference.
    void transact(const std::function<void(Transaction&)>& fn);

    // -- Reading: map operations ----------------------------------------------

    /// Get the winning value at a map key.
    /// @param obj The map object to read from.
    /// @param key The key to look up.
    /// @return The value, or nullopt if the key doesn't exist.
    auto get(const ObjId& obj, std::string_view key) const -> std::optional<Value>;

    /// Get all concurrent values at a map key (for conflict inspection).
    /// @param obj The map object to read from.
    /// @param key The key to look up.
    /// @return All values at this key (empty if key doesn't exist).
    auto get_all(const ObjId& obj, std::string_view key) const -> std::vector<Value>;

    // -- Reading: list operations ---------------------------------------------

    /// Get the value at a list index.
    /// @param obj The list object to read from.
    /// @param index The index to look up.
    /// @return The value, or nullopt if the index is out of bounds.
    auto get(const ObjId& obj, std::size_t index) const -> std::optional<Value>;

    // -- Reading: ranges ------------------------------------------------------

    /// Get all keys in a map, sorted lexicographically.
    auto keys(const ObjId& obj) const -> std::vector<std::string>;

    /// Get all values in a map (in key order) or list (in index order).
    auto values(const ObjId& obj) const -> std::vector<Value>;

    /// Get the number of entries in a map or elements in a list/text.
    auto length(const ObjId& obj) const -> std::size_t;

    // -- Reading: text --------------------------------------------------------

    /// Get the text content of a text object as a string.
    auto text(const ObjId& obj) const -> std::string;

    // -- Object type query ----------------------------------------------------

    /// Get the type of an object (map, list, text, table).
    /// @return The object type, or nullopt if the object doesn't exist.
    auto object_type(const ObjId& obj) const -> std::optional<ObjType>;

    // -- Fork and Merge -------------------------------------------------------

    /// Create an independent copy with a new actor ID.
    auto fork() const -> Document;

    /// Merge another document's unseen changes into this one.
    ///
    /// Merge is commutative, associative, and idempotent.
    void merge(const Document& other);

    /// Get all changes in this document's history.
    auto get_changes() const -> std::vector<Change>;

    /// Apply a set of changes from another document.
    void apply_changes(const std::vector<Change>& changes);

    /// Get the current DAG leaf hashes (heads).
    auto get_heads() const -> std::vector<ChangeHash>;

    // -- Binary Serialization -------------------------------------------------

    /// Serialize the document to binary format.
    /// @return The serialized bytes (v2 chunk-based format).
    auto save() const -> std::vector<std::byte>;

    /// Load a document from binary data.
    ///
    /// Supports both v2 (chunk-based) and v1 (row-based) formats with
    /// automatic detection.
    /// @param data The binary data to load from.
    /// @return The loaded document, or nullopt if the data is invalid.
    static auto load(std::span<const std::byte> data) -> std::optional<Document>;

    // -- Sync Protocol --------------------------------------------------------

    /// Generate the next sync message to send to a peer.
    /// @param sync_state The per-peer sync state (modified in place).
    /// @return The message to send, or nullopt if no message is needed.
    auto generate_sync_message(SyncState& sync_state) const -> std::optional<SyncMessage>;

    /// Process a sync message received from a peer.
    /// @param sync_state The per-peer sync state (modified in place).
    /// @param message The received message to process.
    void receive_sync_message(SyncState& sync_state, const SyncMessage& message);

    // -- Patches --------------------------------------------------------------

    /// Execute a transaction and return patches describing the changes.
    ///
    /// Patches describe the externally visible effects of the transaction
    /// (puts, inserts, deletes, increments, text splices).
    /// @param fn A function that receives a Transaction reference.
    /// @return The patches produced by the transaction.
    auto transact_with_patches(const std::function<void(Transaction&)>& fn) -> std::vector<Patch>;

    // -- Historical reads (time travel) ---------------------------------------

    /// Get a map value as it was at a given point in history.
    /// @param obj The map object.
    /// @param key The key to look up.
    /// @param heads The DAG heads defining the point in time.
    auto get_at(const ObjId& obj, std::string_view key,
                const std::vector<ChangeHash>& heads) const -> std::optional<Value>;

    /// Get a list value as it was at a given point in history.
    auto get_at(const ObjId& obj, std::size_t index,
                const std::vector<ChangeHash>& heads) const -> std::optional<Value>;

    /// Get map keys at a given point in history.
    auto keys_at(const ObjId& obj,
                 const std::vector<ChangeHash>& heads) const -> std::vector<std::string>;

    /// Get values at a given point in history.
    auto values_at(const ObjId& obj,
                   const std::vector<ChangeHash>& heads) const -> std::vector<Value>;

    /// Get length at a given point in history.
    auto length_at(const ObjId& obj,
                   const std::vector<ChangeHash>& heads) const -> std::size_t;

    /// Get text content at a given point in history.
    auto text_at(const ObjId& obj,
                 const std::vector<ChangeHash>& heads) const -> std::string;

    // -- Cursors --------------------------------------------------------------

    /// Create a cursor at a position in a list or text.
    /// @param obj The list or text object.
    /// @param index The current index to anchor the cursor to.
    /// @return A Cursor, or nullopt if the index is out of bounds.
    auto cursor(const ObjId& obj, std::size_t index) const -> std::optional<Cursor>;

    /// Resolve a cursor to its current index.
    /// @param obj The list or text object.
    /// @param cursor The cursor to resolve.
    /// @return The current index, or nullopt if the element was deleted.
    auto resolve_cursor(const ObjId& obj, const Cursor& cursor) const -> std::optional<std::size_t>;

    // -- Rich text marks ------------------------------------------------------

    /// Get all marks on a text or list object.
    auto marks(const ObjId& obj) const -> std::vector<Mark>;

    /// Get marks at a given point in history.
    auto marks_at(const ObjId& obj,
                  const std::vector<ChangeHash>& heads) const -> std::vector<Mark>;

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

    std::unique_ptr<detail::DocState> state_;
    std::shared_ptr<thread_pool> pool_;
    mutable std::shared_mutex mutex_;
    bool read_locking_ = true;
};

}  // namespace automerge_cpp
