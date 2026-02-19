// json_interop_demo — automerge-cpp + nlohmann/json interoperability
//
// Demonstrates all library-level JSON interop features:
//   1. import_json / export_json (recursive, nested objects + arrays)
//   2. JSON Pointer (RFC 6901) — get_pointer, put_pointer, delete_pointer
//   3. JSON Patch (RFC 6902) — apply_json_patch, diff_json_patch
//   4. JSON Merge Patch (RFC 7386) — apply_merge_patch
//   5. ADL serialization — to_json/from_json for automerge types
//   6. Flatten / unflatten
//
// Build: cmake --build build -DAUTOMERGE_CPP_BUILD_EXAMPLES=ON
// Run:   ./build/examples/json_interop_demo

#include <automerge-cpp/automerge.hpp>
#include <automerge-cpp/json.hpp>
#include <nlohmann/json.hpp>

#include <cstdio>
#include <iostream>
#include <string>

namespace am = automerge_cpp;
using json = nlohmann::json;

int main() {
    // =========================================================================
    // 1. Import JSON into an Automerge document
    // =========================================================================
    std::printf("=== 1. Import JSON → Automerge ===\n");

    auto input = json::parse(R"({
        "name": "automerge-cpp",
        "version": "0.5.0",
        "stars": 42,
        "active": true,
        "tags": ["crdt", "collaborative", "c++23"],
        "config": {
            "port": 8080,
            "host": "localhost",
            "debug": false
        }
    })");

    std::printf("Input JSON:\n%s\n\n", input.dump(2).c_str());

    auto doc = am::Document{};
    am::json::import_json(doc, input);

    // Verify with typed get<T>()
    std::printf("Imported into Automerge:\n");
    std::printf("  name:    %s\n", doc.get<std::string>(am::root, "name")->c_str());
    std::printf("  version: %s\n", doc.get<std::string>(am::root, "version")->c_str());
    std::printf("  stars:   %ld\n", *doc.get<std::int64_t>(am::root, "stars"));
    std::printf("  active:  %s\n", *doc.get<bool>(am::root, "active") ? "true" : "false");

    // =========================================================================
    // 2. Export Automerge → JSON (recursive, handles nested objects!)
    // =========================================================================
    std::printf("\n=== 2. Export Automerge → JSON ===\n");

    auto exported = am::json::export_json(doc);
    std::printf("Full recursive export:\n%s\n", exported.dump(2).c_str());

    // Round-trip check
    if (exported == input) {
        std::printf("  Round-trip: PASS (export == input)\n");
    }

    // =========================================================================
    // 3. JSON Pointer (RFC 6901)
    // =========================================================================
    std::printf("\n=== 3. JSON Pointer (RFC 6901) ===\n");

    if (auto val = am::json::get_pointer(doc, "/config/port")) {
        std::printf("  /config/port = %ld\n", *am::get_scalar<std::int64_t>(*val));
    }
    if (auto val = am::json::get_pointer(doc, "/tags/0")) {
        std::printf("  /tags/0 = %s\n", am::get_scalar<std::string>(*val)->c_str());
    }

    am::json::put_pointer(doc, "/config/timeout", am::ScalarValue{std::int64_t{30}});
    std::printf("  Added /config/timeout = 30\n");

    am::json::delete_pointer(doc, "/config/debug");
    std::printf("  Deleted /config/debug\n");

    std::printf("  Config after changes:\n%s\n",
        am::json::export_json(doc, *doc.get_obj_id(am::root, "config")).dump(4).c_str());

    // =========================================================================
    // 4. Fork, merge, diff as JSON Patch
    // =========================================================================
    std::printf("\n=== 4. Fork/merge + diff ===\n");

    auto bob = doc.fork();
    bob.transact([](am::Transaction& tx) {
        tx.put(am::root, "stars", std::int64_t{100});
        tx.put(am::root, "active", false);
    });

    auto diff = am::json::diff_json_patch(doc, bob);
    std::printf("  Diff (RFC 6902):\n%s\n", diff.dump(2).c_str());

    doc.merge(bob);
    std::printf("  After merge: stars=%ld, active=%s\n",
        *doc.get<std::int64_t>(am::root, "stars"),
        *doc.get<bool>(am::root, "active") ? "true" : "false");

    // =========================================================================
    // 5. JSON Patch (RFC 6902)
    // =========================================================================
    std::printf("\n=== 5. JSON Patch (RFC 6902) ===\n");

    am::json::apply_json_patch(doc, json::parse(R"([
        {"op": "add", "path": "/tags/-", "value": "json"},
        {"op": "replace", "path": "/version", "value": "0.6.0"},
        {"op": "test", "path": "/name", "value": "automerge-cpp"}
    ])"));

    std::printf("  After patch: version=%s\n",
        doc.get<std::string>(am::root, "version")->c_str());
    auto tags = doc.get_obj_id(am::root, "tags");
    std::printf("  Tags count: %zu\n", doc.length(*tags));

    // =========================================================================
    // 6. JSON Merge Patch (RFC 7386)
    // =========================================================================
    std::printf("\n=== 6. JSON Merge Patch (RFC 7386) ===\n");

    am::json::apply_merge_patch(doc, json{
        {"stars", 200},
        {"deprecated", nullptr},
        {"config", {{"port", 9090}}},
    });

    auto after_merge_patch = am::json::export_json(doc);
    std::printf("  After merge patch:\n%s\n", after_merge_patch.dump(2).c_str());

    // =========================================================================
    // 7. Flatten / Unflatten
    // =========================================================================
    std::printf("\n=== 7. Flatten ===\n");

    auto flat = am::json::flatten(doc);
    for (const auto& [path, value] : flat) {
        std::printf("  %s = %s\n", path.c_str(), value.dump().c_str());
    }

    // =========================================================================
    // 8. ADL serialization — automerge types ↔ nlohmann::json
    // =========================================================================
    std::printf("\n=== 8. ADL Serialization ===\n");

    auto changes = doc.get_changes();
    if (!changes.empty()) {
        json change_json = changes.back();
        std::printf("  Last change:\n%s\n", change_json.dump(2).c_str());
    }

    auto sv = am::ScalarValue{am::Counter{42}};
    json sv_json = sv;
    std::printf("  Counter as JSON: %s\n", sv_json.dump().c_str());

    // =========================================================================
    // 9. Save/load + JSON verification
    // =========================================================================
    std::printf("\n=== 9. Save, load, verify ===\n");

    auto bytes = doc.save();
    std::printf("  Saved: %zu bytes\n", bytes.size());

    if (auto loaded = am::Document::load(bytes)) {
        auto restored = am::json::export_json(*loaded);
        if (restored == am::json::export_json(doc)) {
            std::printf("  Save/load round-trip: PASS\n");
        }
    }

    std::printf("\nDone.\n");
    return 0;
}
