/// @file json.hpp
/// @brief nlohmann/json interoperability for automerge-cpp.
///
/// Provides ADL serialization (to_json/from_json), document export/import,
/// JSON Pointer (RFC 6901), JSON Patch (RFC 6902), JSON Merge Patch (RFC 7386),
/// and flatten/unflatten utilities.

#pragma once

#include <automerge-cpp/change.hpp>
#include <automerge-cpp/cursor.hpp>
#include <automerge-cpp/document.hpp>
#include <automerge-cpp/mark.hpp>
#include <automerge-cpp/patch.hpp>
#include <automerge-cpp/types.hpp>
#include <automerge-cpp/value.hpp>

#include <nlohmann/json.hpp>

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace automerge_cpp {

// =============================================================================
// ADL serialization: to_json / from_json  (Phase 12B)
// =============================================================================

// -- Scalar tag types ---------------------------------------------------------

void to_json(nlohmann::json& j, Null);
void to_json(nlohmann::json& j, const Counter& c);
void to_json(nlohmann::json& j, const Timestamp& t);

// -- ScalarValue (variant) ----------------------------------------------------

void to_json(nlohmann::json& j, const ScalarValue& sv);
void from_json(const nlohmann::json& j, ScalarValue& sv);

// -- Identity types (hex string) ----------------------------------------------

void to_json(nlohmann::json& j, const ActorId& id);
void from_json(const nlohmann::json& j, ActorId& id);

void to_json(nlohmann::json& j, const ChangeHash& h);
void from_json(const nlohmann::json& j, ChangeHash& h);

void to_json(nlohmann::json& j, const OpId& id);
void to_json(nlohmann::json& j, const ObjId& id);

// -- Compound types -----------------------------------------------------------

void to_json(nlohmann::json& j, const Change& c);
void to_json(nlohmann::json& j, const Patch& p);
void to_json(nlohmann::json& j, const Mark& m);
void to_json(nlohmann::json& j, const Cursor& c);

// =============================================================================
// Document export / import  (Phase 12C)
// =============================================================================

/// Export a document (or subtree) as a nlohmann::json value.
///
/// Maps/tables become JSON objects, lists become JSON arrays,
/// text objects become JSON strings. Scalars map naturally;
/// Counter and Timestamp become plain numbers (lossy — use ADL
/// serialization of ScalarValue for tagged round-trip fidelity).
///
/// @param doc The document to export.
/// @param obj The root of the subtree to export (default: document root).
auto export_json(const Document& doc, const ObjId& obj = root) -> nlohmann::json;

/// Export a document subtree as it was at a historical point.
auto export_json_at(const Document& doc,
                    const std::vector<ChangeHash>& heads,
                    const ObjId& obj = root) -> nlohmann::json;

/// Import a JSON value into a document at the given target object.
/// Wraps all mutations in a single transaction.
///
/// JSON objects become maps, arrays become lists, scalars map directly.
/// @param doc The document to populate.
/// @param j The JSON data.
/// @param target The object to import into (default: root).
void import_json(Document& doc, const nlohmann::json& j,
                 const ObjId& target = root);

/// Import a JSON value within an existing transaction.
void import_json(Transaction& tx, const nlohmann::json& j,
                 const ObjId& target = root);

// =============================================================================
// JSON Pointer (RFC 6901)  (Phase 12D)
// =============================================================================

/// Get a value at a JSON Pointer path.
/// @param doc The document to query.
/// @param pointer RFC 6901 pointer (e.g. "/config/port", "/items/0").
/// @return The value, or nullopt if the path doesn't exist.
auto get_pointer(const Document& doc, std::string_view pointer) -> std::optional<Value>;

/// Put a scalar value at a JSON Pointer path.
/// Creates intermediate map objects as needed.
/// @param doc The document to modify (wrapped in a transaction).
/// @param pointer RFC 6901 pointer path.
/// @param val The scalar value to set.
void put_pointer(Document& doc, std::string_view pointer, ScalarValue val);

/// Delete the value at a JSON Pointer path.
/// @param doc The document to modify (wrapped in a transaction).
/// @param pointer RFC 6901 pointer path.
void delete_pointer(Document& doc, std::string_view pointer);

// =============================================================================
// JSON Patch (RFC 6902)  (Phase 12E)
// =============================================================================

/// Apply an RFC 6902 JSON Patch to the document.
/// All operations run in a single transaction (atomic).
/// Supported ops: add, remove, replace, move, copy, test.
/// @param doc The document to patch.
/// @param patch A JSON array of patch operations.
/// @throws std::runtime_error on invalid patch or failed test op.
void apply_json_patch(Document& doc, const nlohmann::json& patch);

/// Generate an RFC 6902 JSON Patch representing the diff between two documents.
/// Uses nlohmann::json::diff() on the exported JSON.
auto diff_json_patch(const Document& before, const Document& after) -> nlohmann::json;

// =============================================================================
// JSON Merge Patch (RFC 7386)  (Phase 12F)
// =============================================================================

/// Apply an RFC 7386 JSON Merge Patch.
/// - Non-null values: set/replace
/// - null values: delete
/// - Missing keys: unchanged
void apply_merge_patch(Document& doc, const nlohmann::json& patch,
                       const ObjId& target = root);

/// Generate a merge patch showing differences between two documents.
auto generate_merge_patch(const Document& before,
                          const Document& after) -> nlohmann::json;

// =============================================================================
// Flatten / Unflatten  (Phase 12F)
// =============================================================================

/// Flatten a document subtree to a map of JSON Pointer paths to values.
/// Only leaf (scalar) values are included.
auto flatten(const Document& doc, const ObjId& obj = root)
    -> std::map<std::string, nlohmann::json>;

/// Unflatten a map of JSON Pointer paths into a document.
/// Creates intermediate map/list objects as needed.
void unflatten(Document& doc,
               const std::map<std::string, nlohmann::json>& flat,
               const ObjId& target = root);

}  // namespace automerge_cpp
