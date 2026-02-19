// json_test.cpp — Tests for nlohmann/json interoperability (Phases 12B–12F)

#include <automerge-cpp/json.hpp>
#include <automerge-cpp/automerge.hpp>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace am = automerge_cpp;
using json = nlohmann::json;

// =============================================================================
// get_obj_id tests
// =============================================================================

TEST(GetObjId, map_child) {
    auto doc = am::Document{};
    auto child_id = doc.transact([](am::Transaction& tx) {
        return tx.put_object(am::root, "nested", am::ObjType::map);
    });
    auto result = doc.get_obj_id(am::root, "nested");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, child_id);
}

TEST(GetObjId, list_child) {
    auto doc = am::Document{};
    auto list_id = doc.transact([](am::Transaction& tx) {
        auto list = tx.put_object(am::root, "items", am::ObjType::list);
        tx.insert_object(list, 0, am::ObjType::map);
        return list;
    });
    auto child = doc.get_obj_id(list_id, std::size_t{0});
    ASSERT_TRUE(child.has_value());
}

TEST(GetObjId, nonexistent_key_returns_nullopt) {
    auto doc = am::Document{};
    EXPECT_FALSE(doc.get_obj_id(am::root, "nope").has_value());
}

TEST(GetObjId, scalar_value_returns_nullopt) {
    auto doc = am::Document{};
    doc.transact([](am::Transaction& tx) {
        tx.put(am::root, "x", 42);
    });
    EXPECT_FALSE(doc.get_obj_id(am::root, "x").has_value());
}

TEST(GetObjId, out_of_bounds_index_returns_nullopt) {
    auto doc = am::Document{};
    doc.transact([](am::Transaction& tx) {
        tx.put_object(am::root, "list", am::ObjType::list);
    });
    auto list = doc.get_obj_id(am::root, "list");
    ASSERT_TRUE(list.has_value());
    EXPECT_FALSE(doc.get_obj_id(*list, std::size_t{0}).has_value());
}

// =============================================================================
// ADL serialization tests (Phase 12B) — stays in am:: namespace
// =============================================================================

TEST(JsonAdl, null_to_json) {
    json j = am::Null{};
    EXPECT_TRUE(j.is_null());
}

TEST(JsonAdl, bool_scalar_round_trip) {
    auto sv = am::ScalarValue{true};
    json j = sv;
    EXPECT_EQ(j, true);
    auto sv2 = am::ScalarValue{};
    am::from_json(j, sv2);
    EXPECT_EQ(sv2, sv);
}

TEST(JsonAdl, int64_scalar_round_trip) {
    auto sv = am::ScalarValue{std::int64_t{-42}};
    json j = sv;
    EXPECT_EQ(j, -42);
    auto sv2 = am::ScalarValue{};
    am::from_json(j, sv2);
    EXPECT_EQ(std::get<std::int64_t>(sv2), -42);
}

TEST(JsonAdl, uint64_scalar_round_trip) {
    auto val = std::uint64_t{18446744073709551615ULL};
    auto sv = am::ScalarValue{val};
    json j = sv;
    EXPECT_EQ(j, val);
    auto sv2 = am::ScalarValue{};
    am::from_json(j, sv2);
    EXPECT_EQ(std::get<std::uint64_t>(sv2), val);
}

TEST(JsonAdl, double_scalar_round_trip) {
    auto sv = am::ScalarValue{3.14};
    json j = sv;
    EXPECT_DOUBLE_EQ(j.get<double>(), 3.14);
    auto sv2 = am::ScalarValue{};
    am::from_json(j, sv2);
    EXPECT_DOUBLE_EQ(std::get<double>(sv2), 3.14);
}

TEST(JsonAdl, string_scalar_round_trip) {
    auto sv = am::ScalarValue{std::string{"hello"}};
    json j = sv;
    EXPECT_EQ(j, "hello");
    auto sv2 = am::ScalarValue{};
    am::from_json(j, sv2);
    EXPECT_EQ(std::get<std::string>(sv2), "hello");
}

TEST(JsonAdl, counter_tagged_format) {
    auto sv = am::ScalarValue{am::Counter{42}};
    json j = sv;
    EXPECT_EQ(j["__type"], "counter");
    EXPECT_EQ(j["value"], 42);
    auto sv2 = am::ScalarValue{};
    am::from_json(j, sv2);
    EXPECT_EQ(std::get<am::Counter>(sv2).value, 42);
}

TEST(JsonAdl, timestamp_tagged_format) {
    auto sv = am::ScalarValue{am::Timestamp{1234567890}};
    json j = sv;
    EXPECT_EQ(j["__type"], "timestamp");
    EXPECT_EQ(j["value"], 1234567890);
    auto sv2 = am::ScalarValue{};
    am::from_json(j, sv2);
    EXPECT_EQ(std::get<am::Timestamp>(sv2).millis_since_epoch, 1234567890);
}

TEST(JsonAdl, bytes_tagged_format) {
    auto bytes = am::Bytes{std::byte{0xDE}, std::byte{0xAD}, std::byte{0xBE}, std::byte{0xEF}};
    auto sv = am::ScalarValue{bytes};
    json j = sv;
    EXPECT_EQ(j["__type"], "bytes");
    EXPECT_TRUE(j["value"].is_string());
    auto sv2 = am::ScalarValue{};
    am::from_json(j, sv2);
    EXPECT_EQ(std::get<am::Bytes>(sv2), bytes);
}

TEST(JsonAdl, null_scalar_round_trip) {
    auto sv = am::ScalarValue{am::Null{}};
    json j = sv;
    EXPECT_TRUE(j.is_null());
    auto sv2 = am::ScalarValue{};
    am::from_json(j, sv2);
    EXPECT_TRUE(std::holds_alternative<am::Null>(sv2));
}

TEST(JsonAdl, actor_id_hex_round_trip) {
    auto id = am::ActorId{};
    id.bytes[0] = std::byte{0xAB};
    id.bytes[15] = std::byte{0xCD};
    json j = id;
    EXPECT_TRUE(j.is_string());
    auto id2 = am::ActorId{};
    am::from_json(j, id2);
    EXPECT_EQ(id, id2);
}

TEST(JsonAdl, change_hash_hex_round_trip) {
    auto h = am::ChangeHash{};
    h.bytes[0] = std::byte{0xFF};
    h.bytes[31] = std::byte{0x01};
    json j = h;
    EXPECT_TRUE(j.is_string());
    EXPECT_EQ(j.get<std::string>().size(), 64u);
    auto h2 = am::ChangeHash{};
    am::from_json(j, h2);
    EXPECT_EQ(h, h2);
}

TEST(JsonAdl, op_id_to_json) {
    auto id = am::OpId{5, am::ActorId{}};
    json j = id;
    EXPECT_EQ(j["counter"], 5);
    EXPECT_TRUE(j.contains("actor"));
}

TEST(JsonAdl, obj_id_root_to_json) {
    json j = am::root;
    EXPECT_EQ(j, "root");
}

TEST(JsonAdl, obj_id_non_root_to_json) {
    auto obj = am::ObjId{am::OpId{3, am::ActorId{}}};
    json j = obj;
    EXPECT_EQ(j["counter"], 3);
}

TEST(JsonAdl, change_to_json) {
    auto c = am::Change{};
    c.seq = 1;
    c.start_op = 1;
    c.message = "test";
    json j = c;
    EXPECT_EQ(j["seq"], 1);
    EXPECT_EQ(j["message"], "test");
}

TEST(JsonAdl, patch_to_json) {
    auto p = am::Patch{
        am::root,
        am::Prop{std::string{"key"}},
        am::PatchPut{am::Value{am::ScalarValue{std::int64_t{42}}}, false},
    };
    json j = p;
    EXPECT_EQ(j["action"]["type"], "put");
    EXPECT_EQ(j["key"], "key");
}

TEST(JsonAdl, mark_to_json) {
    auto m = am::Mark{0, 5, "bold", am::ScalarValue{true}};
    json j = m;
    EXPECT_EQ(j["start"], 0);
    EXPECT_EQ(j["end"], 5);
    EXPECT_EQ(j["name"], "bold");
    EXPECT_EQ(j["value"], true);
}

TEST(JsonAdl, cursor_to_json) {
    auto c = am::Cursor{am::OpId{7, am::ActorId{}}};
    json j = c;
    EXPECT_EQ(j["counter"], 7);
}

TEST(JsonAdl, from_json_infers_int_for_small_unsigned) {
    json j = 42;
    auto sv = am::ScalarValue{};
    am::from_json(j, sv);
    EXPECT_TRUE(std::holds_alternative<std::int64_t>(sv));
    EXPECT_EQ(std::get<std::int64_t>(sv), 42);
}

// =============================================================================
// Document export tests (Phase 12C) — am::json:: namespace
// =============================================================================

TEST(JsonExport, empty_document) {
    auto doc = am::Document{};
    auto j = am::json::export_json(doc);
    EXPECT_TRUE(j.is_object());
    EXPECT_TRUE(j.empty());
}

TEST(JsonExport, flat_map) {
    auto doc = am::Document{};
    doc.transact([](am::Transaction& tx) {
        tx.put(am::root, "name", "Alice");
        tx.put(am::root, "age", 30);
        tx.put(am::root, "active", true);
    });
    auto j = am::json::export_json(doc);
    EXPECT_EQ(j["name"], "Alice");
    EXPECT_EQ(j["age"], 30);
    EXPECT_EQ(j["active"], true);
}

TEST(JsonExport, nested_map) {
    auto doc = am::Document{};
    doc.transact([](am::Transaction& tx) {
        tx.put(am::root, "config", am::Map{{"port", 8080}, {"host", "localhost"}});
    });
    auto j = am::json::export_json(doc);
    EXPECT_EQ(j["config"]["port"], 8080);
    EXPECT_EQ(j["config"]["host"], "localhost");
}

TEST(JsonExport, list) {
    auto doc = am::Document{};
    doc.transact([](am::Transaction& tx) {
        tx.put(am::root, "items", am::List{"Milk", "Eggs", "Bread"});
    });
    auto j = am::json::export_json(doc);
    EXPECT_EQ(j["items"].size(), 3u);
    EXPECT_EQ(j["items"][0], "Milk");
    EXPECT_EQ(j["items"][1], "Eggs");
    EXPECT_EQ(j["items"][2], "Bread");
}

TEST(JsonExport, mixed_types) {
    auto doc = am::Document{};
    doc.transact([](am::Transaction& tx) {
        tx.put(am::root, "s", "hello");
        tx.put(am::root, "i", 42);
        tx.put(am::root, "d", 3.14);
        tx.put(am::root, "b", true);
        tx.put(am::root, "n", am::Null{});
    });
    auto j = am::json::export_json(doc);
    EXPECT_EQ(j["s"], "hello");
    EXPECT_EQ(j["i"], 42);
    EXPECT_DOUBLE_EQ(j["d"].get<double>(), 3.14);
    EXPECT_EQ(j["b"], true);
    EXPECT_TRUE(j["n"].is_null());
}

TEST(JsonExport, text_object_as_string) {
    auto doc = am::Document{};
    doc.transact([](am::Transaction& tx) {
        auto text = tx.put_object(am::root, "content", am::ObjType::text);
        tx.splice_text(text, 0, 0, "hello world");
    });
    auto j = am::json::export_json(doc);
    EXPECT_EQ(j["content"], "hello world");
}

TEST(JsonExport, deeply_nested) {
    auto doc = am::Document{};
    doc.transact([](am::Transaction& tx) {
        auto a = tx.put_object(am::root, "a", am::ObjType::map);
        auto b = tx.put_object(a, "b", am::ObjType::map);
        tx.put(b, "c", 42);
    });
    auto j = am::json::export_json(doc);
    EXPECT_EQ(j["a"]["b"]["c"], 42);
}

TEST(JsonExport, list_of_maps) {
    auto doc = am::Document{};
    doc.transact([](am::Transaction& tx) {
        auto list = tx.put_object(am::root, "users", am::ObjType::list);
        auto u0 = tx.insert_object(list, 0, am::ObjType::map);
        tx.put(u0, "name", "Alice");
        auto u1 = tx.insert_object(list, 1, am::ObjType::map);
        tx.put(u1, "name", "Bob");
    });
    auto j = am::json::export_json(doc);
    EXPECT_EQ(j["users"][0]["name"], "Alice");
    EXPECT_EQ(j["users"][1]["name"], "Bob");
}

TEST(JsonExport, subtree_export) {
    auto doc = am::Document{};
    auto config = doc.transact([](am::Transaction& tx) {
        return tx.put(am::root, "config", am::Map{{"port", 8080}});
    });
    auto j = am::json::export_json(doc, config);
    EXPECT_EQ(j["port"], 8080);
}

TEST(JsonExport, after_merge) {
    auto doc = am::Document{};
    doc.transact([](am::Transaction& tx) { tx.put(am::root, "x", 1); });
    auto fork = doc.fork();
    fork.transact([](am::Transaction& tx) { tx.put(am::root, "y", 2); });
    doc.merge(fork);
    auto j = am::json::export_json(doc);
    EXPECT_EQ(j["x"], 1);
    EXPECT_EQ(j["y"], 2);
}

TEST(JsonExport, counter_as_number) {
    auto doc = am::Document{};
    doc.transact([](am::Transaction& tx) {
        tx.put(am::root, "views", am::Counter{100});
    });
    auto j = am::json::export_json(doc);
    EXPECT_EQ(j["views"], 100);
}

TEST(JsonExportAt, historical_export) {
    auto doc = am::Document{};
    doc.transact([](am::Transaction& tx) { tx.put(am::root, "x", 1); });
    auto heads1 = doc.get_heads();
    doc.transact([](am::Transaction& tx) { tx.put(am::root, "x", 2); });

    auto j_now = am::json::export_json(doc);
    EXPECT_EQ(j_now["x"], 2);

    auto j_then = am::json::export_json_at(doc, heads1);
    EXPECT_EQ(j_then["x"], 1);
}

// =============================================================================
// Document import tests (Phase 12C)
// =============================================================================

TEST(JsonImport, flat_json) {
    auto doc = am::Document{};
    am::json::import_json(doc, json{{"name", "Alice"}, {"age", 30}});
    EXPECT_EQ(*doc.get<std::string>(am::root, "name"), "Alice");
    EXPECT_EQ(*doc.get<std::int64_t>(am::root, "age"), 30);
}

TEST(JsonImport, nested_objects) {
    auto doc = am::Document{};
    am::json::import_json(doc, json{{"config", {{"port", 8080}}}});
    auto val = doc.get_path("config", "port");
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*am::get_scalar<std::int64_t>(*val), 8080);
}

TEST(JsonImport, arrays) {
    auto doc = am::Document{};
    am::json::import_json(doc, json{{"items", {"a", "b", "c"}}});
    auto items = doc.get_obj_id(am::root, "items");
    ASSERT_TRUE(items.has_value());
    EXPECT_EQ(doc.length(*items), 3u);
    EXPECT_EQ(*doc.get<std::string>(*items, std::size_t{0}), "a");
}

TEST(JsonImport, null_bool_float) {
    auto doc = am::Document{};
    am::json::import_json(doc, json{{"n", nullptr}, {"b", true}, {"f", 3.14}});
    auto n = doc.get(am::root, "n");
    ASSERT_TRUE(n.has_value());
    EXPECT_TRUE(std::holds_alternative<am::Null>(std::get<am::ScalarValue>(*n)));
    EXPECT_EQ(*doc.get<bool>(am::root, "b"), true);
    EXPECT_DOUBLE_EQ(*doc.get<double>(am::root, "f"), 3.14);
}

TEST(JsonImport, with_transaction) {
    auto doc = am::Document{};
    doc.transact([](am::Transaction& tx) {
        am::json::import_json(tx, json{{"x", 1}, {"y", 2}});
    });
    EXPECT_EQ(*doc.get<std::int64_t>(am::root, "x"), 1);
    EXPECT_EQ(*doc.get<std::int64_t>(am::root, "y"), 2);
}

TEST(JsonImport, round_trip_flat) {
    auto input = json{{"name", "Alice"}, {"age", 30}, {"active", true}};
    auto doc = am::Document{};
    am::json::import_json(doc, input);
    auto output = am::json::export_json(doc);
    EXPECT_EQ(output, input);
}

TEST(JsonImport, round_trip_nested) {
    auto input = json{
        {"config", {{"port", 8080}, {"host", "localhost"}}},
        {"items", {"a", "b", "c"}},
    };
    auto doc = am::Document{};
    am::json::import_json(doc, input);
    auto output = am::json::export_json(doc);
    EXPECT_EQ(output, input);
}

TEST(JsonImport, round_trip_deeply_nested) {
    auto input = json{
        {"a", {{"b", {{"c", 42}}}}},
        {"list", {1, 2, {{"nested", true}}}},
    };
    auto doc = am::Document{};
    am::json::import_json(doc, input);
    auto output = am::json::export_json(doc);
    EXPECT_EQ(output, input);
}

TEST(JsonImport, round_trip_empty_containers) {
    auto input = json{{"obj", json::object()}, {"arr", json::array()}};
    auto doc = am::Document{};
    am::json::import_json(doc, input);
    auto output = am::json::export_json(doc);
    EXPECT_EQ(output, input);
}

TEST(JsonImport, array_of_objects) {
    auto input = json{{"users", json::array({
        {{"name", "Alice"}},
        {{"name", "Bob"}},
    })}};
    auto doc = am::Document{};
    am::json::import_json(doc, input);
    auto output = am::json::export_json(doc);
    EXPECT_EQ(output, input);
}

// =============================================================================
// JSON Pointer tests (Phase 12D)
// =============================================================================

TEST(JsonPointer, get_root_key) {
    auto doc = am::Document{};
    doc.transact([](am::Transaction& tx) { tx.put(am::root, "x", 42); });
    auto val = am::json::get_pointer(doc, "/x");
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*am::get_scalar<std::int64_t>(*val), 42);
}

TEST(JsonPointer, get_nested_key) {
    auto doc = am::Document{};
    doc.transact([](am::Transaction& tx) {
        tx.put(am::root, "config", am::Map{{"port", 8080}});
    });
    auto val = am::json::get_pointer(doc, "/config/port");
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*am::get_scalar<std::int64_t>(*val), 8080);
}

TEST(JsonPointer, get_list_index) {
    auto doc = am::Document{};
    doc.transact([](am::Transaction& tx) {
        tx.put(am::root, "items", am::List{"a", "b", "c"});
    });
    auto val = am::json::get_pointer(doc, "/items/1");
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*am::get_scalar<std::string>(*val), "b");
}

TEST(JsonPointer, get_missing_returns_nullopt) {
    auto doc = am::Document{};
    EXPECT_FALSE(am::json::get_pointer(doc, "/nope").has_value());
}

TEST(JsonPointer, get_out_of_bounds_returns_nullopt) {
    auto doc = am::Document{};
    doc.transact([](am::Transaction& tx) {
        tx.put(am::root, "list", am::List{"a"});
    });
    EXPECT_FALSE(am::json::get_pointer(doc, "/list/5").has_value());
}

TEST(JsonPointer, empty_pointer_returns_root) {
    auto doc = am::Document{};
    auto val = am::json::get_pointer(doc, "");
    ASSERT_TRUE(val.has_value());
    EXPECT_TRUE(std::holds_alternative<am::ObjType>(*val));
}

TEST(JsonPointer, escaped_tilde) {
    auto doc = am::Document{};
    doc.transact([](am::Transaction& tx) {
        tx.put(am::root, "a/b", 1);
        tx.put(am::root, "c~d", 2);
    });
    auto v1 = am::json::get_pointer(doc, "/a~1b");
    ASSERT_TRUE(v1.has_value());
    EXPECT_EQ(*am::get_scalar<std::int64_t>(*v1), 1);

    auto v2 = am::json::get_pointer(doc, "/c~0d");
    ASSERT_TRUE(v2.has_value());
    EXPECT_EQ(*am::get_scalar<std::int64_t>(*v2), 2);
}

TEST(JsonPointer, put_creates_value) {
    auto doc = am::Document{};
    am::json::put_pointer(doc, "/name", am::ScalarValue{std::string{"Alice"}});
    EXPECT_EQ(*doc.get<std::string>(am::root, "name"), "Alice");
}

TEST(JsonPointer, put_nested_creates_intermediates) {
    auto doc = am::Document{};
    am::json::put_pointer(doc, "/a/b", am::ScalarValue{std::int64_t{42}});
    auto val = doc.get_path("a", "b");
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*am::get_scalar<std::int64_t>(*val), 42);
}

TEST(JsonPointer, put_list_append_with_dash) {
    auto doc = am::Document{};
    doc.transact([](am::Transaction& tx) {
        tx.put(am::root, "items", am::List{"a"});
    });
    am::json::put_pointer(doc, "/items/-", am::ScalarValue{std::string{"b"}});
    auto items = doc.get_obj_id(am::root, "items");
    ASSERT_TRUE(items.has_value());
    EXPECT_EQ(doc.length(*items), 2u);
    EXPECT_EQ(*doc.get<std::string>(*items, std::size_t{1}), "b");
}

TEST(JsonPointer, delete_map_key) {
    auto doc = am::Document{};
    doc.transact([](am::Transaction& tx) {
        tx.put(am::root, "x", 1);
        tx.put(am::root, "y", 2);
    });
    am::json::delete_pointer(doc, "/x");
    EXPECT_FALSE(doc.get(am::root, "x").has_value());
    EXPECT_TRUE(doc.get(am::root, "y").has_value());
}

TEST(JsonPointer, delete_list_index) {
    auto doc = am::Document{};
    doc.transact([](am::Transaction& tx) {
        tx.put(am::root, "items", am::List{"a", "b", "c"});
    });
    am::json::delete_pointer(doc, "/items/1");
    auto items = doc.get_obj_id(am::root, "items");
    ASSERT_TRUE(items.has_value());
    EXPECT_EQ(doc.length(*items), 2u);
    EXPECT_EQ(*doc.get<std::string>(*items, std::size_t{0}), "a");
    EXPECT_EQ(*doc.get<std::string>(*items, std::size_t{1}), "c");
}

TEST(JsonPointer, invalid_pointer_throws) {
    auto doc = am::Document{};
    EXPECT_THROW(am::json::get_pointer(doc, "no-slash"), std::runtime_error);
}

// =============================================================================
// JSON Patch tests (Phase 12E)
// =============================================================================

TEST(JsonPatch, add_to_map) {
    auto doc = am::Document{};
    am::json::apply_json_patch(doc, json::parse(R"([
        {"op": "add", "path": "/name", "value": "Alice"}
    ])"));
    EXPECT_EQ(*doc.get<std::string>(am::root, "name"), "Alice");
}

TEST(JsonPatch, add_to_list) {
    auto doc = am::Document{};
    doc.transact([](am::Transaction& tx) {
        tx.put(am::root, "items", am::List{"a", "c"});
    });
    am::json::apply_json_patch(doc, json::parse(R"([
        {"op": "add", "path": "/items/1", "value": "b"}
    ])"));
    auto items = doc.get_obj_id(am::root, "items");
    EXPECT_EQ(doc.length(*items), 3u);
    EXPECT_EQ(*doc.get<std::string>(*items, std::size_t{1}), "b");
}

TEST(JsonPatch, add_list_append_with_dash) {
    auto doc = am::Document{};
    doc.transact([](am::Transaction& tx) {
        tx.put(am::root, "items", am::List{"a"});
    });
    am::json::apply_json_patch(doc, json::parse(R"([
        {"op": "add", "path": "/items/-", "value": "b"}
    ])"));
    auto items = doc.get_obj_id(am::root, "items");
    EXPECT_EQ(doc.length(*items), 2u);
    EXPECT_EQ(*doc.get<std::string>(*items, std::size_t{1}), "b");
}

TEST(JsonPatch, remove_from_map) {
    auto doc = am::Document{};
    doc.transact([](am::Transaction& tx) {
        tx.put(am::root, "x", 1);
        tx.put(am::root, "y", 2);
    });
    am::json::apply_json_patch(doc, json::parse(R"([
        {"op": "remove", "path": "/x"}
    ])"));
    EXPECT_FALSE(doc.get(am::root, "x").has_value());
    EXPECT_TRUE(doc.get(am::root, "y").has_value());
}

TEST(JsonPatch, remove_from_list) {
    auto doc = am::Document{};
    doc.transact([](am::Transaction& tx) {
        tx.put(am::root, "items", am::List{"a", "b", "c"});
    });
    am::json::apply_json_patch(doc, json::parse(R"([
        {"op": "remove", "path": "/items/1"}
    ])"));
    auto items = doc.get_obj_id(am::root, "items");
    EXPECT_EQ(doc.length(*items), 2u);
}

TEST(JsonPatch, replace) {
    auto doc = am::Document{};
    doc.transact([](am::Transaction& tx) { tx.put(am::root, "x", 1); });
    am::json::apply_json_patch(doc, json::parse(R"([
        {"op": "replace", "path": "/x", "value": 99}
    ])"));
    EXPECT_EQ(*doc.get<std::int64_t>(am::root, "x"), 99);
}

TEST(JsonPatch, move_between_keys) {
    auto doc = am::Document{};
    doc.transact([](am::Transaction& tx) {
        tx.put(am::root, "old", "value");
    });
    am::json::apply_json_patch(doc, json::parse(R"([
        {"op": "move", "from": "/old", "path": "/new"}
    ])"));
    EXPECT_FALSE(doc.get(am::root, "old").has_value());
    EXPECT_EQ(*doc.get<std::string>(am::root, "new"), "value");
}

TEST(JsonPatch, copy_value) {
    auto doc = am::Document{};
    doc.transact([](am::Transaction& tx) { tx.put(am::root, "src", 42); });
    am::json::apply_json_patch(doc, json::parse(R"([
        {"op": "copy", "from": "/src", "path": "/dst"}
    ])"));
    EXPECT_EQ(*doc.get<std::int64_t>(am::root, "src"), 42);
    EXPECT_EQ(*doc.get<std::int64_t>(am::root, "dst"), 42);
}

TEST(JsonPatch, test_passes) {
    auto doc = am::Document{};
    doc.transact([](am::Transaction& tx) { tx.put(am::root, "x", 42); });
    EXPECT_NO_THROW(am::json::apply_json_patch(doc, json::parse(R"([
        {"op": "test", "path": "/x", "value": 42}
    ])")));
}

TEST(JsonPatch, test_fails_throws) {
    auto doc = am::Document{};
    doc.transact([](am::Transaction& tx) { tx.put(am::root, "x", 42); });
    EXPECT_THROW(am::json::apply_json_patch(doc, json::parse(R"([
        {"op": "test", "path": "/x", "value": 99}
    ])")), std::runtime_error);
}

TEST(JsonPatch, not_array_throws) {
    auto doc = am::Document{};
    EXPECT_THROW(am::json::apply_json_patch(doc, json{{"op", "add"}}), std::runtime_error);
}

TEST(JsonPatch, add_nested_object) {
    auto doc = am::Document{};
    am::json::apply_json_patch(doc, json::parse(R"([
        {"op": "add", "path": "/config", "value": {"port": 8080}}
    ])"));
    auto val = doc.get_path("config", "port");
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*am::get_scalar<std::int64_t>(*val), 8080);
}

TEST(JsonPatch, multiple_ops_atomic) {
    auto doc = am::Document{};
    doc.transact([](am::Transaction& tx) {
        tx.put(am::root, "a", 1);
        tx.put(am::root, "b", 2);
    });
    am::json::apply_json_patch(doc, json::parse(R"([
        {"op": "replace", "path": "/a", "value": 10},
        {"op": "replace", "path": "/b", "value": 20}
    ])"));
    EXPECT_EQ(*doc.get<std::int64_t>(am::root, "a"), 10);
    EXPECT_EQ(*doc.get<std::int64_t>(am::root, "b"), 20);
}

TEST(JsonPatch, diff_generates_patch) {
    auto doc1 = am::Document{};
    am::json::import_json(doc1, json{{"x", 1}, {"y", 2}});

    auto doc2 = doc1.fork();
    doc2.transact([](am::Transaction& tx) {
        tx.put(am::root, "x", 99);
        tx.put(am::root, "z", 3);
    });

    auto patch = am::json::diff_json_patch(doc1, doc2);
    EXPECT_TRUE(patch.is_array());
    EXPECT_GT(patch.size(), 0u);
}

TEST(JsonPatch, diff_round_trip) {
    auto doc1 = am::Document{};
    am::json::import_json(doc1, json{{"x", 1}, {"y", 2}});

    auto doc2 = doc1.fork();
    doc2.transact([](am::Transaction& tx) {
        tx.put(am::root, "x", 99);
    });

    auto patch = am::json::diff_json_patch(doc1, doc2);
    am::json::apply_json_patch(doc1, patch);
    EXPECT_EQ(am::json::export_json(doc1), am::json::export_json(doc2));
}

// =============================================================================
// JSON Merge Patch tests (Phase 12F)
// =============================================================================

TEST(JsonMergePatch, set_scalar) {
    auto doc = am::Document{};
    doc.transact([](am::Transaction& tx) { tx.put(am::root, "x", 1); });
    am::json::apply_merge_patch(doc, json{{"x", 2}});
    EXPECT_EQ(*doc.get<std::int64_t>(am::root, "x"), 2);
}

TEST(JsonMergePatch, add_new_key) {
    auto doc = am::Document{};
    am::json::apply_merge_patch(doc, json{{"name", "Alice"}});
    EXPECT_EQ(*doc.get<std::string>(am::root, "name"), "Alice");
}

TEST(JsonMergePatch, delete_with_null) {
    auto doc = am::Document{};
    doc.transact([](am::Transaction& tx) {
        tx.put(am::root, "x", 1);
        tx.put(am::root, "y", 2);
    });
    am::json::apply_merge_patch(doc, json{{"x", nullptr}});
    EXPECT_FALSE(doc.get(am::root, "x").has_value());
    EXPECT_TRUE(doc.get(am::root, "y").has_value());
}

TEST(JsonMergePatch, recursive_merge) {
    auto doc = am::Document{};
    doc.transact([](am::Transaction& tx) {
        tx.put(am::root, "config", am::Map{{"port", 8080}, {"host", "localhost"}});
    });
    am::json::apply_merge_patch(doc, json{{"config", {{"port", 9090}}}});
    auto j = am::json::export_json(doc);
    EXPECT_EQ(j["config"]["port"], 9090);
    EXPECT_EQ(j["config"]["host"], "localhost");
}

TEST(JsonMergePatch, idempotent) {
    auto doc = am::Document{};
    doc.transact([](am::Transaction& tx) { tx.put(am::root, "x", 1); });
    auto patch = json{{"x", 2}};
    am::json::apply_merge_patch(doc, patch);
    am::json::apply_merge_patch(doc, patch);
    EXPECT_EQ(*doc.get<std::int64_t>(am::root, "x"), 2);
}

TEST(JsonMergePatch, replace_with_array) {
    auto doc = am::Document{};
    doc.transact([](am::Transaction& tx) { tx.put(am::root, "items", "old"); });
    am::json::apply_merge_patch(doc, json{{"items", {1, 2, 3}}});
    auto j = am::json::export_json(doc);
    EXPECT_EQ(j["items"], json({1, 2, 3}));
}

TEST(JsonMergePatch, generate_patch) {
    auto doc1 = am::Document{};
    am::json::import_json(doc1, json{{"x", 1}, {"y", 2}});

    auto doc2 = doc1.fork();
    doc2.transact([](am::Transaction& tx) {
        tx.put(am::root, "x", 99);
    });

    auto patch = am::json::generate_merge_patch(doc1, doc2);
    EXPECT_TRUE(patch.is_object());
    EXPECT_EQ(patch["x"], 99);
}

TEST(JsonMergePatch, multiple_changes) {
    auto doc = am::Document{};
    doc.transact([](am::Transaction& tx) {
        tx.put(am::root, "a", 1);
        tx.put(am::root, "b", 2);
        tx.put(am::root, "c", 3);
    });
    am::json::apply_merge_patch(doc, json{
        {"a", 10}, {"b", nullptr}, {"d", 4}
    });
    auto j = am::json::export_json(doc);
    EXPECT_EQ(j["a"], 10);
    EXPECT_FALSE(j.contains("b"));
    EXPECT_EQ(j["c"], 3);
    EXPECT_EQ(j["d"], 4);
}

// =============================================================================
// Flatten / Unflatten tests (Phase 12F)
// =============================================================================

TEST(JsonFlatten, flat_map) {
    auto doc = am::Document{};
    doc.transact([](am::Transaction& tx) {
        tx.put(am::root, "x", 1);
        tx.put(am::root, "y", 2);
    });
    auto flat = am::json::flatten(doc);
    EXPECT_EQ(flat["/x"], 1);
    EXPECT_EQ(flat["/y"], 2);
}

TEST(JsonFlatten, nested_map) {
    auto doc = am::Document{};
    doc.transact([](am::Transaction& tx) {
        tx.put(am::root, "a", am::Map{{"b", 42}});
    });
    auto flat = am::json::flatten(doc);
    EXPECT_EQ(flat["/a/b"], 42);
}

TEST(JsonFlatten, list_indices) {
    auto doc = am::Document{};
    doc.transact([](am::Transaction& tx) {
        tx.put(am::root, "items", am::List{"a", "b"});
    });
    auto flat = am::json::flatten(doc);
    EXPECT_EQ(flat["/items/0"], "a");
    EXPECT_EQ(flat["/items/1"], "b");
}

TEST(JsonFlatten, deeply_nested) {
    auto doc = am::Document{};
    doc.transact([](am::Transaction& tx) {
        auto a = tx.put_object(am::root, "a", am::ObjType::map);
        auto b = tx.put_object(a, "b", am::ObjType::map);
        tx.put(b, "c", 42);
    });
    auto flat = am::json::flatten(doc);
    EXPECT_EQ(flat["/a/b/c"], 42);
}

TEST(JsonFlatten, escaped_keys) {
    auto doc = am::Document{};
    doc.transact([](am::Transaction& tx) {
        tx.put(am::root, "a/b", 1);
        tx.put(am::root, "c~d", 2);
    });
    auto flat = am::json::flatten(doc);
    EXPECT_EQ(flat["/a~1b"], 1);
    EXPECT_EQ(flat["/c~0d"], 2);
}

TEST(JsonFlatten, empty_document) {
    auto doc = am::Document{};
    auto flat = am::json::flatten(doc);
    EXPECT_TRUE(flat.empty());
}

TEST(JsonUnflatten, recreates_nested_structure) {
    auto flat = std::map<std::string, json>{
        {"/name", "Alice"},
        {"/config/port", 8080},
    };
    auto doc = am::Document{};
    am::json::unflatten(doc, flat);
    EXPECT_EQ(*doc.get<std::string>(am::root, "name"), "Alice");
    auto val = doc.get_path("config", "port");
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*am::get_scalar<std::int64_t>(*val), 8080);
}

TEST(JsonFlatten, round_trip) {
    auto doc = am::Document{};
    doc.transact([](am::Transaction& tx) {
        tx.put(am::root, "x", 1);
        tx.put(am::root, "config", am::Map{{"port", 8080}});
    });
    auto flat = am::json::flatten(doc);

    auto doc2 = am::Document{};
    am::json::unflatten(doc2, flat);
    EXPECT_EQ(am::json::export_json(doc), am::json::export_json(doc2));
}

TEST(JsonFlatten, text_object) {
    auto doc = am::Document{};
    doc.transact([](am::Transaction& tx) {
        auto text = tx.put_object(am::root, "content", am::ObjType::text);
        tx.splice_text(text, 0, 0, "hello");
    });
    auto flat = am::json::flatten(doc);
    EXPECT_EQ(flat["/content"], "hello");
}

// =============================================================================
// Integration / property tests
// =============================================================================

TEST(JsonIntegration, merge_commutativity_in_json) {
    auto a = am::Document{};
    am::json::import_json(a, json{{"x", 1}});
    auto b = a.fork();
    a.transact([](am::Transaction& tx) { tx.put(am::root, "a", 1); });
    b.transact([](am::Transaction& tx) { tx.put(am::root, "b", 2); });

    auto ab = am::Document{a};
    ab.merge(b);
    auto ba = am::Document{b};
    ba.merge(a);
    EXPECT_EQ(am::json::export_json(ab), am::json::export_json(ba));
}

TEST(JsonIntegration, import_export_save_load_round_trip) {
    auto input = json{
        {"name", "test"},
        {"config", {{"port", 8080}}},
        {"items", {1, 2, 3}},
    };
    auto doc = am::Document{};
    am::json::import_json(doc, input);

    auto bytes = doc.save();
    auto loaded = am::Document::load(bytes);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(am::json::export_json(*loaded), input);
}

TEST(JsonIntegration, json_patch_on_imported_doc) {
    auto doc = am::Document{};
    am::json::import_json(doc, json{{"items", {"a", "b", "c"}}});

    am::json::apply_json_patch(doc, json::parse(R"([
        {"op": "add", "path": "/items/-", "value": "d"},
        {"op": "remove", "path": "/items/0"}
    ])"));

    auto j = am::json::export_json(doc);
    EXPECT_EQ(j["items"], json({"b", "c", "d"}));
}

TEST(JsonIntegration, merge_patch_then_export) {
    auto doc = am::Document{};
    am::json::import_json(doc, json{{"a", 1}, {"b", 2}});
    am::json::apply_merge_patch(doc, json{{"a", 10}, {"c", 3}});

    auto j = am::json::export_json(doc);
    EXPECT_EQ(j["a"], 10);
    EXPECT_EQ(j["b"], 2);
    EXPECT_EQ(j["c"], 3);
}

TEST(JsonIntegration, pointer_on_imported_nested) {
    auto doc = am::Document{};
    am::json::import_json(doc, json{{"a", {{"b", {{"c", 42}}}}}});
    auto val = am::json::get_pointer(doc, "/a/b/c");
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*am::get_scalar<std::int64_t>(*val), 42);
}

TEST(JsonIntegration, flatten_imported_document) {
    auto doc = am::Document{};
    am::json::import_json(doc, json{{"a", 1}, {"b", {{"c", 2}}}});
    auto flat = am::json::flatten(doc);
    EXPECT_EQ(flat["/a"], 1);
    EXPECT_EQ(flat["/b/c"], 2);
}
