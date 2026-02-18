#include <automerge-cpp/document.hpp>

#include "doc_state.hpp"

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

}  // namespace automerge_cpp
