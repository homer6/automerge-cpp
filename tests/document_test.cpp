#include <automerge-cpp/automerge.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

using namespace automerge_cpp;

// -- Construction -------------------------------------------------------------

TEST(Document, default_constructed_has_zero_actor_id) {
    const auto doc = Document{};
    EXPECT_TRUE(doc.actor_id().is_zero());
}

TEST(Document, set_and_get_actor_id) {
    const std::uint8_t raw[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    auto doc = Document{};
    doc.set_actor_id(ActorId{raw});
    EXPECT_EQ(doc.actor_id(), ActorId{raw});
}

TEST(Document, root_is_a_map) {
    const auto doc = Document{};
    auto type = doc.object_type(root);
    ASSERT_TRUE(type.has_value());
    EXPECT_EQ(*type, ObjType::map);
}

TEST(Document, root_starts_empty) {
    const auto doc = Document{};
    EXPECT_EQ(doc.length(root), 0u);
    EXPECT_TRUE(doc.keys(root).empty());
    EXPECT_TRUE(doc.values(root).empty());
}

// -- Map put and get ----------------------------------------------------------

TEST(Document, put_and_get_int) {
    auto doc = Document{};
    doc.transact([](auto& tx) {
        tx.put(root, "x", std::int64_t{42});
    });

    auto val = doc.get(root, "x");
    ASSERT_TRUE(val.has_value());
    auto sv = std::get<ScalarValue>(*val);
    EXPECT_EQ(std::get<std::int64_t>(sv), 42);
}

TEST(Document, put_and_get_string) {
    auto doc = Document{};
    doc.transact([](auto& tx) {
        tx.put(root, "name", std::string{"Alice"});
    });

    auto val = doc.get(root, "name");
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(std::get<std::string>(std::get<ScalarValue>(*val)), "Alice");
}

TEST(Document, put_and_get_bool) {
    auto doc = Document{};
    doc.transact([](auto& tx) {
        tx.put(root, "active", true);
    });

    auto val = doc.get(root, "active");
    ASSERT_TRUE(val.has_value());
    EXPECT_TRUE(std::get<bool>(std::get<ScalarValue>(*val)));
}

TEST(Document, put_and_get_double) {
    auto doc = Document{};
    doc.transact([](auto& tx) {
        tx.put(root, "pi", 3.14);
    });

    auto val = doc.get(root, "pi");
    ASSERT_TRUE(val.has_value());
    EXPECT_DOUBLE_EQ(std::get<double>(std::get<ScalarValue>(*val)), 3.14);
}

TEST(Document, put_and_get_null) {
    auto doc = Document{};
    doc.transact([](auto& tx) {
        tx.put(root, "nothing", Null{});
    });

    auto val = doc.get(root, "nothing");
    ASSERT_TRUE(val.has_value());
    EXPECT_TRUE(std::holds_alternative<Null>(std::get<ScalarValue>(*val)));
}

TEST(Document, put_overwrites_previous_value) {
    auto doc = Document{};
    doc.transact([](auto& tx) {
        tx.put(root, "x", std::int64_t{1});
    });
    doc.transact([](auto& tx) {
        tx.put(root, "x", std::int64_t{2});
    });

    auto val = doc.get(root, "x");
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(std::get<std::int64_t>(std::get<ScalarValue>(*val)), 2);
}

TEST(Document, get_missing_key_returns_nullopt) {
    const auto doc = Document{};
    EXPECT_FALSE(doc.get(root, "nonexistent").has_value());
}

// -- Map delete ---------------------------------------------------------------

TEST(Document, delete_key_removes_value) {
    auto doc = Document{};
    doc.transact([](auto& tx) {
        tx.put(root, "x", std::int64_t{42});
    });
    doc.transact([](auto& tx) {
        tx.delete_key(root, "x");
    });

    EXPECT_FALSE(doc.get(root, "x").has_value());
    EXPECT_EQ(doc.length(root), 0u);
}

// -- Keys and values ----------------------------------------------------------

TEST(Document, keys_returns_all_keys_sorted) {
    auto doc = Document{};
    doc.transact([](auto& tx) {
        tx.put(root, "c", std::int64_t{3});
        tx.put(root, "a", std::int64_t{1});
        tx.put(root, "b", std::int64_t{2});
    });

    auto k = doc.keys(root);
    ASSERT_EQ(k.size(), 3u);
    // std::map keeps keys sorted
    EXPECT_EQ(k[0], "a");
    EXPECT_EQ(k[1], "b");
    EXPECT_EQ(k[2], "c");
}

TEST(Document, values_returns_values_in_key_order) {
    auto doc = Document{};
    doc.transact([](auto& tx) {
        tx.put(root, "b", std::int64_t{2});
        tx.put(root, "a", std::int64_t{1});
    });

    auto v = doc.values(root);
    ASSERT_EQ(v.size(), 2u);
    // values follow key order (a, b)
    EXPECT_EQ(std::get<std::int64_t>(std::get<ScalarValue>(v[0])), 1);
    EXPECT_EQ(std::get<std::int64_t>(std::get<ScalarValue>(v[1])), 2);
}

TEST(Document, length_of_map) {
    auto doc = Document{};
    doc.transact([](auto& tx) {
        tx.put(root, "a", std::int64_t{1});
        tx.put(root, "b", std::int64_t{2});
    });
    EXPECT_EQ(doc.length(root), 2u);
}

// -- Nested objects -----------------------------------------------------------

TEST(Document, put_object_creates_nested_map) {
    auto doc = Document{};
    auto nested_id = ObjId{};

    doc.transact([&](auto& tx) {
        nested_id = tx.put_object(root, "meta", ObjType::map);
        tx.put(nested_id, "version", std::int64_t{1});
    });

    auto type = doc.object_type(nested_id);
    ASSERT_TRUE(type.has_value());
    EXPECT_EQ(*type, ObjType::map);

    auto val = doc.get(nested_id, "version");
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(std::get<std::int64_t>(std::get<ScalarValue>(*val)), 1);
}

TEST(Document, put_object_creates_nested_list) {
    auto doc = Document{};
    auto list_id = ObjId{};

    doc.transact([&](auto& tx) {
        list_id = tx.put_object(root, "items", ObjType::list);
        tx.insert(list_id, 0, std::string{"first"});
        tx.insert(list_id, 1, std::string{"second"});
    });

    EXPECT_EQ(doc.length(list_id), 2u);

    auto v0 = doc.get(list_id, std::size_t{0});
    ASSERT_TRUE(v0.has_value());
    EXPECT_EQ(std::get<std::string>(std::get<ScalarValue>(*v0)), "first");

    auto v1 = doc.get(list_id, std::size_t{1});
    ASSERT_TRUE(v1.has_value());
    EXPECT_EQ(std::get<std::string>(std::get<ScalarValue>(*v1)), "second");
}

TEST(Document, deeply_nested_objects) {
    auto doc = Document{};
    auto level1 = ObjId{};
    auto level2 = ObjId{};

    doc.transact([&](auto& tx) {
        level1 = tx.put_object(root, "level1", ObjType::map);
        level2 = tx.put_object(level1, "level2", ObjType::map);
        tx.put(level2, "deep", std::int64_t{99});
    });

    auto val = doc.get(level2, "deep");
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(std::get<std::int64_t>(std::get<ScalarValue>(*val)), 99);
}

// -- List operations ----------------------------------------------------------

TEST(Document, list_insert_and_get) {
    auto doc = Document{};
    auto list_id = ObjId{};

    doc.transact([&](auto& tx) {
        list_id = tx.put_object(root, "list", ObjType::list);
        tx.insert(list_id, 0, std::int64_t{10});
        tx.insert(list_id, 1, std::int64_t{20});
        tx.insert(list_id, 2, std::int64_t{30});
    });

    EXPECT_EQ(doc.length(list_id), 3u);
    EXPECT_EQ(std::get<std::int64_t>(std::get<ScalarValue>(*doc.get(list_id, std::size_t{0}))), 10);
    EXPECT_EQ(std::get<std::int64_t>(std::get<ScalarValue>(*doc.get(list_id, std::size_t{1}))), 20);
    EXPECT_EQ(std::get<std::int64_t>(std::get<ScalarValue>(*doc.get(list_id, std::size_t{2}))), 30);
}

TEST(Document, list_insert_at_beginning) {
    auto doc = Document{};
    auto list_id = ObjId{};

    doc.transact([&](auto& tx) {
        list_id = tx.put_object(root, "list", ObjType::list);
        tx.insert(list_id, 0, std::int64_t{2});
        tx.insert(list_id, 0, std::int64_t{1});  // insert at head
    });

    auto vals = doc.values(list_id);
    ASSERT_EQ(vals.size(), 2u);
    EXPECT_EQ(std::get<std::int64_t>(std::get<ScalarValue>(vals[0])), 1);
    EXPECT_EQ(std::get<std::int64_t>(std::get<ScalarValue>(vals[1])), 2);
}

TEST(Document, list_set_overwrites_element) {
    auto doc = Document{};
    auto list_id = ObjId{};

    doc.transact([&](auto& tx) {
        list_id = tx.put_object(root, "list", ObjType::list);
        tx.insert(list_id, 0, std::int64_t{1});
    });
    doc.transact([&](auto& tx) {
        tx.set(list_id, 0, std::int64_t{99});
    });

    auto val = doc.get(list_id, std::size_t{0});
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(std::get<std::int64_t>(std::get<ScalarValue>(*val)), 99);
}

TEST(Document, list_delete_removes_element) {
    auto doc = Document{};
    auto list_id = ObjId{};

    doc.transact([&](auto& tx) {
        list_id = tx.put_object(root, "list", ObjType::list);
        tx.insert(list_id, 0, std::int64_t{1});
        tx.insert(list_id, 1, std::int64_t{2});
        tx.insert(list_id, 2, std::int64_t{3});
    });
    doc.transact([&](auto& tx) {
        tx.delete_index(list_id, 1);  // delete middle element
    });

    EXPECT_EQ(doc.length(list_id), 2u);
    auto vals = doc.values(list_id);
    EXPECT_EQ(std::get<std::int64_t>(std::get<ScalarValue>(vals[0])), 1);
    EXPECT_EQ(std::get<std::int64_t>(std::get<ScalarValue>(vals[1])), 3);
}

TEST(Document, list_get_out_of_bounds_returns_nullopt) {
    auto doc = Document{};
    auto list_id = ObjId{};

    doc.transact([&](auto& tx) {
        list_id = tx.put_object(root, "list", ObjType::list);
        tx.insert(list_id, 0, std::int64_t{1});
    });

    EXPECT_FALSE(doc.get(list_id, std::size_t{5}).has_value());
}

TEST(Document, insert_object_into_list) {
    auto doc = Document{};
    auto list_id = ObjId{};
    auto nested_map = ObjId{};

    doc.transact([&](auto& tx) {
        list_id = tx.put_object(root, "list", ObjType::list);
        nested_map = tx.insert_object(list_id, 0, ObjType::map);
        tx.put(nested_map, "key", std::string{"value"});
    });

    EXPECT_EQ(doc.length(list_id), 1u);
    auto val = doc.get(nested_map, "key");
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(std::get<std::string>(std::get<ScalarValue>(*val)), "value");
}

// -- Text operations ----------------------------------------------------------

TEST(Document, text_splice_insert) {
    auto doc = Document{};
    auto text_id = ObjId{};

    doc.transact([&](auto& tx) {
        text_id = tx.put_object(root, "content", ObjType::text);
        tx.splice_text(text_id, 0, 0, "Hello");
    });

    EXPECT_EQ(doc.text(text_id), "Hello");
    EXPECT_EQ(doc.length(text_id), 5u);
}

TEST(Document, text_splice_append) {
    auto doc = Document{};
    auto text_id = ObjId{};

    doc.transact([&](auto& tx) {
        text_id = tx.put_object(root, "content", ObjType::text);
        tx.splice_text(text_id, 0, 0, "Hello");
    });
    doc.transact([&](auto& tx) {
        tx.splice_text(text_id, 5, 0, " World");
    });

    EXPECT_EQ(doc.text(text_id), "Hello World");
}

TEST(Document, text_splice_delete) {
    auto doc = Document{};
    auto text_id = ObjId{};

    doc.transact([&](auto& tx) {
        text_id = tx.put_object(root, "content", ObjType::text);
        tx.splice_text(text_id, 0, 0, "Hello World");
    });
    doc.transact([&](auto& tx) {
        tx.splice_text(text_id, 5, 6, "");  // delete " World"
    });

    EXPECT_EQ(doc.text(text_id), "Hello");
}

TEST(Document, text_splice_replace) {
    auto doc = Document{};
    auto text_id = ObjId{};

    doc.transact([&](auto& tx) {
        text_id = tx.put_object(root, "content", ObjType::text);
        tx.splice_text(text_id, 0, 0, "Hello World");
    });
    doc.transact([&](auto& tx) {
        tx.splice_text(text_id, 5, 6, " C++");  // replace " World" with " C++"
    });

    EXPECT_EQ(doc.text(text_id), "Hello C++");
}

// -- Counter operations -------------------------------------------------------

TEST(Document, counter_put_and_increment) {
    auto doc = Document{};
    doc.transact([](auto& tx) {
        tx.put(root, "views", Counter{0});
    });
    doc.transact([](auto& tx) {
        tx.increment(root, "views", 5);
    });

    auto val = doc.get(root, "views");
    ASSERT_TRUE(val.has_value());
    auto counter = std::get<Counter>(std::get<ScalarValue>(*val));
    EXPECT_EQ(counter.value, 5);
}

TEST(Document, counter_multiple_increments) {
    auto doc = Document{};
    doc.transact([](auto& tx) {
        tx.put(root, "count", Counter{10});
    });
    doc.transact([](auto& tx) {
        tx.increment(root, "count", 3);
        tx.increment(root, "count", -1);
    });

    auto val = doc.get(root, "views");
    // "views" doesn't exist, "count" does
    EXPECT_FALSE(val.has_value());

    val = doc.get(root, "count");
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(std::get<Counter>(std::get<ScalarValue>(*val)).value, 12);
}

// -- Copy semantics -----------------------------------------------------------

TEST(Document, copy_creates_independent_document) {
    auto doc1 = Document{};
    doc1.transact([](auto& tx) {
        tx.put(root, "x", std::int64_t{1});
    });

    auto doc2 = doc1;  // copy
    doc2.transact([](auto& tx) {
        tx.put(root, "x", std::int64_t{2});
    });

    // doc1 unchanged
    EXPECT_EQ(std::get<std::int64_t>(std::get<ScalarValue>(*doc1.get(root, "x"))), 1);
    // doc2 has new value
    EXPECT_EQ(std::get<std::int64_t>(std::get<ScalarValue>(*doc2.get(root, "x"))), 2);
}

// -- Multiple transactions ----------------------------------------------------

TEST(Document, multiple_transactions_accumulate) {
    auto doc = Document{};
    auto list_id = ObjId{};

    doc.transact([&](auto& tx) {
        list_id = tx.put_object(root, "items", ObjType::list);
    });
    doc.transact([&](auto& tx) {
        tx.insert(list_id, 0, std::string{"a"});
    });
    doc.transact([&](auto& tx) {
        tx.insert(list_id, 1, std::string{"b"});
    });
    doc.transact([&](auto& tx) {
        tx.insert(list_id, 2, std::string{"c"});
    });

    EXPECT_EQ(doc.length(list_id), 3u);
    EXPECT_EQ(doc.length(root), 1u);
}

// -- get_all (conflicts — single actor always has 1) --------------------------

TEST(Document, get_all_single_actor_returns_one_value) {
    auto doc = Document{};
    doc.transact([](auto& tx) {
        tx.put(root, "key", std::int64_t{42});
    });

    auto all = doc.get_all(root, "key");
    ASSERT_EQ(all.size(), 1u);
    EXPECT_EQ(std::get<std::int64_t>(std::get<ScalarValue>(all[0])), 42);
}

TEST(Document, get_all_missing_key_returns_empty) {
    const auto doc = Document{};
    EXPECT_TRUE(doc.get_all(root, "missing").empty());
}

// -- Phase 3: Fork and Merge --------------------------------------------------

// Helper: create a document with a specific actor ID
auto make_doc(std::uint8_t actor_byte) -> Document {
    auto doc = Document{};
    std::uint8_t raw[16] = {};
    raw[0] = actor_byte;
    doc.set_actor_id(ActorId{raw});
    return doc;
}

// Helper: extract int64 from a Value
auto get_int(const std::optional<Value>& val) -> std::int64_t {
    return std::get<std::int64_t>(std::get<ScalarValue>(*val));
}

// Helper: extract string from a Value
auto get_str(const std::optional<Value>& val) -> std::string {
    return std::get<std::string>(std::get<ScalarValue>(*val));
}

TEST(Document, fork_creates_independent_copy) {
    auto doc = make_doc(1);
    doc.transact([](auto& tx) {
        tx.put(root, "x", std::int64_t{10});
    });

    auto forked = doc.fork();

    // Forked has the same value
    EXPECT_EQ(get_int(forked.get(root, "x")), 10);

    // Mutations are independent
    forked.transact([](auto& tx) {
        tx.put(root, "x", std::int64_t{20});
    });

    EXPECT_EQ(get_int(doc.get(root, "x")), 10);
    EXPECT_EQ(get_int(forked.get(root, "x")), 20);
}

TEST(Document, fork_has_different_actor_id) {
    auto doc = make_doc(1);
    auto forked = doc.fork();
    EXPECT_NE(doc.actor_id(), forked.actor_id());
}

TEST(Document, merge_combines_independent_map_edits) {
    auto doc1 = make_doc(1);
    doc1.transact([](auto& tx) {
        tx.put(root, "x", std::int64_t{1});
    });

    auto doc2 = doc1.fork();
    doc2.transact([](auto& tx) {
        tx.put(root, "y", std::int64_t{2});
    });

    doc1.merge(doc2);

    // doc1 has both keys
    EXPECT_EQ(get_int(doc1.get(root, "x")), 1);
    EXPECT_EQ(get_int(doc1.get(root, "y")), 2);
    EXPECT_EQ(doc1.length(root), 2u);
}

TEST(Document, merge_concurrent_map_edits_creates_conflict) {
    auto doc1 = make_doc(1);
    auto doc2 = doc1.fork();

    // Both edit the same key concurrently
    doc1.transact([](auto& tx) {
        tx.put(root, "x", std::int64_t{10});
    });
    doc2.transact([](auto& tx) {
        tx.put(root, "x", std::int64_t{20});
    });

    doc1.merge(doc2);

    // get_all should return both conflicting values
    auto all = doc1.get_all(root, "x");
    EXPECT_EQ(all.size(), 2u);

    // get returns the winner (highest OpId)
    auto winner = doc1.get(root, "x");
    ASSERT_TRUE(winner.has_value());
    // The winner should be one of the two values
    auto winner_int = get_int(winner);
    EXPECT_TRUE(winner_int == 10 || winner_int == 20);
}

TEST(Document, merge_concurrent_list_inserts_rga_ordering) {
    auto doc1 = make_doc(1);
    doc1.transact([](auto& tx) {
        tx.put_object(root, "list", ObjType::list);
    });

    auto doc2 = doc1.fork();

    // Get the list ObjId from both docs
    auto list1_val = doc1.get(root, "list");
    ASSERT_TRUE(list1_val.has_value());
    auto list1_type = std::get<ObjType>(*list1_val);
    EXPECT_EQ(list1_type, ObjType::list);

    // We need the ObjId — it's the OpId that created the list object
    // Since we know doc1 was created with actor_byte=1 and the list is the first op,
    // we can reconstruct the ObjId. But it's easier to capture it during creation.

    // Let's redo this test with a captured ObjId
    doc1 = make_doc(1);
    auto list_id = ObjId{};
    doc1.transact([&](auto& tx) {
        list_id = tx.put_object(root, "list", ObjType::list);
        tx.insert(list_id, 0, std::string{"A"});
    });

    doc2 = doc1.fork();

    // Both insert at position 1 (after "A") concurrently
    doc1.transact([&](auto& tx) {
        tx.insert(list_id, 1, std::string{"B"});
    });
    doc2.transact([&](auto& tx) {
        tx.insert(list_id, 1, std::string{"C"});
    });

    doc1.merge(doc2);

    // After merge, list should have 3 elements: A, then B and C in deterministic order
    EXPECT_EQ(doc1.length(list_id), 3u);
    auto vals = doc1.values(list_id);
    ASSERT_EQ(vals.size(), 3u);
    // First element is always A
    EXPECT_EQ(std::get<std::string>(std::get<ScalarValue>(vals[0])), "A");
    // B and C are in deterministic RGA order (higher OpId goes first)
    auto second = std::get<std::string>(std::get<ScalarValue>(vals[1]));
    auto third = std::get<std::string>(std::get<ScalarValue>(vals[2]));
    EXPECT_TRUE((second == "B" && third == "C") || (second == "C" && third == "B"));
}

TEST(Document, merge_concurrent_text_edits) {
    auto doc1 = make_doc(1);
    auto text_id = ObjId{};
    doc1.transact([&](auto& tx) {
        text_id = tx.put_object(root, "text", ObjType::text);
        tx.splice_text(text_id, 0, 0, "Hello");
    });

    auto doc2 = doc1.fork();

    // doc1 appends " World"
    doc1.transact([&](auto& tx) {
        tx.splice_text(text_id, 5, 0, " World");
    });

    // doc2 appends "!"
    doc2.transact([&](auto& tx) {
        tx.splice_text(text_id, 5, 0, "!");
    });

    doc1.merge(doc2);

    auto result = doc1.text(text_id);
    // Both edits should be present
    EXPECT_TRUE(result.find("Hello") != std::string::npos);
    EXPECT_TRUE(result.find("World") != std::string::npos);
    EXPECT_TRUE(result.find("!") != std::string::npos);
}

TEST(Document, merge_concurrent_counter_increments) {
    auto doc1 = make_doc(1);
    doc1.transact([](auto& tx) {
        tx.put(root, "count", Counter{0});
    });

    auto doc2 = doc1.fork();

    doc1.transact([](auto& tx) {
        tx.increment(root, "count", 5);
    });
    doc2.transact([](auto& tx) {
        tx.increment(root, "count", 3);
    });

    doc1.merge(doc2);

    auto val = doc1.get(root, "count");
    ASSERT_TRUE(val.has_value());
    auto counter = std::get<Counter>(std::get<ScalarValue>(*val));
    // 0 + 5 (from doc1) + 3 (from doc2 merge) = 8
    EXPECT_EQ(counter.value, 8);
}

TEST(Document, merge_concurrent_delete_and_put) {
    auto doc1 = make_doc(1);
    doc1.transact([](auto& tx) {
        tx.put(root, "x", std::int64_t{1});
    });

    auto doc2 = doc1.fork();

    // doc1 deletes the key
    doc1.transact([](auto& tx) {
        tx.delete_key(root, "x");
    });
    // doc2 puts a new value (concurrent)
    doc2.transact([](auto& tx) {
        tx.put(root, "x", std::int64_t{2});
    });

    doc1.merge(doc2);

    // The concurrent put should survive the delete (it supersedes the old value
    // independently — the delete only removes what it saw)
    auto val = doc1.get(root, "x");
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(get_int(val), 2);
}

TEST(Document, merge_is_commutative) {
    auto doc1 = make_doc(1);
    doc1.transact([](auto& tx) {
        tx.put(root, "shared", std::int64_t{0});
    });

    auto doc2 = doc1.fork();

    doc1.transact([](auto& tx) {
        tx.put(root, "a", std::int64_t{1});
    });
    doc2.transact([](auto& tx) {
        tx.put(root, "b", std::int64_t{2});
    });

    // Merge in both directions
    auto result_ab = Document{doc1};
    result_ab.merge(doc2);

    auto result_ba = Document{doc2};
    result_ba.merge(doc1);

    // Both should have the same keys and values
    auto keys_ab = result_ab.keys(root);
    auto keys_ba = result_ba.keys(root);
    EXPECT_EQ(keys_ab, keys_ba);

    for (const auto& key : keys_ab) {
        EXPECT_EQ(result_ab.get(root, key), result_ba.get(root, key));
    }
}

TEST(Document, merge_is_idempotent) {
    auto doc1 = make_doc(1);
    doc1.transact([](auto& tx) {
        tx.put(root, "x", std::int64_t{42});
    });

    auto doc2 = doc1.fork();
    doc2.transact([](auto& tx) {
        tx.put(root, "y", std::int64_t{99});
    });

    doc1.merge(doc2);
    auto keys_first = doc1.keys(root);
    auto x_first = doc1.get(root, "x");
    auto y_first = doc1.get(root, "y");

    // Merge again — should be a no-op
    doc1.merge(doc2);
    EXPECT_EQ(doc1.keys(root), keys_first);
    EXPECT_EQ(doc1.get(root, "x"), x_first);
    EXPECT_EQ(doc1.get(root, "y"), y_first);
}

TEST(Document, merge_has_identity) {
    auto doc = make_doc(1);
    doc.transact([](auto& tx) {
        tx.put(root, "x", std::int64_t{42});
    });

    auto keys_before = doc.keys(root);
    auto val_before = doc.get(root, "x");

    // Merge with an empty document
    auto empty = Document{};
    doc.merge(empty);

    EXPECT_EQ(doc.keys(root), keys_before);
    EXPECT_EQ(doc.get(root, "x"), val_before);
}

TEST(Document, three_way_merge) {
    auto doc1 = make_doc(1);
    doc1.transact([](auto& tx) {
        tx.put(root, "base", std::int64_t{0});
    });

    auto doc2 = doc1.fork();
    auto doc3 = doc1.fork();
    // Ensure doc3 has a distinct actor from doc2
    std::uint8_t raw3[16] = {};
    raw3[0] = 3;
    doc3.set_actor_id(ActorId{raw3});

    doc1.transact([](auto& tx) {
        tx.put(root, "a", std::int64_t{1});
    });
    doc2.transact([](auto& tx) {
        tx.put(root, "b", std::int64_t{2});
    });
    doc3.transact([](auto& tx) {
        tx.put(root, "c", std::int64_t{3});
    });

    // Merge all into doc1
    doc1.merge(doc2);
    doc1.merge(doc3);

    EXPECT_EQ(doc1.length(root), 4u);  // base, a, b, c
    EXPECT_EQ(get_int(doc1.get(root, "base")), 0);
    EXPECT_EQ(get_int(doc1.get(root, "a")), 1);
    EXPECT_EQ(get_int(doc1.get(root, "b")), 2);
    EXPECT_EQ(get_int(doc1.get(root, "c")), 3);
}

TEST(Document, get_changes_returns_history) {
    auto doc = make_doc(1);
    doc.transact([](auto& tx) {
        tx.put(root, "x", std::int64_t{1});
    });
    doc.transact([](auto& tx) {
        tx.put(root, "y", std::int64_t{2});
    });

    auto changes = doc.get_changes();
    EXPECT_EQ(changes.size(), 2u);
    EXPECT_EQ(changes[0].seq, 1u);
    EXPECT_EQ(changes[1].seq, 2u);
    EXPECT_EQ(changes[0].actor, doc.actor_id());
}

TEST(Document, apply_changes_from_another_doc) {
    auto doc1 = make_doc(1);
    doc1.transact([](auto& tx) {
        tx.put(root, "x", std::int64_t{42});
    });

    auto doc2 = make_doc(2);
    doc2.apply_changes(doc1.get_changes());

    auto val = doc2.get(root, "x");
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(get_int(val), 42);
}

TEST(Document, merge_nested_objects) {
    auto doc1 = make_doc(1);
    auto nested_id = ObjId{};
    doc1.transact([&](auto& tx) {
        nested_id = tx.put_object(root, "config", ObjType::map);
        tx.put(nested_id, "version", std::int64_t{1});
    });

    auto doc2 = doc1.fork();

    // doc1 adds a key to nested map
    doc1.transact([&](auto& tx) {
        tx.put(nested_id, "debug", true);
    });

    // doc2 adds a different key to nested map
    doc2.transact([&](auto& tx) {
        tx.put(nested_id, "verbose", false);
    });

    doc1.merge(doc2);

    // Both nested keys should be present
    EXPECT_EQ(doc1.length(nested_id), 3u);  // version, debug, verbose
    auto debug_val = doc1.get(nested_id, "debug");
    ASSERT_TRUE(debug_val.has_value());
    EXPECT_TRUE(std::get<bool>(std::get<ScalarValue>(*debug_val)));

    auto verbose_val = doc1.get(nested_id, "verbose");
    ASSERT_TRUE(verbose_val.has_value());
    EXPECT_FALSE(std::get<bool>(std::get<ScalarValue>(*verbose_val)));
}

TEST(Document, get_heads_tracks_dag) {
    auto doc = make_doc(1);
    EXPECT_TRUE(doc.get_heads().empty());

    doc.transact([](auto& tx) {
        tx.put(root, "x", std::int64_t{1});
    });
    EXPECT_EQ(doc.get_heads().size(), 1u);

    doc.transact([](auto& tx) {
        tx.put(root, "y", std::int64_t{2});
    });
    // Sequential changes: still 1 head (the latest)
    EXPECT_EQ(doc.get_heads().size(), 1u);
}
