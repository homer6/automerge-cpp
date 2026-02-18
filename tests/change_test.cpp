#include <automerge-cpp/change.hpp>

#include <gtest/gtest.h>

using namespace automerge_cpp;

TEST(Change, default_constructed) {
    const auto c = Change{};

    EXPECT_TRUE(c.actor.is_zero());
    EXPECT_EQ(c.seq, 0u);
    EXPECT_EQ(c.start_op, 0u);
    EXPECT_EQ(c.timestamp, 0);
    EXPECT_FALSE(c.message.has_value());
    EXPECT_TRUE(c.deps.empty());
    EXPECT_TRUE(c.operations.empty());
}

TEST(Change, construction_with_fields) {
    const std::uint8_t actor_raw[16] = {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    const auto actor = ActorId{actor_raw};

    auto change = Change{
        .actor = actor,
        .seq = 1,
        .start_op = 1,
        .timestamp = 1708000000000,
        .message = "initial",
        .deps = {},
        .operations = {
            Op{
                .id = OpId{1, actor},
                .obj = root,
                .key = map_key("title"),
                .action = OpType::put,
                .value = Value{ScalarValue{std::string{"Hello"}}},
                .pred = {},
            },
        },
    };

    EXPECT_EQ(change.actor, actor);
    EXPECT_EQ(change.seq, 1u);
    EXPECT_EQ(change.message.value(), "initial");
    EXPECT_EQ(change.operations.size(), 1u);
    EXPECT_EQ(change.operations[0].action, OpType::put);
}

TEST(Change, equality) {
    const auto a = Change{.seq = 1, .start_op = 1};
    const auto b = Change{.seq = 1, .start_op = 1};
    const auto c = Change{.seq = 2, .start_op = 1};

    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

TEST(Change, equality_considers_deps) {
    const std::uint8_t hash_raw[32] = {1};
    const auto hash = ChangeHash{hash_raw};

    auto a = Change{.seq = 1};
    auto b = Change{.seq = 1};
    b.deps.push_back(hash);

    EXPECT_NE(a, b);
}
