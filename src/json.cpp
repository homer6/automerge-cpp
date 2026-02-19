#include <automerge-cpp/json.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace automerge_cpp {

// =============================================================================
// Shared helpers (anonymous namespace, visible to both automerge_cpp and
// automerge_cpp::json through normal name lookup)
// =============================================================================

namespace {

auto bytes_to_hex(const std::byte* data, std::size_t len) -> std::string {
    static constexpr char hex_chars[] = "0123456789abcdef";
    auto result = std::string{};
    result.reserve(len * 2);
    for (std::size_t i = 0; i < len; ++i) {
        auto b = static_cast<unsigned char>(data[i]);
        result.push_back(hex_chars[b >> 4]);
        result.push_back(hex_chars[b & 0x0F]);
    }
    return result;
}

auto hex_char_to_nibble(char c) -> std::byte {
    if (c >= '0' && c <= '9') return std::byte(c - '0');
    if (c >= 'a' && c <= 'f') return std::byte(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return std::byte(c - 'A' + 10);
    throw std::runtime_error{"invalid hex character"};
}

void hex_to_bytes(std::string_view hex, std::byte* out, std::size_t expected_len) {
    if (hex.size() != expected_len * 2) {
        throw std::runtime_error{"hex string length mismatch"};
    }
    for (std::size_t i = 0; i < expected_len; ++i) {
        out[i] = (hex_char_to_nibble(hex[i * 2]) << 4) | hex_char_to_nibble(hex[i * 2 + 1]);
    }
}

// Base64 encode for Bytes
auto base64_encode(const std::vector<std::byte>& data) -> std::string {
    static constexpr char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    auto result = std::string{};
    auto n = data.size();
    result.reserve(((n + 2) / 3) * 4);
    for (std::size_t i = 0; i < n; i += 3) {
        auto b0 = static_cast<unsigned char>(data[i]);
        auto b1 = (i + 1 < n) ? static_cast<unsigned char>(data[i + 1]) : 0u;
        auto b2 = (i + 2 < n) ? static_cast<unsigned char>(data[i + 2]) : 0u;
        result.push_back(table[b0 >> 2]);
        result.push_back(table[((b0 & 0x03) << 4) | (b1 >> 4)]);
        result.push_back((i + 1 < n) ? table[((b1 & 0x0F) << 2) | (b2 >> 6)] : '=');
        result.push_back((i + 2 < n) ? table[b2 & 0x3F] : '=');
    }
    return result;
}

auto base64_decode(std::string_view encoded) -> std::vector<std::byte> {
    static const auto decode_table = []() {
        std::array<unsigned char, 256> t{};
        t.fill(0);
        constexpr std::string_view chars =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (unsigned char i = 0; i < chars.size(); ++i) {
            t[static_cast<unsigned char>(chars[i])] = i;
        }
        return t;
    }();
    auto result = std::vector<std::byte>{};
    result.reserve((encoded.size() / 4) * 3);
    for (std::size_t i = 0; i + 3 < encoded.size(); i += 4) {
        auto a = decode_table[static_cast<unsigned char>(encoded[i])];
        auto b = decode_table[static_cast<unsigned char>(encoded[i + 1])];
        auto c = decode_table[static_cast<unsigned char>(encoded[i + 2])];
        auto d = decode_table[static_cast<unsigned char>(encoded[i + 3])];
        result.push_back(std::byte((a << 2) | (b >> 4)));
        if (encoded[i + 2] != '=')
            result.push_back(std::byte(((b & 0x0F) << 4) | (c >> 2)));
        if (encoded[i + 3] != '=')
            result.push_back(std::byte(((c & 0x03) << 6) | d));
    }
    return result;
}

}  // anonymous namespace

// =============================================================================
// ADL serialization: to_json / from_json  (in namespace automerge_cpp)
// =============================================================================

void to_json(nlohmann::json& j, Null) {
    j = nullptr;
}

void to_json(nlohmann::json& j, const Counter& c) {
    j = nlohmann::json{{"__type", "counter"}, {"value", c.value}};
}

void to_json(nlohmann::json& j, const Timestamp& t) {
    j = nlohmann::json{{"__type", "timestamp"}, {"value", t.millis_since_epoch}};
}

void to_json(nlohmann::json& j, const ScalarValue& sv) {
    std::visit(overload{
        [&](Null) { j = nullptr; },
        [&](bool b) { j = b; },
        [&](std::int64_t i) { j = i; },
        [&](std::uint64_t u) { j = u; },
        [&](double d) { j = d; },
        [&](const Counter& c) { to_json(j, c); },
        [&](const Timestamp& t) { to_json(j, t); },
        [&](const std::string& s) { j = s; },
        [&](const Bytes& b) {
            j = nlohmann::json{{"__type", "bytes"}, {"value", base64_encode(b)}};
        },
    }, sv);
}

void from_json(const nlohmann::json& j, ScalarValue& sv) {
    // Check for tagged types first
    if (j.is_object() && j.contains("__type")) {
        auto type = j["__type"].get<std::string>();
        if (type == "counter") {
            sv = Counter{j["value"].get<std::int64_t>()};
            return;
        }
        if (type == "timestamp") {
            sv = Timestamp{j["value"].get<std::int64_t>()};
            return;
        }
        if (type == "bytes") {
            sv = base64_decode(j["value"].get<std::string>());
            return;
        }
    }
    // Infer from JSON type
    if (j.is_null()) {
        sv = Null{};
    } else if (j.is_boolean()) {
        sv = j.get<bool>();
    } else if (j.is_number_unsigned()) {
        auto val = j.get<std::uint64_t>();
        // If it fits in int64, prefer int64 for consistency
        if (val <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            sv = static_cast<std::int64_t>(val);
        } else {
            sv = val;
        }
    } else if (j.is_number_integer()) {
        sv = j.get<std::int64_t>();
    } else if (j.is_number_float()) {
        sv = j.get<double>();
    } else if (j.is_string()) {
        sv = j.get<std::string>();
    } else {
        throw std::runtime_error{"cannot convert JSON to ScalarValue"};
    }
}

// -- Identity types -----------------------------------------------------------

void to_json(nlohmann::json& j, const ActorId& id) {
    j = bytes_to_hex(id.bytes.data(), ActorId::size);
}

void from_json(const nlohmann::json& j, ActorId& id) {
    hex_to_bytes(j.get<std::string>(), id.bytes.data(), ActorId::size);
}

void to_json(nlohmann::json& j, const ChangeHash& h) {
    j = bytes_to_hex(h.bytes.data(), ChangeHash::size);
}

void from_json(const nlohmann::json& j, ChangeHash& h) {
    hex_to_bytes(j.get<std::string>(), h.bytes.data(), ChangeHash::size);
}

void to_json(nlohmann::json& j, const OpId& id) {
    j = nlohmann::json{{"counter", id.counter}, {"actor", id.actor}};
}

void to_json(nlohmann::json& j, const ObjId& id) {
    std::visit(overload{
        [&](Root) { j = "root"; },
        [&](const OpId& op) { to_json(j, op); },
    }, id.inner);
}

// -- Compound types -----------------------------------------------------------

void to_json(nlohmann::json& j, const Change& c) {
    j = nlohmann::json{
        {"actor", c.actor},
        {"seq", c.seq},
        {"start_op", c.start_op},
        {"timestamp", c.timestamp},
        {"deps", c.deps},
        {"ops", c.operations.size()},
    };
    if (c.message) {
        j["message"] = *c.message;
    }
}

namespace {

void patch_action_to_json(nlohmann::json& j, const PatchAction& action) {
    std::visit(overload{
        [&](const PatchPut& p) {
            j["type"] = "put";
            j["conflict"] = p.conflict;
            std::visit(overload{
                [&](const ScalarValue& sv) {
                    auto sv_json = nlohmann::json{};
                    to_json(sv_json, sv);
                    j["value"] = std::move(sv_json);
                },
                [&](ObjType ot) {
                    j["value"] = std::string{to_string_view(ot)};
                },
            }, p.value);
        },
        [&](const PatchInsert& p) {
            j["type"] = "insert";
            j["index"] = p.index;
            std::visit(overload{
                [&](const ScalarValue& sv) {
                    auto sv_json = nlohmann::json{};
                    to_json(sv_json, sv);
                    j["value"] = std::move(sv_json);
                },
                [&](ObjType ot) {
                    j["value"] = std::string{to_string_view(ot)};
                },
            }, p.value);
        },
        [&](const PatchDelete& p) {
            j["type"] = "delete";
            j["index"] = p.index;
            j["count"] = p.count;
        },
        [&](const PatchIncrement& p) {
            j["type"] = "increment";
            j["delta"] = p.delta;
        },
        [&](const PatchSpliceText& p) {
            j["type"] = "splice_text";
            j["index"] = p.index;
            j["delete_count"] = p.delete_count;
            j["text"] = p.text;
        },
    }, action);
}

}  // anonymous namespace

void to_json(nlohmann::json& j, const Patch& p) {
    j = nlohmann::json{
        {"obj", p.obj},
    };
    std::visit(overload{
        [&](const std::string& k) { j["key"] = k; },
        [&](std::size_t i) { j["key"] = i; },
    }, p.key);
    auto action_j = nlohmann::json{};
    patch_action_to_json(action_j, p.action);
    j["action"] = std::move(action_j);
}

void to_json(nlohmann::json& j, const Mark& m) {
    j = nlohmann::json{
        {"start", m.start},
        {"end", m.end},
        {"name", m.name},
    };
    auto val_j = nlohmann::json{};
    to_json(val_j, m.value);
    j["value"] = std::move(val_j);
}

void to_json(nlohmann::json& j, const Cursor& c) {
    to_json(j, c.position);
}

// =============================================================================
// namespace automerge_cpp::json — all non-ADL interop functionality
// =============================================================================

namespace json {

// =============================================================================
// Document export (Phase 12C)
// =============================================================================

namespace {

// Templated export helpers — work with both Document and Transaction
template<typename Reader>
auto export_object_impl(const Reader& reader, const ObjId& obj) -> nlohmann::json;

template<typename Reader>
auto export_value_impl(const Reader& reader, const Value& val,
                       const ObjId& parent, const Prop& prop) -> nlohmann::json {
    return std::visit(overload{
        [&](ObjType) -> nlohmann::json {
            // Get the child ObjId and recurse
            auto child = std::visit(overload{
                [&](const std::string& key) { return reader.get_obj_id(parent, key); },
                [&](std::size_t index) { return reader.get_obj_id(parent, index); },
            }, prop);
            if (child) return export_object_impl(reader, *child);
            return nlohmann::json{};
        },
        [](const ScalarValue& sv) -> nlohmann::json {
            return std::visit(overload{
                [](Null) -> nlohmann::json { return nullptr; },
                [](bool b) -> nlohmann::json { return b; },
                [](std::int64_t i) -> nlohmann::json { return i; },
                [](std::uint64_t u) -> nlohmann::json { return u; },
                [](double d) -> nlohmann::json { return d; },
                [](const Counter& c) -> nlohmann::json { return c.value; },
                [](const Timestamp& t) -> nlohmann::json { return t.millis_since_epoch; },
                [](const std::string& s) -> nlohmann::json { return s; },
                [](const Bytes& b) -> nlohmann::json { return base64_encode(b); },
            }, sv);
        },
    }, val);
}

template<typename Reader>
auto export_object_impl(const Reader& reader, const ObjId& obj) -> nlohmann::json {
    auto type = reader.object_type(obj);
    if (!type) return nlohmann::json{};

    if (*type == ObjType::text) {
        return nlohmann::json(reader.text(obj));
    }

    if (*type == ObjType::map || *type == ObjType::table) {
        auto result = nlohmann::json::object();
        for (const auto& key : reader.keys(obj)) {
            if (auto val = reader.get(obj, key)) {
                result[key] = export_value_impl(reader, *val, obj, Prop{key});
            }
        }
        return result;
    }

    // list
    auto result = nlohmann::json::array();
    auto len = reader.length(obj);
    for (std::size_t i = 0; i < len; ++i) {
        if (auto val = reader.get(obj, i)) {
            result.push_back(export_value_impl(reader, *val, obj, Prop{i}));
        }
    }
    return result;
}

// Historical export helpers
auto export_value_at(const Document& doc, const Value& val,
                     const ObjId& parent, const Prop& prop,
                     const std::vector<ChangeHash>& heads) -> nlohmann::json;
auto export_object_at(const Document& doc, const ObjId& obj,
                      const std::vector<ChangeHash>& heads) -> nlohmann::json;

auto export_value_at(const Document& doc, const Value& val,
                     const ObjId& parent, const Prop& prop,
                     const std::vector<ChangeHash>& heads) -> nlohmann::json {
    return std::visit(overload{
        [&](ObjType) -> nlohmann::json {
            auto child = std::visit(overload{
                [&](const std::string& key) { return doc.get_obj_id(parent, key); },
                [&](std::size_t index) { return doc.get_obj_id(parent, index); },
            }, prop);
            if (child) return export_object_at(doc, *child, heads);
            return nlohmann::json{};
        },
        [](const ScalarValue& sv) -> nlohmann::json {
            return std::visit(overload{
                [](Null) -> nlohmann::json { return nullptr; },
                [](bool b) -> nlohmann::json { return b; },
                [](std::int64_t i) -> nlohmann::json { return i; },
                [](std::uint64_t u) -> nlohmann::json { return u; },
                [](double d) -> nlohmann::json { return d; },
                [](const Counter& c) -> nlohmann::json { return c.value; },
                [](const Timestamp& t) -> nlohmann::json { return t.millis_since_epoch; },
                [](const std::string& s) -> nlohmann::json { return s; },
                [](const Bytes& b) -> nlohmann::json { return base64_encode(b); },
            }, sv);
        },
    }, val);
}

auto export_object_at(const Document& doc, const ObjId& obj,
                      const std::vector<ChangeHash>& heads) -> nlohmann::json {
    auto type = doc.object_type(obj);
    if (!type) return nlohmann::json{};

    if (*type == ObjType::text) {
        return nlohmann::json(doc.text_at(obj, heads));
    }

    if (*type == ObjType::map || *type == ObjType::table) {
        auto result = nlohmann::json::object();
        for (const auto& key : doc.keys_at(obj, heads)) {
            if (auto val = doc.get_at(obj, key, heads)) {
                result[key] = export_value_at(doc, *val, obj, Prop{key}, heads);
            }
        }
        return result;
    }

    auto result = nlohmann::json::array();
    auto len = doc.length_at(obj, heads);
    for (std::size_t i = 0; i < len; ++i) {
        if (auto val = doc.get_at(obj, i, heads)) {
            result.push_back(export_value_at(doc, *val, obj, Prop{i}, heads));
        }
    }
    return result;
}

}  // anonymous namespace

auto export_json(const Document& doc, const ObjId& obj) -> nlohmann::json {
    return export_object_impl(doc, obj);
}

auto export_json_at(const Document& doc,
                    const std::vector<ChangeHash>& heads,
                    const ObjId& obj) -> nlohmann::json {
    return export_object_at(doc, obj, heads);
}

// =============================================================================
// Document import (Phase 12C)
// =============================================================================

namespace {

void import_json_value_at_key(Transaction& tx, const ObjId& obj,
                              std::string_view key, const nlohmann::json& val);
void import_json_value_at_index(Transaction& tx, const ObjId& obj,
                                std::size_t index, const nlohmann::json& val);

void import_json_value_at_key(Transaction& tx, const ObjId& obj,
                              std::string_view key, const nlohmann::json& val) {
    if (val.is_object()) {
        auto child = tx.put_object(obj, key, ObjType::map);
        for (auto& [k, v] : val.items()) {
            import_json_value_at_key(tx, child, k, v);
        }
    } else if (val.is_array()) {
        auto child = tx.put_object(obj, key, ObjType::list);
        for (std::size_t i = 0; i < val.size(); ++i) {
            import_json_value_at_index(tx, child, i, val[i]);
        }
    } else if (val.is_string()) {
        tx.put(obj, key, val.get<std::string>());
    } else if (val.is_number_unsigned()) {
        auto u = val.get<std::uint64_t>();
        if (u <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            tx.put(obj, key, static_cast<std::int64_t>(u));
        } else {
            tx.put(obj, key, u);
        }
    } else if (val.is_number_integer()) {
        tx.put(obj, key, val.get<std::int64_t>());
    } else if (val.is_number_float()) {
        tx.put(obj, key, val.get<double>());
    } else if (val.is_boolean()) {
        tx.put(obj, key, val.get<bool>());
    } else if (val.is_null()) {
        tx.put(obj, key, Null{});
    }
}

void import_json_value_at_index(Transaction& tx, const ObjId& obj,
                                std::size_t index, const nlohmann::json& val) {
    if (val.is_object()) {
        auto child = tx.insert_object(obj, index, ObjType::map);
        for (auto& [k, v] : val.items()) {
            import_json_value_at_key(tx, child, k, v);
        }
    } else if (val.is_array()) {
        auto child = tx.insert_object(obj, index, ObjType::list);
        for (std::size_t i = 0; i < val.size(); ++i) {
            import_json_value_at_index(tx, child, i, val[i]);
        }
    } else if (val.is_string()) {
        tx.insert(obj, index, val.get<std::string>());
    } else if (val.is_number_unsigned()) {
        auto u = val.get<std::uint64_t>();
        if (u <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            tx.insert(obj, index, static_cast<std::int64_t>(u));
        } else {
            tx.insert(obj, index, u);
        }
    } else if (val.is_number_integer()) {
        tx.insert(obj, index, val.get<std::int64_t>());
    } else if (val.is_number_float()) {
        tx.insert(obj, index, val.get<double>());
    } else if (val.is_boolean()) {
        tx.insert(obj, index, val.get<bool>());
    } else if (val.is_null()) {
        tx.insert(obj, index, ScalarValue{Null{}});
    }
}

}  // anonymous namespace

void import_json(Document& doc, const nlohmann::json& j, const ObjId& target) {
    doc.transact([&](Transaction& tx) {
        import_json(tx, j, target);
    });
}

void import_json(Transaction& tx, const nlohmann::json& j, const ObjId& target) {
    if (j.is_object()) {
        for (auto& [key, val] : j.items()) {
            import_json_value_at_key(tx, target, key, val);
        }
    } else if (j.is_array()) {
        for (std::size_t i = 0; i < j.size(); ++i) {
            import_json_value_at_index(tx, target, i, j[i]);
        }
    }
    // If j is a scalar, there's nothing to import into a target object
}

// =============================================================================
// JSON Pointer (RFC 6901) — Phase 12D
// =============================================================================

namespace {

/// Parse an RFC 6901 JSON Pointer into segments.
/// Empty string "" = root (0 segments).
/// "/" = one empty-string segment.
/// "/a/b/0" = ["a", "b", "0"].
auto parse_pointer(std::string_view pointer) -> std::vector<std::string> {
    if (pointer.empty()) return {};
    if (pointer[0] != '/') {
        throw std::runtime_error{"JSON Pointer must start with '/' or be empty"};
    }
    auto segments = std::vector<std::string>{};
    auto pos = std::size_t{1};
    while (pos <= pointer.size()) {
        auto next = pointer.find('/', pos);
        auto segment = std::string{pointer.substr(pos, next - pos)};
        // Unescape: ~1 -> /, ~0 -> ~  (order matters: ~1 first)
        for (auto i = std::size_t{0}; i < segment.size(); ++i) {
            if (segment[i] == '~' && i + 1 < segment.size()) {
                if (segment[i + 1] == '1') {
                    segment.replace(i, 2, "/");
                } else if (segment[i + 1] == '0') {
                    segment.replace(i, 2, "~");
                }
            }
        }
        segments.push_back(std::move(segment));
        if (next == std::string_view::npos) break;
        pos = next + 1;
    }
    return segments;
}

/// Try to parse a segment as a list index.
auto try_parse_index(std::string_view segment) -> std::optional<std::size_t> {
    if (segment.empty()) return std::nullopt;
    // Leading zeros are not allowed per RFC 6901 (except "0" itself)
    if (segment.size() > 1 && segment[0] == '0') return std::nullopt;
    auto result = std::size_t{0};
    auto [ptr, ec] = std::from_chars(segment.data(), segment.data() + segment.size(), result);
    if (ec == std::errc{} && ptr == segment.data() + segment.size()) return result;
    return std::nullopt;
}

}  // anonymous namespace

auto get_pointer(const Document& doc, std::string_view pointer) -> std::optional<Value> {
    auto segments = parse_pointer(pointer);
    if (segments.empty()) {
        // Empty pointer = root. Return the root object type.
        return Value{ObjType::map};
    }

    auto current = root;
    for (std::size_t i = 0; i < segments.size(); ++i) {
        auto type = doc.object_type(current);
        if (!type) return std::nullopt;

        auto is_last = (i == segments.size() - 1);

        if (*type == ObjType::list || *type == ObjType::text) {
            auto idx = try_parse_index(segments[i]);
            if (!idx) return std::nullopt;
            if (is_last) {
                return doc.get(current, *idx);
            }
            auto child = doc.get_obj_id(current, *idx);
            if (!child) return std::nullopt;
            current = *child;
        } else {
            // map or table
            if (is_last) {
                return doc.get(current, segments[i]);
            }
            auto child = doc.get_obj_id(current, segments[i]);
            if (!child) return std::nullopt;
            current = *child;
        }
    }
    return std::nullopt;
}

void put_pointer(Document& doc, std::string_view pointer, ScalarValue val) {
    auto segments = parse_pointer(pointer);
    if (segments.empty()) {
        throw std::runtime_error{"cannot put at root pointer"};
    }

    doc.transact([&](Transaction& tx) {
        auto current = root;
        // Walk to the parent, creating intermediate maps as needed
        for (std::size_t i = 0; i + 1 < segments.size(); ++i) {
            auto type = tx.object_type(current);
            if (!type) {
                throw std::runtime_error{"path segment does not exist"};
            }
            if (*type == ObjType::list || *type == ObjType::text) {
                auto idx = try_parse_index(segments[i]);
                if (!idx) throw std::runtime_error{"invalid list index in pointer"};
                auto child = tx.get_obj_id(current, *idx);
                if (!child) throw std::runtime_error{"path segment does not exist"};
                current = *child;
            } else {
                auto child = tx.get_obj_id(current, segments[i]);
                if (!child) {
                    // Create intermediate map
                    current = tx.put_object(current, segments[i], ObjType::map);
                } else {
                    current = *child;
                }
            }
        }
        // Set the final segment
        auto& last = segments.back();
        auto type = tx.object_type(current);
        if (type && (*type == ObjType::list || *type == ObjType::text)) {
            if (last == "-") {
                tx.insert(current, tx.length(current), val);
            } else {
                auto idx = try_parse_index(last);
                if (!idx) throw std::runtime_error{"invalid list index in pointer"};
                if (*idx < tx.length(current)) {
                    tx.set(current, *idx, val);
                } else {
                    tx.insert(current, *idx, val);
                }
            }
        } else {
            tx.put(current, last, val);
        }
    });
}

void delete_pointer(Document& doc, std::string_view pointer) {
    auto segments = parse_pointer(pointer);
    if (segments.empty()) {
        throw std::runtime_error{"cannot delete root pointer"};
    }

    doc.transact([&](Transaction& tx) {
        auto current = root;
        for (std::size_t i = 0; i + 1 < segments.size(); ++i) {
            auto type = tx.object_type(current);
            if (!type) return;
            if (*type == ObjType::list || *type == ObjType::text) {
                auto idx = try_parse_index(segments[i]);
                if (!idx) return;
                auto child = tx.get_obj_id(current, *idx);
                if (!child) return;
                current = *child;
            } else {
                auto child = tx.get_obj_id(current, segments[i]);
                if (!child) return;
                current = *child;
            }
        }
        auto& last = segments.back();
        auto type = tx.object_type(current);
        if (type && (*type == ObjType::list || *type == ObjType::text)) {
            auto idx = try_parse_index(last);
            if (idx && *idx < tx.length(current)) {
                tx.delete_index(current, *idx);
            }
        } else {
            tx.delete_key(current, last);
        }
    });
}

// =============================================================================
// JSON Patch (RFC 6902) — Phase 12E
// =============================================================================

namespace {

// Internal helpers for JSON Patch that operate within a transaction.

auto get_value_at_pointer(const Transaction& tx, const std::vector<std::string>& segments)
    -> std::optional<Value> {
    if (segments.empty()) return Value{ObjType::map};
    auto current = root;
    for (std::size_t i = 0; i < segments.size(); ++i) {
        auto type = tx.object_type(current);
        if (!type) return std::nullopt;
        auto is_last = (i == segments.size() - 1);
        if (*type == ObjType::list || *type == ObjType::text) {
            auto idx = try_parse_index(segments[i]);
            if (!idx) return std::nullopt;
            if (is_last) return tx.get(current, *idx);
            auto child = tx.get_obj_id(current, *idx);
            if (!child) return std::nullopt;
            current = *child;
        } else {
            if (is_last) return tx.get(current, segments[i]);
            auto child = tx.get_obj_id(current, segments[i]);
            if (!child) return std::nullopt;
            current = *child;
        }
    }
    return std::nullopt;
}

void add_value_at_pointer(Transaction& tx,
                          const std::vector<std::string>& segments,
                          const nlohmann::json& value) {
    if (segments.empty()) {
        throw std::runtime_error{"cannot add at root"};
    }
    auto current = root;
    for (std::size_t i = 0; i + 1 < segments.size(); ++i) {
        auto type = tx.object_type(current);
        if (!type) throw std::runtime_error{"path does not exist"};
        if (*type == ObjType::list || *type == ObjType::text) {
            auto idx = try_parse_index(segments[i]);
            if (!idx) throw std::runtime_error{"invalid index"};
            auto child = tx.get_obj_id(current, *idx);
            if (!child) throw std::runtime_error{"path does not exist"};
            current = *child;
        } else {
            auto child = tx.get_obj_id(current, segments[i]);
            if (!child) throw std::runtime_error{"path does not exist"};
            current = *child;
        }
    }
    auto& last = segments.back();
    auto type = tx.object_type(current);
    if (type && (*type == ObjType::list || *type == ObjType::text)) {
        std::size_t idx;
        if (last == "-") {
            idx = tx.length(current);
        } else {
            auto parsed = try_parse_index(last);
            if (!parsed) throw std::runtime_error{"invalid index"};
            idx = *parsed;
        }
        // For lists, "add" means insert
        if (value.is_object()) {
            auto child = tx.insert_object(current, idx, ObjType::map);
            for (auto& [k, v] : value.items()) {
                import_json_value_at_key(tx, child, k, v);
            }
        } else if (value.is_array()) {
            auto child = tx.insert_object(current, idx, ObjType::list);
            for (std::size_t i = 0; i < value.size(); ++i) {
                import_json_value_at_index(tx, child, i, value[i]);
            }
        } else {
            auto sv = ScalarValue{};
            automerge_cpp::from_json(value, sv);
            tx.insert(current, idx, sv);
        }
    } else {
        // Map: "add" means put (create or replace)
        import_json_value_at_key(tx, current, last, value);
    }
}

void remove_at_pointer(Transaction& tx,
                       const std::vector<std::string>& segments) {
    if (segments.empty()) throw std::runtime_error{"cannot remove root"};
    auto current = root;
    for (std::size_t i = 0; i + 1 < segments.size(); ++i) {
        auto type = tx.object_type(current);
        if (!type) throw std::runtime_error{"path does not exist"};
        if (*type == ObjType::list || *type == ObjType::text) {
            auto idx = try_parse_index(segments[i]);
            if (!idx) throw std::runtime_error{"invalid index"};
            auto child = tx.get_obj_id(current, *idx);
            if (!child) throw std::runtime_error{"path does not exist"};
            current = *child;
        } else {
            auto child = tx.get_obj_id(current, segments[i]);
            if (!child) throw std::runtime_error{"path does not exist"};
            current = *child;
        }
    }
    auto& last = segments.back();
    auto type = tx.object_type(current);
    if (type && (*type == ObjType::list || *type == ObjType::text)) {
        auto idx = try_parse_index(last);
        if (!idx) throw std::runtime_error{"invalid index"};
        tx.delete_index(current, *idx);
    } else {
        tx.delete_key(current, last);
    }
}

}  // anonymous namespace

void apply_json_patch(Document& doc, const nlohmann::json& patch) {
    if (!patch.is_array()) {
        throw std::runtime_error{"JSON Patch must be an array"};
    }

    doc.transact([&](Transaction& tx) {
        // Helper: resolve an ObjId at a pointer path using tx for reads
        auto resolve_obj_at = [&](const std::vector<std::string>& segs) -> std::optional<ObjId> {
            if (segs.empty()) return root;
            auto current = root;
            for (std::size_t i = 0; i + 1 < segs.size(); ++i) {
                auto type = tx.object_type(current);
                if (*type == ObjType::list || *type == ObjType::text) {
                    auto idx = try_parse_index(segs[i]);
                    current = *tx.get_obj_id(current, *idx);
                } else {
                    current = *tx.get_obj_id(current, segs[i]);
                }
            }
            auto& last = segs.back();
            auto type = tx.object_type(current);
            if (type && (*type == ObjType::list || *type == ObjType::text)) {
                auto idx = try_parse_index(last);
                return tx.get_obj_id(current, *idx);
            }
            return tx.get_obj_id(current, last);
        };

        // Helper: convert a Value to JSON for move/copy/test
        auto value_to_json = [&](const Value& val, const std::vector<std::string>& segs) -> nlohmann::json {
            return std::visit(overload{
                [&](ObjType) -> nlohmann::json {
                    auto child = resolve_obj_at(segs);
                    if (child) return export_object_impl(tx, *child);
                    return nlohmann::json{};
                },
                [&](const ScalarValue& sv) -> nlohmann::json {
                    return std::visit(overload{
                        [](Null) -> nlohmann::json { return nullptr; },
                        [](bool b) -> nlohmann::json { return b; },
                        [](std::int64_t i) -> nlohmann::json { return i; },
                        [](std::uint64_t u) -> nlohmann::json { return u; },
                        [](double d) -> nlohmann::json { return d; },
                        [](const Counter& c) -> nlohmann::json { return c.value; },
                        [](const Timestamp& t) -> nlohmann::json { return t.millis_since_epoch; },
                        [](const std::string& s) -> nlohmann::json { return s; },
                        [](const Bytes& b) -> nlohmann::json { return base64_encode(b); },
                    }, sv);
                },
            }, val);
        };

        for (const auto& op : patch) {
            auto op_str = op.at("op").get<std::string>();
            auto path_segments = parse_pointer(op.at("path").get<std::string>());

            if (op_str == "add") {
                add_value_at_pointer(tx, path_segments, op.at("value"));
            } else if (op_str == "remove") {
                remove_at_pointer(tx, path_segments);
            } else if (op_str == "replace") {
                remove_at_pointer(tx, path_segments);
                add_value_at_pointer(tx, path_segments, op.at("value"));
            } else if (op_str == "move") {
                auto from_segments = parse_pointer(op.at("from").get<std::string>());
                auto val = get_value_at_pointer(tx, from_segments);
                if (!val) throw std::runtime_error{"move: from path does not exist"};
                auto val_json = value_to_json(*val, from_segments);
                remove_at_pointer(tx, from_segments);
                add_value_at_pointer(tx, path_segments, val_json);
            } else if (op_str == "copy") {
                auto from_segments = parse_pointer(op.at("from").get<std::string>());
                auto val = get_value_at_pointer(tx, from_segments);
                if (!val) throw std::runtime_error{"copy: from path does not exist"};
                auto val_json = value_to_json(*val, from_segments);
                add_value_at_pointer(tx, path_segments, val_json);
            } else if (op_str == "test") {
                auto val = get_value_at_pointer(tx, path_segments);
                if (!val) throw std::runtime_error{"test: path does not exist"};
                auto current_json = value_to_json(*val, path_segments);
                if (current_json != op.at("value")) {
                    throw std::runtime_error{"test: value mismatch"};
                }
            } else {
                throw std::runtime_error{"unknown JSON Patch operation: " + op_str};
            }
        }
    });
}

auto diff_json_patch(const Document& before, const Document& after) -> nlohmann::json {
    auto j1 = export_json(before);
    auto j2 = export_json(after);
    return nlohmann::json::diff(j1, j2);
}

// =============================================================================
// JSON Merge Patch (RFC 7386) — Phase 12F
// =============================================================================

namespace {

void apply_merge_patch_recursive(Transaction& tx,
                                 const ObjId& target, const nlohmann::json& patch) {
    if (!patch.is_object()) return;

    for (auto& [key, value] : patch.items()) {
        if (value.is_null()) {
            tx.delete_key(target, key);
        } else if (value.is_object()) {
            // Check if the target already has an object at this key
            auto existing = tx.get_obj_id(target, key);
            if (existing) {
                auto type = tx.object_type(*existing);
                if (type && (*type == ObjType::map || *type == ObjType::table)) {
                    apply_merge_patch_recursive(tx, *existing, value);
                    continue;
                }
            }
            // Create new map and import
            auto child = tx.put_object(target, key, ObjType::map);
            for (auto& [k, v] : value.items()) {
                import_json_value_at_key(tx, child, k, v);
            }
        } else {
            // Scalar or array — just import
            import_json_value_at_key(tx, target, key, value);
        }
    }
}

}  // anonymous namespace

void apply_merge_patch(Document& doc, const nlohmann::json& patch, const ObjId& target) {
    doc.transact([&](Transaction& tx) {
        apply_merge_patch_recursive(tx, target, patch);
    });
}

auto generate_merge_patch(const Document& before, const Document& after) -> nlohmann::json {
    auto j1 = export_json(before);
    auto j2 = export_json(after);
    // Build a merge patch by comparing the two JSON trees.
    auto build_patch = [](const nlohmann::json& source, const nlohmann::json& target,
                          auto&& self) -> nlohmann::json {
        if (target.is_object()) {
            auto patch = nlohmann::json::object();
            if (source.is_object()) {
                // Keys removed in target
                for (auto it = source.begin(); it != source.end(); ++it) {
                    if (!target.contains(it.key())) {
                        patch[it.key()] = nullptr;
                    }
                }
                // Keys added or changed in target
                for (auto it = target.begin(); it != target.end(); ++it) {
                    if (!source.contains(it.key())) {
                        patch[it.key()] = it.value();
                    } else if (source[it.key()] != it.value()) {
                        patch[it.key()] = self(source[it.key()], it.value(), self);
                    }
                }
            } else {
                // Source was not object, target is — return entire target
                return target;
            }
            return patch;
        }
        return target;
    };
    return build_patch(j1, j2, build_patch);
}

// =============================================================================
// Flatten / Unflatten — Phase 12F
// =============================================================================

namespace {

/// Escape a segment for RFC 6901: ~ -> ~0, / -> ~1
auto escape_pointer_segment(const std::string& segment) -> std::string {
    auto result = std::string{};
    result.reserve(segment.size());
    for (char c : segment) {
        if (c == '~') { result += "~0"; }
        else if (c == '/') { result += "~1"; }
        else { result += c; }
    }
    return result;
}

void flatten_recursive(const Document& doc, const ObjId& obj,
                       const std::string& prefix,
                       std::map<std::string, nlohmann::json>& result) {
    auto type = doc.object_type(obj);
    if (!type) return;

    if (*type == ObjType::text) {
        result[prefix.empty() ? "/" : prefix] = doc.text(obj);
        return;
    }

    if (*type == ObjType::map || *type == ObjType::table) {
        for (const auto& key : doc.keys(obj)) {
            auto path = prefix + "/" + escape_pointer_segment(key);
            if (auto val = doc.get(obj, key)) {
                std::visit(overload{
                    [&](ObjType) {
                        auto child = doc.get_obj_id(obj, key);
                        if (child) flatten_recursive(doc, *child, path, result);
                    },
                    [&](const ScalarValue& sv) {
                        result[path] = std::visit(overload{
                            [](Null) -> nlohmann::json { return nullptr; },
                            [](bool b) -> nlohmann::json { return b; },
                            [](std::int64_t i) -> nlohmann::json { return i; },
                            [](std::uint64_t u) -> nlohmann::json { return u; },
                            [](double d) -> nlohmann::json { return d; },
                            [](const Counter& c) -> nlohmann::json { return c.value; },
                            [](const Timestamp& t) -> nlohmann::json { return t.millis_since_epoch; },
                            [](const std::string& s) -> nlohmann::json { return s; },
                            [](const Bytes& b) -> nlohmann::json { return base64_encode(b); },
                        }, sv);
                    },
                }, *val);
            }
        }
    } else {
        // list
        auto len = doc.length(obj);
        for (std::size_t i = 0; i < len; ++i) {
            auto path = prefix + "/" + std::to_string(i);
            if (auto val = doc.get(obj, i)) {
                std::visit(overload{
                    [&](ObjType) {
                        auto child = doc.get_obj_id(obj, i);
                        if (child) flatten_recursive(doc, *child, path, result);
                    },
                    [&](const ScalarValue& sv) {
                        result[path] = std::visit(overload{
                            [](Null) -> nlohmann::json { return nullptr; },
                            [](bool b) -> nlohmann::json { return b; },
                            [](std::int64_t i) -> nlohmann::json { return i; },
                            [](std::uint64_t u) -> nlohmann::json { return u; },
                            [](double d) -> nlohmann::json { return d; },
                            [](const Counter& c) -> nlohmann::json { return c.value; },
                            [](const Timestamp& t) -> nlohmann::json { return t.millis_since_epoch; },
                            [](const std::string& s) -> nlohmann::json { return s; },
                            [](const Bytes& b) -> nlohmann::json { return base64_encode(b); },
                        }, sv);
                    },
                }, *val);
            }
        }
    }
}

}  // anonymous namespace

auto flatten(const Document& doc, const ObjId& obj)
    -> std::map<std::string, nlohmann::json> {
    auto result = std::map<std::string, nlohmann::json>{};
    flatten_recursive(doc, obj, "", result);
    return result;
}

void unflatten(Document& doc,
               const std::map<std::string, nlohmann::json>& flat,
               const ObjId& target) {
    doc.transact([&](Transaction& tx) {
        for (const auto& [path, value] : flat) {
            auto segments = parse_pointer(path);
            if (segments.empty()) continue;

            auto current = target;
            // Walk/create intermediates
            for (std::size_t i = 0; i + 1 < segments.size(); ++i) {
                auto type = tx.object_type(current);
                if (!type) break;
                if (*type == ObjType::list || *type == ObjType::text) {
                    auto idx = try_parse_index(segments[i]);
                    if (!idx) break;
                    auto child = tx.get_obj_id(current, *idx);
                    if (!child) break;
                    current = *child;
                } else {
                    auto child = tx.get_obj_id(current, segments[i]);
                    if (!child) {
                        current = tx.put_object(current, segments[i], ObjType::map);
                    } else {
                        current = *child;
                    }
                }
            }
            // Set the leaf value
            auto& last = segments.back();
            import_json_value_at_key(tx, current, last, value);
        }
    });
}

}  // namespace json

}  // namespace automerge_cpp
