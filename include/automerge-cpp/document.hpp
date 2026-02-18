#pragma once

#include <automerge-cpp/change.hpp>
#include <automerge-cpp/sync_state.hpp>
#include <automerge-cpp/transaction.hpp>
#include <automerge-cpp/types.hpp>
#include <automerge-cpp/value.hpp>

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace automerge_cpp {

namespace detail { struct DocState; }

class Document {
public:
    Document();
    ~Document();

    Document(Document&&) noexcept;
    auto operator=(Document&&) noexcept -> Document&;

    Document(const Document&);
    auto operator=(const Document&) -> Document&;

    // Identity
    auto actor_id() const -> const ActorId&;
    void set_actor_id(ActorId id);

    // Mutation via transaction
    void transact(std::function<void(Transaction&)> fn);

    // Reading — map operations
    auto get(const ObjId& obj, std::string_view key) const -> std::optional<Value>;
    auto get_all(const ObjId& obj, std::string_view key) const -> std::vector<Value>;

    // Reading — list operations
    auto get(const ObjId& obj, std::size_t index) const -> std::optional<Value>;

    // Reading — ranges
    auto keys(const ObjId& obj) const -> std::vector<std::string>;
    auto values(const ObjId& obj) const -> std::vector<Value>;
    auto length(const ObjId& obj) const -> std::size_t;

    // Reading — text
    auto text(const ObjId& obj) const -> std::string;

    // Object type query
    auto object_type(const ObjId& obj) const -> std::optional<ObjType>;

    // Phase 3: Fork and Merge
    auto fork() const -> Document;
    void merge(const Document& other);
    auto get_changes() const -> std::vector<Change>;
    void apply_changes(const std::vector<Change>& changes);
    auto get_heads() const -> std::vector<ChangeHash>;

    // Phase 4: Binary Serialization
    auto save() const -> std::vector<std::byte>;
    static auto load(std::span<const std::byte> data) -> std::optional<Document>;

    // Phase 5: Sync Protocol
    auto generate_sync_message(SyncState& sync_state) const -> std::optional<SyncMessage>;
    void receive_sync_message(SyncState& sync_state, const SyncMessage& message);

private:
    std::unique_ptr<detail::DocState> state_;
};

}  // namespace automerge_cpp
