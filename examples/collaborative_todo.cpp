// collaborative_todo — two actors concurrently edit a shared todo list
//
// Demonstrates: fork, merge, conflict resolution, list operations,
//               transact with return values, typed get<T>()

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
        if (auto s = am::get_scalar<std::string>(values[i])) {
            std::printf("  %zu. %s\n", i + 1, s->c_str());
        }
    }
}

int main() {
    // Alice creates the initial document
    const std::uint8_t alice_id[16] = {1};
    auto alice_doc = am::Document{};
    alice_doc.set_actor_id(am::ActorId{alice_id});

    // transact returns the list ObjId directly — no external variable needed
    auto todo_list = alice_doc.transact([](am::Transaction& tx) {
        tx.put(am::root, "title", "Team Tasks");
        auto list = tx.put_object(am::root, "todos", am::ObjType::list);
        tx.insert(list, 0, "Set up CI pipeline");
        tx.insert(list, 1, "Write unit tests");
        return list;
    });

    print_todos(alice_doc, todo_list, "Alice (initial)");

    // Bob forks the document
    auto bob_doc = alice_doc.fork();

    // Alice and Bob add items concurrently
    alice_doc.transact([&](auto& tx) {
        tx.insert(todo_list, 2, "Review PRs");
    });
    bob_doc.transact([&](auto& tx) {
        tx.insert(todo_list, 2, "Update docs");
    });

    print_todos(alice_doc, todo_list, "Alice (after her edit)");
    print_todos(bob_doc, todo_list, "Bob (after his edit)");

    // Merge — both items are preserved, no data loss
    alice_doc.merge(bob_doc);
    print_todos(alice_doc, todo_list, "Alice (after merge)");

    std::printf("\nAll %zu todos preserved after merge.\n",
                alice_doc.length(todo_list));
    return 0;
}
