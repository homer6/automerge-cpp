#include <automerge-cpp/automerge.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace {

auto executable_path_storage() -> std::string& {
    static auto path = std::string{};
    return path;
}

} // namespace

namespace test_support {

auto actor_id_to_hex(const automerge_cpp::ActorId& actor) -> std::string {
    constexpr auto digits = std::string_view{"0123456789abcdef"};
    auto result = std::string{};
    result.reserve(automerge_cpp::ActorId::size * 2);
    for (const auto byte : actor.bytes) {
        const auto value = std::to_integer<unsigned int>(byte);
        result.push_back(digits[value >> 4]);
        result.push_back(digits[value & 0x0f]);
    }
    return result;
}

auto executable_path() -> const std::string& {
    return executable_path_storage();
}

} // namespace test_support

auto main(int argc, char** argv) -> int {
    if (argc == 2 && std::string_view{argv[1]} == "--print-actor-id") {
        const auto document = automerge_cpp::Document{};
        std::cout << test_support::actor_id_to_hex(document.actor_id());
        return 0;
    }

    executable_path_storage() = std::filesystem::absolute(argv[0]).string();
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
