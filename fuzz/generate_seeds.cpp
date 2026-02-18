// Helper to generate valid seed corpus files for fuzz testing.
// Build and run once: ./generate_seeds
// Not a fuzz target itself — just a corpus generator.

#include <automerge-cpp/automerge.hpp>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>

static void write_seed(const std::string& path, const std::vector<std::byte>& data) {
    auto ofs = std::ofstream{path, std::ios::binary};
    ofs.write(reinterpret_cast<const char*>(data.data()),
              static_cast<std::streamsize>(data.size()));
}

int main() {
    namespace fs = std::filesystem;
    const auto dir = std::string{"fuzz/corpus"};
    fs::create_directories(dir);

    // Seed 1: empty document
    {
        auto doc = automerge_cpp::Document{};
        write_seed(dir + "/seed_empty.bin", doc.save());
    }

    // Seed 2: document with a single map key
    {
        auto doc = automerge_cpp::Document{};
        doc.transact([](auto& tx) {
            tx.put(automerge_cpp::root, "key", std::int64_t{42});
        });
        write_seed(dir + "/seed_single_key.bin", doc.save());
    }

    // Seed 3: document with text
    {
        auto doc = automerge_cpp::Document{};
        doc.transact([](auto& tx) {
            auto text_id = tx.put_object(automerge_cpp::root, "text",
                                         automerge_cpp::ObjType::text);
            tx.splice_text(text_id, 0, 0, "hello world");
        });
        write_seed(dir + "/seed_text.bin", doc.save());
    }

    // Seed 4: document with nested objects
    {
        auto doc = automerge_cpp::Document{};
        doc.transact([](auto& tx) {
            auto map_id = tx.put_object(automerge_cpp::root, "nested",
                                        automerge_cpp::ObjType::map);
            tx.put(map_id, "inner", std::string{"value"});
            auto list_id = tx.put_object(automerge_cpp::root, "items",
                                         automerge_cpp::ObjType::list);
            tx.insert(list_id, 0, std::int64_t{1});
            tx.insert(list_id, 1, std::int64_t{2});
        });
        write_seed(dir + "/seed_nested.bin", doc.save());
    }

    // Seed 5: document with multiple transactions
    {
        auto doc = automerge_cpp::Document{};
        doc.transact([](auto& tx) {
            tx.put(automerge_cpp::root, "a", std::int64_t{1});
        });
        doc.transact([](auto& tx) {
            tx.put(automerge_cpp::root, "b", std::int64_t{2});
        });
        doc.transact([](auto& tx) {
            tx.put(automerge_cpp::root, "c", std::int64_t{3});
        });
        write_seed(dir + "/seed_multi_tx.bin", doc.save());
    }

    return 0;
}
