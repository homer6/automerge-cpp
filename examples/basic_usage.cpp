// basic_usage — demonstrates core automerge-cpp API
//
// Shows multiple API styles: bare initializer lists, List{}/Map{} wrappers,
// STL containers (vector, set, map), typed get<T>(), operator[],
// get_path(), counters, and save/load.
//
// Build: cmake --build build
// Run:   ./build/examples/basic_usage

#include <automerge-cpp/automerge.hpp>

#include <cstdint>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace am = automerge_cpp;

int main() {
    auto doc = am::Document{};

    // -- Bare initializer list: creates a list automatically ------------------
    auto list_id = doc.transact([](am::Transaction& tx) {
        tx.put(am::root, "title", "Shopping List");
        tx.put(am::root, "created_by", "Alice");
        return tx.put(am::root, "items", {"Milk", "Eggs", "Bread"});
    });

    // -- Map{} wrapper: create a populated map --------------------------------
    doc.transact([](am::Transaction& tx) {
        tx.put(am::root, "config", am::Map{
            {"theme", "dark"},
            {"lang", "en"},
            {"max_items", 100},
        });
    });

    // -- Bare initializer list of pairs: creates a map automatically ----------
    doc.transact([](am::Transaction& tx) {
        tx.put(am::root, "author", {{"name", "Alice"}, {"email", "alice@example.com"}});
    });

    // -- List{} wrapper: explicit list with mixed types -----------------------
    doc.transact([](am::Transaction& tx) {
        tx.put(am::root, "mixed", am::List{1, "hello", 3.14, true});
    });

    // -- Typed get<T>() — no manual variant unwrapping ------------------------
    if (auto title = doc.get<std::string>(am::root, "title")) {
        std::printf("Title: %s\n", title->c_str());
    }

    // -- operator[] for quick root access -------------------------------------
    if (auto author = doc["created_by"]) {
        if (auto s = am::get_scalar<std::string>(*author)) {
            std::printf("Created by: %s\n", s->c_str());
        }
    }

    // -- Read list values -----------------------------------------------------
    std::printf("Items (%zu):\n", doc.length(list_id));
    for (auto& val : doc.values(list_id)) {
        if (auto s = am::get_scalar<std::string>(val)) {
            std::printf("  - %s\n", s->c_str());
        }
    }

    // -- get_path() for nested access -----------------------------------------
    if (auto theme = doc.get_path("config", "theme")) {
        if (auto s = am::get_scalar<std::string>(*theme)) {
            std::printf("Config theme: %s\n", s->c_str());
        }
    }
    if (auto email = doc.get_path("author", "email")) {
        if (auto s = am::get_scalar<std::string>(*email)) {
            std::printf("Author email: %s\n", s->c_str());
        }
    }
    if (auto first = doc.get_path("items", std::size_t{0})) {
        if (auto s = am::get_scalar<std::string>(*first)) {
            std::printf("First item: %s\n", s->c_str());
        }
    }

    // -- STL containers: vector → list, map → map -----------------------------
    doc.transact([](am::Transaction& tx) {
        auto tags = std::vector<std::string>{"crdt", "cpp", "collaborative"};
        tx.put(am::root, "tags", tags);
    });
    doc.transact([](am::Transaction& tx) {
        auto dims = std::map<std::string, am::ScalarValue>{
            {"w", am::ScalarValue{std::int64_t{800}}},
            {"h", am::ScalarValue{std::int64_t{600}}},
        };
        tx.put(am::root, "dims", dims);
    });
    if (auto w = doc.get_path("dims", "w")) {
        if (auto i = am::get_scalar<std::int64_t>(*w)) {
            std::printf("Dims width: %ld\n", *i);
        }
    }
    if (auto tag = doc.get_path("tags", std::size_t{0})) {
        if (auto s = am::get_scalar<std::string>(*tag)) {
            std::printf("First tag: %s\n", s->c_str());
        }
    }

    // -- Counters -------------------------------------------------------------
    doc.transact([](auto& tx) {
        tx.put(am::root, "views", am::Counter{0});
    });
    doc.transact([](auto& tx) {
        tx.increment(am::root, "views", 1);
        tx.increment(am::root, "views", 1);
        tx.increment(am::root, "views", 1);
    });
    if (auto views = doc.get<am::Counter>(am::root, "views")) {
        std::printf("Views: %ld\n", views->value);
    }

    // -- Save to binary and load back -----------------------------------------
    auto bytes = doc.save();
    std::printf("Saved document: %zu bytes\n", bytes.size());

    if (auto loaded = am::Document::load(bytes)) {
        if (auto title = loaded->get<std::string>(am::root, "title")) {
            std::printf("Loaded title: %s\n", title->c_str());
        }
    }

    std::printf("Done.\n");
    return 0;
}
