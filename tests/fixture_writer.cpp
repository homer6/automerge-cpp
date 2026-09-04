#include <automerge-cpp/automerge.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace am = automerge_cpp;

auto actor(std::byte value) -> am::ActorId {
    auto bytes = std::array<std::byte, am::ActorId::size>{};
    bytes.fill(value);
    return am::ActorId{bytes};
}

auto document_with_actor(std::byte value) -> am::Document {
    auto document = am::Document{1};
    document.set_actor_id(actor(value));
    return document;
}

void require_merge(am::Document& target, const am::Document& source) {
    const auto result = target.merge(source);
    if (!result) {
        throw std::runtime_error{result.error().message};
    }
}

auto scalar_values() -> am::Document {
    auto document = document_with_actor(std::byte{0x81});
    document.transact([](am::Transaction& transaction) {
        transaction.put(am::root, "null", am::Null{});
        transaction.put(am::root, "bool", true);
        transaction.put(am::root, "int", std::int64_t{-42});
        transaction.put(am::root, "uint", std::uint64_t{42});
        transaction.put(am::root, "f64", 3.5);
        transaction.put(am::root, "str", "value");
        transaction.put(am::root,
                        "bytes",
                        am::Bytes{
                            std::byte{0x00},
                            std::byte{0x01},
                            std::byte{0xfe},
                            std::byte{0xff},
                        });
        transaction.put(am::root, "counter", am::Counter{7});
        transaction.put(am::root, "timestamp", am::Timestamp{1'700'000'000'123});
    });
    return document;
}

auto nested_objects() -> am::Document {
    auto document = document_with_actor(std::byte{0x82});
    document.transact([](am::Transaction& transaction) {
        const auto map = transaction.put_object(am::root, "profile", am::ObjType::map);
        transaction.put(map, "name", "Ada");
        const auto list = transaction.put_object(map, "tags", am::ObjType::list);
        transaction.insert(list, 0, "math");
        const auto entry = transaction.insert_object(list, 1, am::ObjType::map);
        transaction.put(entry, "language", "C++");
    });
    return document;
}

auto list_edits() -> am::Document {
    auto document = document_with_actor(std::byte{0x83});
    const auto list = document.transact([](am::Transaction& transaction) {
        const auto result = transaction.put_object(am::root, "items", am::ObjType::list);
        transaction.insert(result, 0, "a");
        transaction.insert(result, 1, "b");
        transaction.insert(result, 2, "c");
        return result;
    });
    document.transact([&](am::Transaction& transaction) {
        transaction.set(list, 1, "B");
        transaction.delete_index(list, 0);
        transaction.insert(list, 1, "between");
    });
    return document;
}

auto unicode_text() -> am::Document {
    auto document = document_with_actor(std::byte{0x84});
    const auto text = document.transact([](am::Transaction& transaction) {
        const auto result = transaction.put_object(am::root, "body", am::ObjType::text);
        static_cast<void>(transaction.splice_text(result, 0, 0, "Aé🙂漢字"));
        return result;
    });
    document.transact([&](am::Transaction& transaction) {
        static_cast<void>(transaction.splice_text(text, 1, 2, "🚀ß"));
    });
    return document;
}

auto marks() -> am::Document {
    auto document = document_with_actor(std::byte{0x85});
    const auto text = document.transact([](am::Transaction& transaction) {
        const auto result = transaction.put_object(am::root, "body", am::ObjType::text);
        static_cast<void>(transaction.splice_text(result, 0, 0, "marked text"));
        return result;
    });
    document.transact(
        [&](am::Transaction& transaction) { transaction.mark(text, 0, 6, "bold", true); });
    return document;
}

auto counter_increments() -> am::Document {
    auto document = document_with_actor(std::byte{0x86});
    document.transact(
        [](am::Transaction& transaction) { transaction.put(am::root, "count", am::Counter{10}); });
    document.transact(
        [](am::Transaction& transaction) { transaction.increment(am::root, "count", 5); });
    document.transact(
        [](am::Transaction& transaction) { transaction.increment(am::root, "count", -3); });
    return document;
}

auto deletes_tombstones() -> am::Document {
    auto document = document_with_actor(std::byte{0x87});
    const auto list = document.transact([](am::Transaction& transaction) {
        transaction.put(am::root, "gone", "value");
        const auto result = transaction.put_object(am::root, "items", am::ObjType::list);
        transaction.insert(result, 0, "first");
        transaction.insert(result, 1, "deleted");
        transaction.insert(result, 2, "last");
        return result;
    });
    document.transact([&](am::Transaction& transaction) {
        transaction.delete_key(am::root, "gone");
        transaction.delete_index(list, 1);
    });
    document.transact(
        [&](am::Transaction& transaction) { transaction.insert(list, 1, "replacement"); });
    return document;
}

auto concurrent_conflicts() -> am::Document {
    auto first = document_with_actor(std::byte{0x88});
    first.transact(
        [](am::Transaction& transaction) { transaction.put(am::root, "value", "base"); });
    auto second = first.fork();
    second.set_actor_id(actor(std::byte{0x89}));

    first.transact(
        [](am::Transaction& transaction) { transaction.put(am::root, "value", "left"); });
    second.transact(
        [](am::Transaction& transaction) { transaction.put(am::root, "value", "right"); });
    require_merge(first, second);
    return first;
}

auto commit_metadata() -> am::Document {
    auto document = document_with_actor(std::byte{0x77});
    document.transact(
        [](am::Transaction& transaction) { transaction.put(am::root, "metadata", true); },
        am::CommitOptions{
            .message = "import customer record",
            .time = std::int64_t{1'725'000'000'123},
        });
    return document;
}

auto long_history() -> am::Document {
    auto document = document_with_actor(std::byte{0x8b});
    for (auto index = 0; index < 1'000; ++index) {
        document.transact([&](am::Transaction& transaction) {
            auto key = std::string{"key-"};
            const auto digits = std::to_string(index);
            key.append(4 - digits.size(), '0');
            key += digits;
            transaction.put(am::root, key, std::int64_t{index});
        });
    }
    return document;
}

auto interleaved_histories() -> am::Document {
    auto document = document_with_actor(std::byte{0x8c});
    document.transact(
        [](am::Transaction& transaction) { transaction.put(am::root, "round-00", "first"); });

    for (auto round = 1; round <= 10; ++round) {
        document.set_actor_id(actor(std::byte{0x8c}));
        document.transact([&](am::Transaction& transaction) {
            const auto key = "round-" + (round < 10 ? std::string{"0"} : std::string{}) +
                             std::to_string(round) + "-first";
            transaction.put(am::root, key, std::int64_t{round});
        });
        document.set_actor_id(actor(std::byte{0x8d}));
        document.transact([&](am::Transaction& transaction) {
            const auto key = "round-" + (round < 10 ? std::string{"0"} : std::string{}) +
                             std::to_string(round) + "-second";
            transaction.put(am::root, key, std::int64_t{-round});
        });
    }
    return document;
}

auto empty_document() -> am::Document {
    return document_with_actor(std::byte{0x8e});
}

struct Fixture {
    std::string_view name;
    am::Document (*make_document)();
};

void write_bytes(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
    auto output = std::ofstream{path, std::ios::binary};
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    if (!output) {
        throw std::runtime_error{"failed to write reverse fixture"};
    }
}

void write_fixture(const std::filesystem::path& output_directory, const Fixture& fixture) {
    auto document = fixture.make_document();
    write_bytes(output_directory / (std::string{fixture.name} + ".automerge"), document.save());
    write_bytes(output_directory / (std::string{fixture.name} + ".document.automerge"),
                document.save_compact());
}

} // namespace

auto main(int argc, char** argv) -> int {
    if (argc != 2) {
        return 2;
    }
    const auto output_directory = std::filesystem::path{argv[1]};
    std::filesystem::create_directories(output_directory);
    const auto fixtures = std::array{
        Fixture{"scalar_values", scalar_values},
        Fixture{"nested_objects", nested_objects},
        Fixture{"list_edits", list_edits},
        Fixture{"unicode_text", unicode_text},
        Fixture{"marks", marks},
        Fixture{"counter_increments", counter_increments},
        Fixture{"deletes_tombstones", deletes_tombstones},
        Fixture{"concurrent_conflicts", concurrent_conflicts},
        Fixture{"commit_metadata", commit_metadata},
        Fixture{"long_history", long_history},
        Fixture{"interleaved_histories", interleaved_histories},
        Fixture{"empty_document", empty_document},
    };
    for (const auto& fixture : fixtures) {
        write_fixture(output_directory, fixture);
    }
    return 0;
}
