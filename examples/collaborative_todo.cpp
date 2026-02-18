// collaborative_todo — two actors concurrently edit a shared todo list
//
// Demonstrates: fork, merge, conflict resolution, list operations

#include <automerge-cpp/automerge.hpp>

#include <cstdint>
#include <cstdio>
#include <string>

namespace am = automerge_cpp;

static void print_todos(const am::Document& doc, const am::ObjId& list_id,
                        const char* label) {
    std::printf("\n=== %s (%zu items) ===\n", label, doc.length(list_id));
    auto values = doc.values(list_id);
    for (std::size_t i = 0; i < values.size(); ++i) {
        auto& sv = std::get<am::ScalarValue>(values[i]);
        std::printf("  %zu. %s\n", i + 1, std::get<std::string>(sv).c_str());
    }
}

int main() {
    // Alice creates the initial document
    const std::uint8_t alice_id[16] = {1};
    auto alice_doc = am::Document{};
    alice_doc.set_actor_id(am::ActorId{alice_id});

    am::ObjId todo_list;
    alice_doc.transact([&](auto& tx) {
        tx.put(am::root, "title", std::string{"Team Tasks"});
        todo_list = tx.put_object(am::root, "todos", am::ObjType::list);
        tx.insert(todo_list, 0, std::string{"Set up CI pipeline"});
        tx.insert(todo_list, 1, std::string{"Write unit tests"});
    });

    print_todos(alice_doc, todo_list, "Alice (initial)");

    // Bob forks the document
    auto bob_doc = alice_doc.fork();

    // Alice adds items
    alice_doc.transact([&](auto& tx) {
        tx.insert(todo_list, 2, std::string{"Review PRs"});
    });

    // Bob also adds items (concurrently)
    bob_doc.transact([&](auto& tx) {
        tx.insert(todo_list, 2, std::string{"Update docs"});
    });

    print_todos(alice_doc, todo_list, "Alice (after her edit)");
    print_todos(bob_doc, todo_list, "Bob (after his edit)");

    // Merge Bob's changes into Alice's document
    alice_doc.merge(bob_doc);
    print_todos(alice_doc, todo_list, "Alice (after merge)");

    // Both items are preserved — no data loss
    std::printf("\nAll %zu todos preserved after merge.\n", alice_doc.length(todo_list));

    return 0;
}
