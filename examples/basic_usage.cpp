// basic_usage — demonstrates core automerge-cpp API
//
// Build: cmake --build build
// Run:   ./build/examples/basic_usage

#include <automerge-cpp/automerge.hpp>

#include <cstdint>
#include <cstdio>
#include <string>

namespace am = automerge_cpp;

int main() {
    // Create a document
    auto doc = am::Document{};

    // All mutations go through transact().
    // The lambda can return a value — here we capture the list ObjId.
    auto list_id = doc.transact([](am::Transaction& tx) {
        tx.put(am::root, "title", "Shopping List");
        tx.put(am::root, "created_by", "Alice");

        // Create a nested list and return its id
        auto items = tx.put_object(am::root, "items", am::ObjType::list);
        tx.insert(items, 0, "Milk");
        tx.insert(items, 1, "Eggs");
        tx.insert(items, 2, "Bread");
        return items;
    });

    // Typed get<T>() — no manual variant unwrapping
    if (auto title = doc.get<std::string>(am::root, "title")) {
        std::printf("Title: %s\n", title->c_str());
    }

    // operator[] for quick root access
    if (auto author = doc["created_by"]) {
        if (auto s = am::get_scalar<std::string>(*author)) {
            std::printf("Created by: %s\n", s->c_str());
        }
    }

    // Read list values
    std::printf("Items (%zu):\n", doc.length(list_id));
    for (auto& val : doc.values(list_id)) {
        if (auto s = am::get_scalar<std::string>(val)) {
            std::printf("  - %s\n", s->c_str());
        }
    }

    // Counters — put a Counter, then increment it
    doc.transact([](auto& tx) {
        tx.put(am::root, "views", am::Counter{0});
    });
    doc.transact([](auto& tx) {
        tx.increment(am::root, "views", 1);
        tx.increment(am::root, "views", 1);
        tx.increment(am::root, "views", 1);
    });

    if (auto views = doc.get<am::Counter>(am::root, "views")) {
        std::printf("Views: %lld\n", views->value);
    }

    // Save to binary and load back
    auto bytes = doc.save();
    std::printf("Saved document: %zu bytes\n", bytes.size());

    if (auto loaded = am::Document::load(bytes)) {
        if (auto title = loaded->get<std::string>(am::root, "title")) {
            std::printf("Loaded title: %s\n", title->c_str());
        }
    }

    // Path-based access — navigate nested objects in one call
    if (auto first_item = doc.get_path("items", std::size_t{0})) {
        if (auto s = am::get_scalar<std::string>(*first_item)) {
            std::printf("First item: %s\n", s->c_str());
        }
    }

    std::printf("Done.\n");
    return 0;
}
