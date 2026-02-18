#include <automerge-cpp/op.hpp>

#include <gtest/gtest.h>

using namespace automerge_cpp;

TEST(OpType, to_string_view_covers_all_variants) {
    EXPECT_EQ(to_string_view(OpType::put),         "put");
    EXPECT_EQ(to_string_view(OpType::del),         "del");
    EXPECT_EQ(to_string_view(OpType::insert),      "insert");
    EXPECT_EQ(to_string_view(OpType::make_object), "make_object");
    EXPECT_EQ(to_string_view(OpType::increment),   "increment");
    EXPECT_EQ(to_string_view(OpType::splice_text), "splice_text");
    EXPECT_EQ(to_string_view(OpType::mark),        "mark");
}

TEST(Op, construction_and_equality) {
    const auto id = OpId{1, ActorId{}};
    const auto op = Op{
        .id = id,
        .obj = root,
        .key = map_key("name"),
        .action = OpType::put,
        .value = Value{ScalarValue{std::string{"Alice"}}},
        .pred = {},
    };

    EXPECT_EQ(op.id, id);
    EXPECT_EQ(op.action, OpType::put);
    EXPECT_TRUE(op.obj.is_root());
    EXPECT_TRUE(op.pred.empty());
}

TEST(Op, equality_detects_different_actions) {
    const auto base = Op{
        .id = OpId{1, ActorId{}},
        .obj = root,
        .key = map_key("x"),
        .action = OpType::put,
        .value = Value{ScalarValue{std::int64_t{1}}},
        .pred = {},
    };

    auto different = base;
    different.action = OpType::del;

    EXPECT_NE(base, different);
}

TEST(Op, equality_detects_different_predecessors) {
    const auto base = Op{
        .id = OpId{1, ActorId{}},
        .obj = root,
        .key = map_key("x"),
        .action = OpType::put,
        .value = Value{ScalarValue{std::int64_t{1}}},
        .pred = {},
    };

    auto with_pred = base;
    with_pred.pred.push_back(OpId{0, ActorId{}});

    EXPECT_NE(base, with_pred);
}
