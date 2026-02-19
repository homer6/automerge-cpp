// collaborative_todo — two actors concurrently edit a shared todo list
//
// Demonstrates: bare initializer lists, Map{} wrapper, std::vector,
//               fork, merge, conflict resolution, transact with return values,
//               get_path()

#include <automerge-cpp/automerge.hpp>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

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

    // Bare initializer list — creates the todos list in one call
    auto todo_list = alice_doc.transact([](am::Transaction& tx) {
        tx.put(am::root, "title", "Team Tasks");
        return tx.put(am::root, "todos", {"Set up CI pipeline", "Write unit tests"});
    });

    // Map{} wrapper — create a metadata map with explicit wrapper
    alice_doc.transact([](am::Transaction& tx) {
        tx.put(am::root, "meta", am::Map{{"owner", "Alice"}, {"priority", "high"}});
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

    // Merge — both items preserved, no data loss
    alice_doc.merge(bob_doc);
    print_todos(alice_doc, todo_list, "Alice (after merge)");

    // std::vector — add labels from a container
    alice_doc.transact([](am::Transaction& tx) {
        auto labels = std::vector<std::string>{"urgent", "sprint-3"};
        tx.put(am::root, "labels", labels);
    });

    // get_path() into nested map
    if (auto owner = alice_doc.get_path("meta", "owner")) {
        if (auto s = am::get_scalar<std::string>(*owner)) {
            std::printf("\nMeta owner: %s\n", s->c_str());
        }
    }
    if (auto label = alice_doc.get_path("labels", std::size_t{0})) {
        if (auto s = am::get_scalar<std::string>(*label)) {
            std::printf("First label: %s\n", s->c_str());
        }
    }

    std::printf("All %zu todos preserved after merge.\n",
                alice_doc.length(todo_list));
    return 0;
}
