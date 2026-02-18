#include "../src/storage/columns/change_op_columns.hpp"

#include <gtest/gtest.h>

using namespace automerge_cpp;
using namespace automerge_cpp::storage;

static auto make_actor(std::uint8_t id) -> ActorId {
    auto actor = ActorId{};
    actor.bytes[15] = static_cast<std::byte>(id);
    return actor;
}

// -- Basic round-trip tests ---------------------------------------------------

TEST(ChangeOpColumns, empty_ops_round_trip) {
    auto actor_table = std::vector<ActorId>{make_actor(1)};
    auto ops = std::vector<Op>{};
    auto columns = encode_change_ops(ops, actor_table);

    auto decoded = decode_change_ops(columns, actor_table, make_actor(1), 1, 0);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_TRUE(decoded->empty());
}

TEST(ChangeOpColumns, map_put_round_trip) {
    auto actor = make_actor(1);
    auto actor_table = std::vector<ActorId>{actor};

    auto ops = std::vector<Op>{
        Op{
            .id = OpId{1, actor},
            .obj = root,
            .key = map_key("name"),
            .action = OpType::put,
            .value = Value{ScalarValue{std::string{"Alice"}}},
            .pred = {},
        },
    };

    auto columns = encode_change_ops(ops, actor_table);
    auto decoded = decode_change_ops(columns, actor_table, actor, 1, 1);

    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->size(), 1u);

    const auto& op = (*decoded)[0];
    EXPECT_EQ(op.id, OpId(1, actor));
    EXPECT_TRUE(op.obj.is_root());
    EXPECT_EQ(std::get<std::string>(op.key), "name");
    EXPECT_EQ(op.action, OpType::put);

    auto* sv = std::get_if<ScalarValue>(&op.value);
    ASSERT_NE(sv, nullptr);
    auto* s = std::get_if<std::string>(sv);
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(*s, "Alice");
}

TEST(ChangeOpColumns, integer_value_round_trip) {
    auto actor = make_actor(1);
    auto actor_table = std::vector<ActorId>{actor};

    auto ops = std::vector<Op>{
        Op{
            .id = OpId{1, actor},
            .obj = root,
            .key = map_key("count"),
            .action = OpType::put,
            .value = Value{ScalarValue{std::int64_t{-42}}},
            .pred = {},
        },
    };

    auto columns = encode_change_ops(ops, actor_table);
    auto decoded = decode_change_ops(columns, actor_table, actor, 1, 1);

    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->size(), 1u);

    auto* sv = std::get_if<ScalarValue>(&(*decoded)[0].value);
    ASSERT_NE(sv, nullptr);
    EXPECT_EQ(std::get<std::int64_t>(*sv), -42);
}

TEST(ChangeOpColumns, multiple_ops_round_trip) {
    auto actor = make_actor(1);
    auto actor_table = std::vector<ActorId>{actor};

    auto ops = std::vector<Op>{
        Op{
            .id = OpId{1, actor},
            .obj = root,
            .key = map_key("a"),
            .action = OpType::put,
            .value = Value{ScalarValue{std::int64_t{1}}},
            .pred = {},
        },
        Op{
            .id = OpId{2, actor},
            .obj = root,
            .key = map_key("b"),
            .action = OpType::put,
            .value = Value{ScalarValue{std::int64_t{2}}},
            .pred = {},
        },
        Op{
            .id = OpId{3, actor},
            .obj = root,
            .key = map_key("c"),
            .action = OpType::put,
            .value = Value{ScalarValue{std::string{"hello"}}},
            .pred = {},
        },
    };

    auto columns = encode_change_ops(ops, actor_table);
    auto decoded = decode_change_ops(columns, actor_table, actor, 1, 3);

    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->size(), 3u);

    EXPECT_EQ(std::get<std::string>((*decoded)[0].key), "a");
    EXPECT_EQ(std::get<std::string>((*decoded)[1].key), "b");
    EXPECT_EQ(std::get<std::string>((*decoded)[2].key), "c");
}

TEST(ChangeOpColumns, delete_op_round_trip) {
    auto actor = make_actor(1);
    auto actor_table = std::vector<ActorId>{actor};

    auto ops = std::vector<Op>{
        Op{
            .id = OpId{2, actor},
            .obj = root,
            .key = map_key("x"),
            .action = OpType::del,
            .value = Value{ScalarValue{Null{}}},
            .pred = {OpId{1, actor}},
        },
    };

    auto columns = encode_change_ops(ops, actor_table);
    auto decoded = decode_change_ops(columns, actor_table, actor, 2, 1);

    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->size(), 1u);

    EXPECT_EQ((*decoded)[0].action, OpType::del);
    ASSERT_EQ((*decoded)[0].pred.size(), 1u);
    EXPECT_EQ((*decoded)[0].pred[0], OpId(1, actor));
}

TEST(ChangeOpColumns, make_object_map_round_trip) {
    auto actor = make_actor(1);
    auto actor_table = std::vector<ActorId>{actor};

    auto ops = std::vector<Op>{
        Op{
            .id = OpId{1, actor},
            .obj = root,
            .key = map_key("nested"),
            .action = OpType::make_object,
            .value = Value{ObjType::map},
            .pred = {},
        },
    };

    auto columns = encode_change_ops(ops, actor_table);
    auto decoded = decode_change_ops(columns, actor_table, actor, 1, 1);

    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->size(), 1u);
    EXPECT_EQ((*decoded)[0].action, OpType::make_object);
}

TEST(ChangeOpColumns, make_object_list_round_trip) {
    auto actor = make_actor(1);
    auto actor_table = std::vector<ActorId>{actor};

    auto ops = std::vector<Op>{
        Op{
            .id = OpId{1, actor},
            .obj = root,
            .key = map_key("items"),
            .action = OpType::make_object,
            .value = Value{ObjType::list},
            .pred = {},
        },
    };

    auto columns = encode_change_ops(ops, actor_table);
    auto decoded = decode_change_ops(columns, actor_table, actor, 1, 1);

    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->size(), 1u);
    EXPECT_EQ((*decoded)[0].action, OpType::make_object);
}

TEST(ChangeOpColumns, insert_op_round_trip) {
    auto actor = make_actor(1);
    auto actor_table = std::vector<ActorId>{actor};
    auto list_obj = ObjId{OpId{1, actor}};

    auto ops = std::vector<Op>{
        Op{
            .id = OpId{2, actor},
            .obj = list_obj,
            .key = list_index(0),
            .action = OpType::insert,
            .value = Value{ScalarValue{std::string{"item1"}}},
            .pred = {},
            .insert_after = std::nullopt,  // insert at head
        },
    };

    auto columns = encode_change_ops(ops, actor_table);
    auto decoded = decode_change_ops(columns, actor_table, actor, 2, 1);

    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->size(), 1u);
    // Insert ops should be decoded
    EXPECT_TRUE((*decoded)[0].action == OpType::insert ||
                (*decoded)[0].action == OpType::splice_text);
}

TEST(ChangeOpColumns, counter_value_round_trip) {
    auto actor = make_actor(1);
    auto actor_table = std::vector<ActorId>{actor};

    auto ops = std::vector<Op>{
        Op{
            .id = OpId{1, actor},
            .obj = root,
            .key = map_key("views"),
            .action = OpType::put,
            .value = Value{ScalarValue{Counter{100}}},
            .pred = {},
        },
    };

    auto columns = encode_change_ops(ops, actor_table);
    auto decoded = decode_change_ops(columns, actor_table, actor, 1, 1);

    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->size(), 1u);

    auto* sv = std::get_if<ScalarValue>(&(*decoded)[0].value);
    ASSERT_NE(sv, nullptr);
    auto* c = std::get_if<Counter>(sv);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->value, 100);
}

TEST(ChangeOpColumns, boolean_value_round_trip) {
    auto actor = make_actor(1);
    auto actor_table = std::vector<ActorId>{actor};

    auto ops = std::vector<Op>{
        Op{
            .id = OpId{1, actor},
            .obj = root,
            .key = map_key("flag"),
            .action = OpType::put,
            .value = Value{ScalarValue{true}},
            .pred = {},
        },
    };

    auto columns = encode_change_ops(ops, actor_table);
    auto decoded = decode_change_ops(columns, actor_table, actor, 1, 1);

    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->size(), 1u);

    auto* sv = std::get_if<ScalarValue>(&(*decoded)[0].value);
    ASSERT_NE(sv, nullptr);
    EXPECT_EQ(std::get<bool>(*sv), true);
}

TEST(ChangeOpColumns, double_value_round_trip) {
    auto actor = make_actor(1);
    auto actor_table = std::vector<ActorId>{actor};

    auto ops = std::vector<Op>{
        Op{
            .id = OpId{1, actor},
            .obj = root,
            .key = map_key("pi"),
            .action = OpType::put,
            .value = Value{ScalarValue{3.14159}},
            .pred = {},
        },
    };

    auto columns = encode_change_ops(ops, actor_table);
    auto decoded = decode_change_ops(columns, actor_table, actor, 1, 1);

    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->size(), 1u);

    auto* sv = std::get_if<ScalarValue>(&(*decoded)[0].value);
    ASSERT_NE(sv, nullptr);
    EXPECT_DOUBLE_EQ(std::get<double>(*sv), 3.14159);
}

TEST(ChangeOpColumns, multi_actor_predecessors) {
    auto actor1 = make_actor(1);
    auto actor2 = make_actor(2);
    auto actor_table = std::vector<ActorId>{actor1, actor2};

    auto ops = std::vector<Op>{
        Op{
            .id = OpId{5, actor1},
            .obj = root,
            .key = map_key("x"),
            .action = OpType::put,
            .value = Value{ScalarValue{std::int64_t{99}}},
            .pred = {OpId{3, actor1}, OpId{4, actor2}},
        },
    };

    auto columns = encode_change_ops(ops, actor_table);
    auto decoded = decode_change_ops(columns, actor_table, actor1, 5, 1);

    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->size(), 1u);
    ASSERT_EQ((*decoded)[0].pred.size(), 2u);
    EXPECT_EQ((*decoded)[0].pred[0], OpId(3, actor1));
    EXPECT_EQ((*decoded)[0].pred[1], OpId(4, actor2));
}

TEST(ChangeOpColumns, nested_object_ops) {
    auto actor = make_actor(1);
    auto actor_table = std::vector<ActorId>{actor};
    auto nested_obj = ObjId{OpId{1, actor}};

    auto ops = std::vector<Op>{
        Op{
            .id = OpId{1, actor},
            .obj = root,
            .key = map_key("child"),
            .action = OpType::make_object,
            .value = Value{ObjType::map},
            .pred = {},
        },
        Op{
            .id = OpId{2, actor},
            .obj = nested_obj,
            .key = map_key("name"),
            .action = OpType::put,
            .value = Value{ScalarValue{std::string{"nested value"}}},
            .pred = {},
        },
    };

    auto columns = encode_change_ops(ops, actor_table);
    auto decoded = decode_change_ops(columns, actor_table, actor, 1, 2);

    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->size(), 2u);

    EXPECT_EQ((*decoded)[0].action, OpType::make_object);
    EXPECT_TRUE((*decoded)[0].obj.is_root());

    EXPECT_EQ((*decoded)[1].action, OpType::put);
    EXPECT_FALSE((*decoded)[1].obj.is_root());
}

TEST(ChangeOpColumns, increment_op_round_trip) {
    auto actor = make_actor(1);
    auto actor_table = std::vector<ActorId>{actor};

    auto ops = std::vector<Op>{
        Op{
            .id = OpId{2, actor},
            .obj = root,
            .key = map_key("views"),
            .action = OpType::increment,
            .value = Value{ScalarValue{Counter{5}}},
            .pred = {OpId{1, actor}},
        },
    };

    auto columns = encode_change_ops(ops, actor_table);
    auto decoded = decode_change_ops(columns, actor_table, actor, 2, 1);

    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->size(), 1u);
    EXPECT_EQ((*decoded)[0].action, OpType::increment);
}
