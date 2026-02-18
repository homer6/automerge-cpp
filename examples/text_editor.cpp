// text_editor — concurrent text editing with merge and time travel
//
// Demonstrates: text objects, splice_text, cursors, patches, time travel,
//               transact with return values

#include <automerge-cpp/automerge.hpp>

#include <cstdint>
#include <cstdio>
#include <string>

namespace am = automerge_cpp;

int main() {
    const std::uint8_t actor1[16] = {1};
    auto doc = am::Document{};
    doc.set_actor_id(am::ActorId{actor1});

    // Create a text object — transact returns the ObjId directly
    auto text_id = doc.transact([](am::Transaction& tx) {
        auto id = tx.put_object(am::root, "content", am::ObjType::text);
        tx.splice_text(id, 0, 0, "Hello World");
        return id;
    });
    std::printf("Initial: \"%s\"\n", doc.text(text_id).c_str());

    // Save a snapshot for time travel
    auto v1_heads = doc.get_heads();

    // transact_with_patches to see what changed
    auto patches = doc.transact_with_patches([&](auto& tx) {
        tx.splice_text(text_id, 5, 6, " C++23");
    });

    std::printf("After edit: \"%s\"\n", doc.text(text_id).c_str());
    std::printf("Patches generated: %zu\n", patches.size());

    for (const auto& patch : patches) {
        if (auto* splice = std::get_if<am::PatchSpliceText>(&patch.action)) {
            std::printf("  SpliceText at %zu: deleted %zu, inserted \"%s\"\n",
                        splice->index, splice->delete_count, splice->text.c_str());
        }
    }

    // Cursor: create at the 'C' in "C++23"
    auto cursor = doc.cursor(text_id, 6);
    std::printf("\nCursor created at index 6 (character 'C')\n");

    // Insert text before the cursor
    doc.transact([&](auto& tx) {
        tx.splice_text(text_id, 0, 0, ">>> ");
    });

    std::printf("After prepend: \"%s\"\n", doc.text(text_id).c_str());

    // Cursor should have moved with the content
    if (cursor) {
        if (auto idx = doc.resolve_cursor(text_id, *cursor)) {
            std::printf("Cursor now at index %zu (still pointing to 'C')\n", *idx);
        }
    }

    // Time travel — read the original text
    std::printf("\nTime travel to v1: \"%s\"\n",
                doc.text_at(text_id, v1_heads).c_str());

    // Concurrent editing with merge
    auto doc2 = doc.fork();

    doc.transact([&](auto& tx) {
        tx.splice_text(text_id, doc.length(text_id), 0, " rocks!");
    });
    doc2.transact([&](auto& tx) {
        tx.splice_text(text_id, 0, 4, "");  // remove ">>> "
    });

    doc.merge(doc2);
    std::printf("\nAfter concurrent edits + merge: \"%s\"\n",
                doc.text(text_id).c_str());

    return 0;
}
