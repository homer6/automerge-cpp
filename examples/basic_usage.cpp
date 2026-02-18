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

    // All mutations go through transact()
    am::ObjId list_id;
    doc.transact([&](auto& tx) {
        tx.put(am::root, "title", std::string{"Shopping List"});
        tx.put(am::root, "created_by", std::string{"Alice"});

        // Create a nested list
        list_id = tx.put_object(am::root, "items", am::ObjType::list);
        tx.insert(list_id, 0, std::string{"Milk"});
        tx.insert(list_id, 1, std::string{"Eggs"});
        tx.insert(list_id, 2, std::string{"Bread"});
    });

    // Read values
    auto title = doc.get(am::root, "title");
    if (title) {
        auto& sv = std::get<am::ScalarValue>(*title);
        std::printf("Title: %s\n", std::get<std::string>(sv).c_str());
    }

    // Read list
    std::printf("Items (%zu):\n", doc.length(list_id));
    for (auto& val : doc.values(list_id)) {
        auto& sv = std::get<am::ScalarValue>(val);
        std::printf("  - %s\n", std::get<std::string>(sv).c_str());
    }

    // Counters
    doc.transact([](auto& tx) {
        tx.put(am::root, "views", am::Counter{0});
    });
    doc.transact([](auto& tx) {
        tx.increment(am::root, "views", 1);
        tx.increment(am::root, "views", 1);
        tx.increment(am::root, "views", 1);
    });

    auto views = doc.get(am::root, "views");
    if (views) {
        auto& sv = std::get<am::ScalarValue>(*views);
        std::printf("Views: %lld\n", std::get<am::Counter>(sv).value);
    }

    // Save to binary
    auto bytes = doc.save();
    std::printf("Saved document: %zu bytes\n", bytes.size());

    // Load from binary
    auto loaded = am::Document::load(bytes);
    if (loaded) {
        auto t = loaded->get(am::root, "title");
        auto& sv = std::get<am::ScalarValue>(*t);
        std::printf("Loaded title: %s\n", std::get<std::string>(sv).c_str());
    }

    std::printf("Done.\n");
    return 0;
}
