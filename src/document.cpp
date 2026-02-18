#include <automerge-cpp/document.hpp>

#include "doc_state.hpp"
#include "storage/serializer.hpp"
#include "storage/deserializer.hpp"

#include <algorithm>
#include <ranges>
#include <unordered_set>

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

// -- Phase 4: Binary Serialization --------------------------------------------

// Format: Magic(4) + Version(1) + ActorTable + Changes + Heads + LocalState

static constexpr std::uint8_t MAGIC[] = {0x85, 0x6F, 0x4A, 0x83};
static constexpr std::uint8_t FORMAT_VERSION = 0x01;

// Build a deduplicated actor table from all changes
static auto build_actor_table(const detail::DocState& state) -> std::vector<ActorId> {
    auto seen = std::unordered_set<ActorId>{};
    auto table = std::vector<ActorId>{};

    // Local actor first
    if (seen.insert(state.actor).second) {
        table.push_back(state.actor);
    }

    for (const auto& change : state.change_history) {
        if (seen.insert(change.actor).second) {
            table.push_back(change.actor);
        }
        for (const auto& op : change.operations) {
            if (seen.insert(op.id.actor).second) {
                table.push_back(op.id.actor);
            }
        }
    }
    return table;
}

auto Document::save() const -> std::vector<std::byte> {
    auto s = storage::Serializer{};

    auto actor_table = build_actor_table(*state_);

    // Magic bytes + version
    s.write_raw_bytes(MAGIC, 4);
    s.write_u8(FORMAT_VERSION);

    // Actor table
    s.write_uleb128(actor_table.size());
    for (const auto& actor : actor_table) {
        s.write_actor_id(actor);
    }

    // Local actor index
    for (std::size_t i = 0; i < actor_table.size(); ++i) {
        if (actor_table[i] == state_->actor) {
            s.write_uleb128(i);
            break;
        }
    }

    // Local state
    s.write_uleb128(state_->next_counter);
    s.write_uleb128(state_->local_seq);

    // Changes
    s.write_uleb128(state_->change_history.size());
    for (const auto& change : state_->change_history) {
        // Actor index
        for (std::size_t i = 0; i < actor_table.size(); ++i) {
            if (actor_table[i] == change.actor) {
                s.write_uleb128(i);
                break;
            }
        }
        s.write_uleb128(change.seq);
        s.write_uleb128(change.start_op);
        s.write_sleb128(change.timestamp);

        // Message
        if (change.message) {
            s.write_u8(1);
            s.write_string(*change.message);
        } else {
            s.write_u8(0);
        }

        // Deps
        s.write_uleb128(change.deps.size());
        for (const auto& dep : change.deps) {
            s.write_change_hash(dep);
        }

        // Operations
        s.write_uleb128(change.operations.size());
        for (const auto& op : change.operations) {
            s.write_op_id(op.id, actor_table);
            s.write_obj_id(op.obj, actor_table);
            s.write_prop(op.key);
            s.write_u8(static_cast<std::uint8_t>(op.action));
            s.write_value(op.value);

            // Predecessors
            s.write_uleb128(op.pred.size());
            for (const auto& p : op.pred) {
                s.write_op_id(p, actor_table);
            }

            // Insert_after
            if (op.insert_after) {
                s.write_u8(1);
                s.write_op_id(*op.insert_after, actor_table);
            } else {
                s.write_u8(0);
            }
        }
    }

    // Heads
    s.write_uleb128(state_->heads.size());
    for (const auto& h : state_->heads) {
        s.write_change_hash(h);
    }

    // Clock
    s.write_uleb128(state_->clock.size());
    for (const auto& [actor, seq] : state_->clock) {
        for (std::size_t i = 0; i < actor_table.size(); ++i) {
            if (actor_table[i] == actor) {
                s.write_uleb128(i);
                break;
            }
        }
        s.write_uleb128(seq);
    }

    return s.take();
}

auto Document::load(std::span<const std::byte> data) -> std::optional<Document> {
    auto d = storage::Deserializer{data};

    // Verify magic bytes
    auto magic = d.read_bytes(4);
    if (!magic) return std::nullopt;
    for (int i = 0; i < 4; ++i) {
        if ((*magic)[i] != static_cast<std::byte>(MAGIC[i])) return std::nullopt;
    }

    // Version
    auto version = d.read_u8();
    if (!version || *version != FORMAT_VERSION) return std::nullopt;

    // Actor table
    auto num_actors = d.read_uleb128();
    if (!num_actors) return std::nullopt;
    auto actor_table = std::vector<ActorId>{};
    actor_table.reserve(static_cast<std::size_t>(*num_actors));
    for (std::uint64_t i = 0; i < *num_actors; ++i) {
        auto actor = d.read_actor_id();
        if (!actor) return std::nullopt;
        actor_table.push_back(*actor);
    }

    // Local actor index
    auto actor_idx = d.read_uleb128();
    if (!actor_idx || *actor_idx >= actor_table.size()) return std::nullopt;

    // Local state
    auto next_counter = d.read_uleb128();
    if (!next_counter) return std::nullopt;
    auto local_seq = d.read_uleb128();
    if (!local_seq) return std::nullopt;

    // Changes
    auto num_changes = d.read_uleb128();
    if (!num_changes) return std::nullopt;

    auto changes = std::vector<Change>{};
    changes.reserve(static_cast<std::size_t>(*num_changes));

    for (std::uint64_t ci = 0; ci < *num_changes; ++ci) {
        auto change_actor_idx = d.read_uleb128();
        if (!change_actor_idx || *change_actor_idx >= actor_table.size()) return std::nullopt;

        auto seq = d.read_uleb128();
        if (!seq) return std::nullopt;
        auto start_op = d.read_uleb128();
        if (!start_op) return std::nullopt;
        auto timestamp = d.read_sleb128();
        if (!timestamp) return std::nullopt;

        // Message
        auto has_msg = d.read_u8();
        if (!has_msg) return std::nullopt;
        auto message = std::optional<std::string>{};
        if (*has_msg == 1) {
            auto msg = d.read_string();
            if (!msg) return std::nullopt;
            message = std::move(*msg);
        }

        // Deps
        auto num_deps = d.read_uleb128();
        if (!num_deps) return std::nullopt;
        auto deps = std::vector<ChangeHash>{};
        deps.reserve(static_cast<std::size_t>(*num_deps));
        for (std::uint64_t di = 0; di < *num_deps; ++di) {
            auto dep = d.read_change_hash();
            if (!dep) return std::nullopt;
            deps.push_back(*dep);
        }

        // Operations
        auto num_ops = d.read_uleb128();
        if (!num_ops) return std::nullopt;
        auto operations = std::vector<Op>{};
        operations.reserve(static_cast<std::size_t>(*num_ops));

        for (std::uint64_t oi = 0; oi < *num_ops; ++oi) {
            auto id = d.read_op_id(actor_table);
            if (!id) return std::nullopt;
            auto obj = d.read_obj_id(actor_table);
            if (!obj) return std::nullopt;
            auto key = d.read_prop();
            if (!key) return std::nullopt;
            auto action = d.read_u8();
            if (!action) return std::nullopt;
            auto value = d.read_value();
            if (!value) return std::nullopt;

            // Predecessors
            auto num_pred = d.read_uleb128();
            if (!num_pred) return std::nullopt;
            auto pred = std::vector<OpId>{};
            pred.reserve(static_cast<std::size_t>(*num_pred));
            for (std::uint64_t pi = 0; pi < *num_pred; ++pi) {
                auto p = d.read_op_id(actor_table);
                if (!p) return std::nullopt;
                pred.push_back(*p);
            }

            // Insert_after
            auto has_ia = d.read_u8();
            if (!has_ia) return std::nullopt;
            auto insert_after = std::optional<OpId>{};
            if (*has_ia == 1) {
                auto ia = d.read_op_id(actor_table);
                if (!ia) return std::nullopt;
                insert_after = *ia;
            }

            operations.push_back(Op{
                .id = *id,
                .obj = *obj,
                .key = std::move(*key),
                .action = static_cast<OpType>(*action),
                .value = std::move(*value),
                .pred = std::move(pred),
                .insert_after = insert_after,
            });
        }

        changes.push_back(Change{
            .actor = actor_table[static_cast<std::size_t>(*change_actor_idx)],
            .seq = *seq,
            .start_op = *start_op,
            .timestamp = *timestamp,
            .message = std::move(message),
            .deps = std::move(deps),
            .operations = std::move(operations),
        });
    }

    // Heads
    auto num_heads = d.read_uleb128();
    if (!num_heads) return std::nullopt;
    auto heads = std::vector<ChangeHash>{};
    heads.reserve(static_cast<std::size_t>(*num_heads));
    for (std::uint64_t i = 0; i < *num_heads; ++i) {
        auto h = d.read_change_hash();
        if (!h) return std::nullopt;
        heads.push_back(*h);
    }

    // Clock
    auto num_clock = d.read_uleb128();
    if (!num_clock) return std::nullopt;
    auto clock = std::map<ActorId, std::uint64_t>{};
    for (std::uint64_t i = 0; i < *num_clock; ++i) {
        auto cidx = d.read_uleb128();
        if (!cidx || *cidx >= actor_table.size()) return std::nullopt;
        auto cseq = d.read_uleb128();
        if (!cseq) return std::nullopt;
        clock[actor_table[static_cast<std::size_t>(*cidx)]] = *cseq;
    }

    // Reconstruct document by replaying changes
    auto doc = Document{};
    doc.set_actor_id(actor_table[static_cast<std::size_t>(*actor_idx)]);
    doc.state_->next_counter = *next_counter;
    doc.state_->local_seq = *local_seq;

    // Replay all changes to rebuild the document state
    for (const auto& change : changes) {
        for (const auto& op : change.operations) {
            doc.state_->apply_op(op);
            doc.state_->op_log.push_back(op);
        }
    }

    // Restore metadata
    doc.state_->change_history = std::move(changes);
    doc.state_->heads = std::move(heads);
    doc.state_->clock = std::move(clock);

    return doc;
}

}  // namespace automerge_cpp
