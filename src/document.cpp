#include <automerge-cpp/document.hpp>

#include "doc_state.hpp"

#include <algorithm>
#include <ranges>

namespace automerge_cpp {

Document::Document()
    : state_{std::make_unique<detail::DocState>()} {}

Document::~Document() = default;

Document::Document(Document&&) noexcept = default;
auto Document::operator=(Document&&) noexcept -> Document& = default;

Document::Document(const Document& other)
    : state_{std::make_unique<detail::DocState>(*other.state_)} {}

auto Document::operator=(const Document& other) -> Document& {
    if (this != &other) {
        state_ = std::make_unique<detail::DocState>(*other.state_);
    }
    return *this;
}

auto Document::actor_id() const -> const ActorId& {
    return state_->actor;
}

void Document::set_actor_id(ActorId id) {
    state_->actor = std::move(id);
}

void Document::transact(std::function<void(Transaction&)> fn) {
    auto tx = Transaction{*state_};
    fn(tx);
    tx.commit();
}

auto Document::get(const ObjId& obj, std::string_view key) const -> std::optional<Value> {
    return state_->map_get(obj, std::string{key});
}

auto Document::get_all(const ObjId& obj, std::string_view key) const -> std::vector<Value> {
    return state_->map_get_all(obj, std::string{key});
}

auto Document::get(const ObjId& obj, std::size_t index) const -> std::optional<Value> {
    return state_->list_get(obj, index);
}

auto Document::keys(const ObjId& obj) const -> std::vector<std::string> {
    return state_->map_keys(obj);
}

auto Document::values(const ObjId& obj) const -> std::vector<Value> {
    const auto type = state_->object_type(obj);
    if (!type) return {};

    switch (*type) {
        case ObjType::map:
        case ObjType::table:
            return state_->map_values(obj);
        case ObjType::list:
        case ObjType::text:
            return state_->list_values(obj);
    }
    return {};
}

auto Document::length(const ObjId& obj) const -> std::size_t {
    return state_->object_length(obj);
}

auto Document::text(const ObjId& obj) const -> std::string {
    return state_->text_content(obj);
}

auto Document::object_type(const ObjId& obj) const -> std::optional<ObjType> {
    return state_->object_type(obj);
}

// -- Phase 3: Fork and Merge --------------------------------------------------

auto Document::fork() const -> Document {
    auto forked = Document{*this};
    // Create a unique actor by incrementing the last byte
    auto new_actor = state_->actor;
    new_actor.bytes[15] = static_cast<std::byte>(
        static_cast<std::uint8_t>(new_actor.bytes[15]) + 1);
    forked.set_actor_id(new_actor);
    return forked;
}

void Document::merge(const Document& other) {
    // Find changes from other that we haven't seen
    auto missing = std::vector<Change>{};
    for (const auto& change : other.state_->change_history) {
        auto it = state_->clock.find(change.actor);
        if (it == state_->clock.end() || it->second < change.seq) {
            missing.push_back(change);
        }
    }
    // Sort by start_op for causal ordering
    std::ranges::sort(missing, [](const Change& a, const Change& b) {
        return a.start_op < b.start_op;
    });
    apply_changes(missing);
}

auto Document::get_changes() const -> std::vector<Change> {
    return state_->change_history;
}

void Document::apply_changes(const std::vector<Change>& changes) {
    for (const auto& change : changes) {
        // Apply each operation
        for (const auto& op : change.operations) {
            state_->apply_op(op);
            state_->op_log.push_back(op);
        }

        // Update clock
        auto& seq = state_->clock[change.actor];
        seq = std::max(seq, change.seq);

        // Update heads
        auto hash = detail::DocState::compute_change_hash(change);
        // Remove deps from heads
        for (const auto& dep : change.deps) {
            std::erase(state_->heads, dep);
        }
        state_->heads.push_back(hash);

        // Store change
        state_->change_history.push_back(change);
    }
}

auto Document::get_heads() const -> std::vector<ChangeHash> {
    return state_->heads;
}

}  // namespace automerge_cpp
