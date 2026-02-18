#include <automerge-cpp/automerge.hpp>

// Internal header for shared pool test
#include <automerge-cpp/thread_pool.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <string>
#include <thread>

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

// -- Phase 4: Binary Serialization --------------------------------------------

TEST(Document, save_and_load_empty_document) {
    auto doc = make_doc(1);
    auto bytes = doc.save();
    EXPECT_FALSE(bytes.empty());

    auto loaded = Document::load(bytes);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->actor_id(), doc.actor_id());
    EXPECT_EQ(loaded->length(root), 0u);
}

TEST(Document, save_and_load_int_value) {
    auto doc = make_doc(1);
    doc.transact([](auto& tx) {
        tx.put(root, "x", std::int64_t{42});
    });

    auto bytes = doc.save();
    auto loaded = Document::load(bytes);
    ASSERT_TRUE(loaded.has_value());

    auto val = loaded->get(root, "x");
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(std::get<std::int64_t>(std::get<ScalarValue>(*val)), 42);
}

TEST(Document, save_and_load_string_value) {
    auto doc = make_doc(1);
    doc.transact([](auto& tx) {
        tx.put(root, "name", std::string{"Alice"});
    });

    auto loaded = Document::load(doc.save());
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(get_str(loaded->get(root, "name")), "Alice");
}

TEST(Document, save_and_load_bool_value) {
    auto doc = make_doc(1);
    doc.transact([](auto& tx) {
        tx.put(root, "flag", true);
    });

    auto loaded = Document::load(doc.save());
    ASSERT_TRUE(loaded.has_value());
    auto val = loaded->get(root, "flag");
    ASSERT_TRUE(val.has_value());
    EXPECT_TRUE(std::get<bool>(std::get<ScalarValue>(*val)));
}

TEST(Document, save_and_load_double_value) {
    auto doc = make_doc(1);
    doc.transact([](auto& tx) {
        tx.put(root, "pi", 3.14159);
    });

    auto loaded = Document::load(doc.save());
    ASSERT_TRUE(loaded.has_value());
    auto val = loaded->get(root, "pi");
    ASSERT_TRUE(val.has_value());
    EXPECT_DOUBLE_EQ(std::get<double>(std::get<ScalarValue>(*val)), 3.14159);
}

TEST(Document, save_and_load_null_value) {
    auto doc = make_doc(1);
    doc.transact([](auto& tx) {
        tx.put(root, "nothing", Null{});
    });

    auto loaded = Document::load(doc.save());
    ASSERT_TRUE(loaded.has_value());
    auto val = loaded->get(root, "nothing");
    ASSERT_TRUE(val.has_value());
    EXPECT_TRUE(std::holds_alternative<Null>(std::get<ScalarValue>(*val)));
}

TEST(Document, save_and_load_uint64_value) {
    auto doc = make_doc(1);
    doc.transact([](auto& tx) {
        tx.put(root, "big", std::uint64_t{18446744073709551615ULL});
    });

    auto loaded = Document::load(doc.save());
    ASSERT_TRUE(loaded.has_value());
    auto val = loaded->get(root, "big");
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(std::get<std::uint64_t>(std::get<ScalarValue>(*val)),
              18446744073709551615ULL);
}

TEST(Document, save_and_load_counter_value) {
    auto doc = make_doc(1);
    doc.transact([](auto& tx) {
        tx.put(root, "count", Counter{100});
    });
    doc.transact([](auto& tx) {
        tx.increment(root, "count", 7);
    });

    auto loaded = Document::load(doc.save());
    ASSERT_TRUE(loaded.has_value());
    auto val = loaded->get(root, "count");
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(std::get<Counter>(std::get<ScalarValue>(*val)).value, 107);
}

TEST(Document, save_and_load_timestamp_value) {
    auto doc = make_doc(1);
    doc.transact([](auto& tx) {
        tx.put(root, "when", Timestamp{1700000000000});
    });

    auto loaded = Document::load(doc.save());
    ASSERT_TRUE(loaded.has_value());
    auto val = loaded->get(root, "when");
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(std::get<Timestamp>(std::get<ScalarValue>(*val)).millis_since_epoch,
              1700000000000);
}

TEST(Document, save_and_load_bytes_value) {
    auto doc = make_doc(1);
    doc.transact([](auto& tx) {
        auto data = Bytes{std::byte{0xDE}, std::byte{0xAD}, std::byte{0xBE}, std::byte{0xEF}};
        tx.put(root, "binary", std::move(data));
    });

    auto loaded = Document::load(doc.save());
    ASSERT_TRUE(loaded.has_value());
    auto val = loaded->get(root, "binary");
    ASSERT_TRUE(val.has_value());
    auto& b = std::get<Bytes>(std::get<ScalarValue>(*val));
    ASSERT_EQ(b.size(), 4u);
    EXPECT_EQ(b[0], std::byte{0xDE});
    EXPECT_EQ(b[3], std::byte{0xEF});
}

TEST(Document, save_and_load_multiple_keys) {
    auto doc = make_doc(1);
    doc.transact([](auto& tx) {
        tx.put(root, "a", std::int64_t{1});
        tx.put(root, "b", std::string{"hello"});
        tx.put(root, "c", true);
        tx.put(root, "d", 2.5);
    });

    auto loaded = Document::load(doc.save());
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->length(root), 4u);
    EXPECT_EQ(get_int(loaded->get(root, "a")), 1);
    EXPECT_EQ(get_str(loaded->get(root, "b")), "hello");
    EXPECT_TRUE(std::get<bool>(std::get<ScalarValue>(*loaded->get(root, "c"))));
    EXPECT_DOUBLE_EQ(std::get<double>(std::get<ScalarValue>(*loaded->get(root, "d"))), 2.5);
}

TEST(Document, save_and_load_nested_map) {
    auto doc = make_doc(1);
    auto nested_id = ObjId{};
    doc.transact([&](auto& tx) {
        nested_id = tx.put_object(root, "config", ObjType::map);
        tx.put(nested_id, "version", std::int64_t{3});
        tx.put(nested_id, "name", std::string{"test"});
    });

    auto loaded = Document::load(doc.save());
    ASSERT_TRUE(loaded.has_value());

    auto config_val = loaded->get(root, "config");
    ASSERT_TRUE(config_val.has_value());
    EXPECT_EQ(std::get<ObjType>(*config_val), ObjType::map);

    // Access nested values
    EXPECT_EQ(loaded->length(nested_id), 2u);
    EXPECT_EQ(get_int(loaded->get(nested_id, "version")), 3);
    EXPECT_EQ(get_str(loaded->get(nested_id, "name")), "test");
}

TEST(Document, save_and_load_list) {
    auto doc = make_doc(1);
    auto list_id = ObjId{};
    doc.transact([&](auto& tx) {
        list_id = tx.put_object(root, "items", ObjType::list);
        tx.insert(list_id, 0, std::int64_t{10});
        tx.insert(list_id, 1, std::int64_t{20});
        tx.insert(list_id, 2, std::int64_t{30});
    });

    auto loaded = Document::load(doc.save());
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->length(list_id), 3u);

    auto vals = loaded->values(list_id);
    ASSERT_EQ(vals.size(), 3u);
    EXPECT_EQ(std::get<std::int64_t>(std::get<ScalarValue>(vals[0])), 10);
    EXPECT_EQ(std::get<std::int64_t>(std::get<ScalarValue>(vals[1])), 20);
    EXPECT_EQ(std::get<std::int64_t>(std::get<ScalarValue>(vals[2])), 30);
}

TEST(Document, save_and_load_text) {
    auto doc = make_doc(1);
    auto text_id = ObjId{};
    doc.transact([&](auto& tx) {
        text_id = tx.put_object(root, "content", ObjType::text);
        tx.splice_text(text_id, 0, 0, "Hello World");
    });

    auto loaded = Document::load(doc.save());
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->text(text_id), "Hello World");
}

TEST(Document, save_and_load_multiple_transactions) {
    auto doc = make_doc(1);
    doc.transact([](auto& tx) {
        tx.put(root, "x", std::int64_t{1});
    });
    doc.transact([](auto& tx) {
        tx.put(root, "x", std::int64_t{2});
    });
    doc.transact([](auto& tx) {
        tx.put(root, "y", std::int64_t{3});
    });

    auto loaded = Document::load(doc.save());
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(get_int(loaded->get(root, "x")), 2);
    EXPECT_EQ(get_int(loaded->get(root, "y")), 3);

    auto changes = loaded->get_changes();
    EXPECT_EQ(changes.size(), 3u);
}

TEST(Document, save_and_load_preserves_actor_id) {
    const std::uint8_t raw[16] = {0xAA, 0xBB, 0xCC, 0xDD, 1, 2, 3, 4,
                                   5, 6, 7, 8, 9, 10, 11, 12};
    auto doc = Document{};
    doc.set_actor_id(ActorId{raw});
    doc.transact([](auto& tx) {
        tx.put(root, "x", std::int64_t{1});
    });

    auto loaded = Document::load(doc.save());
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->actor_id(), doc.actor_id());
}

TEST(Document, save_and_load_preserves_heads) {
    auto doc = make_doc(1);
    doc.transact([](auto& tx) {
        tx.put(root, "x", std::int64_t{1});
    });
    doc.transact([](auto& tx) {
        tx.put(root, "y", std::int64_t{2});
    });

    auto heads_before = doc.get_heads();
    auto loaded = Document::load(doc.save());
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->get_heads(), heads_before);
}

TEST(Document, save_and_load_preserves_change_history) {
    auto doc = make_doc(1);
    doc.transact([](auto& tx) {
        tx.put(root, "a", std::int64_t{1});
    });
    doc.transact([](auto& tx) {
        tx.put(root, "b", std::int64_t{2});
    });

    auto changes_before = doc.get_changes();
    auto loaded = Document::load(doc.save());
    ASSERT_TRUE(loaded.has_value());
    auto changes_after = loaded->get_changes();
    ASSERT_EQ(changes_after.size(), changes_before.size());

    for (std::size_t i = 0; i < changes_before.size(); ++i) {
        EXPECT_EQ(changes_after[i].actor, changes_before[i].actor);
        EXPECT_EQ(changes_after[i].seq, changes_before[i].seq);
        EXPECT_EQ(changes_after[i].start_op, changes_before[i].start_op);
        EXPECT_EQ(changes_after[i].operations.size(),
                  changes_before[i].operations.size());
    }
}

TEST(Document, save_and_load_after_merge) {
    auto doc1 = make_doc(1);
    doc1.transact([](auto& tx) {
        tx.put(root, "x", std::int64_t{1});
    });

    auto doc2 = doc1.fork();
    doc2.transact([](auto& tx) {
        tx.put(root, "y", std::int64_t{2});
    });

    doc1.merge(doc2);

    auto loaded = Document::load(doc1.save());
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->length(root), 2u);
    EXPECT_EQ(get_int(loaded->get(root, "x")), 1);
    EXPECT_EQ(get_int(loaded->get(root, "y")), 2);
}

TEST(Document, save_and_load_can_continue_editing) {
    auto doc = make_doc(1);
    doc.transact([](auto& tx) {
        tx.put(root, "x", std::int64_t{1});
    });

    auto loaded = Document::load(doc.save());
    ASSERT_TRUE(loaded.has_value());

    // Continue editing after load
    loaded->transact([](auto& tx) {
        tx.put(root, "y", std::int64_t{2});
    });

    EXPECT_EQ(loaded->length(root), 2u);
    EXPECT_EQ(get_int(loaded->get(root, "x")), 1);
    EXPECT_EQ(get_int(loaded->get(root, "y")), 2);
}

TEST(Document, save_and_load_can_merge_after_load) {
    auto doc1 = make_doc(1);
    doc1.transact([](auto& tx) {
        tx.put(root, "x", std::int64_t{1});
    });

    auto saved = doc1.save();
    auto loaded = Document::load(saved);
    ASSERT_TRUE(loaded.has_value());

    // Create a separate doc and merge with loaded
    auto doc2 = make_doc(2);
    doc2.transact([](auto& tx) {
        tx.put(root, "y", std::int64_t{2});
    });

    loaded->merge(doc2);
    EXPECT_EQ(loaded->length(root), 2u);
    EXPECT_EQ(get_int(loaded->get(root, "x")), 1);
    EXPECT_EQ(get_int(loaded->get(root, "y")), 2);
}

TEST(Document, save_and_load_deeply_nested) {
    auto doc = make_doc(1);
    auto level1 = ObjId{};
    auto level2 = ObjId{};
    doc.transact([&](auto& tx) {
        level1 = tx.put_object(root, "l1", ObjType::map);
        level2 = tx.put_object(level1, "l2", ObjType::map);
        tx.put(level2, "deep", std::int64_t{42});
    });

    auto loaded = Document::load(doc.save());
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(get_int(loaded->get(level2, "deep")), 42);
}

TEST(Document, save_and_load_with_delete) {
    auto doc = make_doc(1);
    doc.transact([](auto& tx) {
        tx.put(root, "keep", std::int64_t{1});
        tx.put(root, "remove", std::int64_t{2});
    });
    doc.transact([](auto& tx) {
        tx.delete_key(root, "remove");
    });

    auto loaded = Document::load(doc.save());
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->length(root), 1u);
    EXPECT_TRUE(loaded->get(root, "keep").has_value());
    EXPECT_FALSE(loaded->get(root, "remove").has_value());
}

TEST(Document, save_and_load_negative_int) {
    auto doc = make_doc(1);
    doc.transact([](auto& tx) {
        tx.put(root, "neg", std::int64_t{-999});
    });

    auto loaded = Document::load(doc.save());
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(get_int(loaded->get(root, "neg")), -999);
}

// -- Corrupt data handling ----------------------------------------------------

TEST(Document, load_empty_data_returns_nullopt) {
    auto empty = std::vector<std::byte>{};
    EXPECT_FALSE(Document::load(empty).has_value());
}

TEST(Document, load_bad_magic_returns_nullopt) {
    auto data = std::vector<std::byte>{
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x01}};
    EXPECT_FALSE(Document::load(data).has_value());
}

TEST(Document, load_truncated_data_returns_nullopt) {
    auto doc = make_doc(1);
    doc.transact([](auto& tx) {
        tx.put(root, "x", std::int64_t{42});
    });

    auto bytes = doc.save();
    // Truncate to half
    bytes.resize(bytes.size() / 2);
    EXPECT_FALSE(Document::load(bytes).has_value());
}

TEST(Document, load_wrong_version_returns_nullopt) {
    auto doc = make_doc(1);
    auto bytes = doc.save();
    // Corrupt the version byte (position 4)
    bytes[4] = std::byte{0xFF};
    EXPECT_FALSE(Document::load(bytes).has_value());
}

TEST(Document, double_save_load_round_trip) {
    auto doc = make_doc(1);
    doc.transact([](auto& tx) {
        tx.put(root, "x", std::int64_t{42});
        tx.put(root, "s", std::string{"hello"});
    });

    // Save, load, save again, load again
    auto loaded1 = Document::load(doc.save());
    ASSERT_TRUE(loaded1.has_value());
    auto loaded2 = Document::load(loaded1->save());
    ASSERT_TRUE(loaded2.has_value());

    EXPECT_EQ(get_int(loaded2->get(root, "x")), 42);
    EXPECT_EQ(get_str(loaded2->get(root, "s")), "hello");
}

// -- Phase 5: Sync Protocol ---------------------------------------------------

// Helper: run sync protocol until both peers converge (returns message count).
auto sync_docs(Document& a, Document& b) -> int {
    auto sa = SyncState{};
    auto sb = SyncState{};
    int count = 0;
    constexpr int max_rounds = 20;

    for (int round = 0; round < max_rounds; ++round) {
        bool progress = false;

        auto msg_a = a.generate_sync_message(sa);
        if (msg_a) {
            b.receive_sync_message(sb, *msg_a);
            ++count;
            progress = true;
        }

        auto msg_b = b.generate_sync_message(sb);
        if (msg_b) {
            a.receive_sync_message(sa, *msg_b);
            ++count;
            progress = true;
        }

        if (!progress) break;
    }
    return count;
}

TEST(Document, sync_two_fresh_documents) {
    auto doc1 = make_doc(1);
    doc1.transact([](auto& tx) {
        tx.put(root, "x", std::int64_t{1});
    });

    auto doc2 = make_doc(2);
    doc2.transact([](auto& tx) {
        tx.put(root, "y", std::int64_t{2});
    });

    sync_docs(doc1, doc2);

    // Both should have both keys
    EXPECT_EQ(doc1.length(root), 2u);
    EXPECT_EQ(doc2.length(root), 2u);
    EXPECT_EQ(get_int(doc1.get(root, "x")), 1);
    EXPECT_EQ(get_int(doc1.get(root, "y")), 2);
    EXPECT_EQ(get_int(doc2.get(root, "x")), 1);
    EXPECT_EQ(get_int(doc2.get(root, "y")), 2);
}

TEST(Document, sync_one_empty_one_populated) {
    auto doc1 = make_doc(1);
    doc1.transact([](auto& tx) {
        tx.put(root, "a", std::int64_t{10});
        tx.put(root, "b", std::string{"hello"});
    });

    auto doc2 = make_doc(2);

    sync_docs(doc1, doc2);

    EXPECT_EQ(doc2.length(root), 2u);
    EXPECT_EQ(get_int(doc2.get(root, "a")), 10);
    EXPECT_EQ(get_str(doc2.get(root, "b")), "hello");
}

TEST(Document, sync_already_in_sync_produces_few_messages) {
    auto doc1 = make_doc(1);
    doc1.transact([](auto& tx) {
        tx.put(root, "x", std::int64_t{1});
    });

    auto doc2 = doc1.fork();  // exact same state

    auto sa = SyncState{};
    auto sb = SyncState{};

    // First round: exchange heads
    auto msg1 = doc1.generate_sync_message(sa);
    ASSERT_TRUE(msg1.has_value());
    doc2.receive_sync_message(sb, *msg1);

    auto msg2 = doc2.generate_sync_message(sb);
    ASSERT_TRUE(msg2.has_value());
    EXPECT_TRUE(msg2->changes.empty());  // no changes needed
    doc1.receive_sync_message(sa, *msg2);

    // After exchanging heads, both should realize they're synced
    auto msg3 = doc1.generate_sync_message(sa);
    // Should be no more messages or an empty ack
    if (msg3) {
        EXPECT_TRUE(msg3->changes.empty());
    }
}

TEST(Document, sync_after_concurrent_edits) {
    auto doc1 = make_doc(1);
    doc1.transact([](auto& tx) {
        tx.put(root, "base", std::int64_t{0});
    });
    auto doc2 = doc1.fork();

    // Concurrent edits
    doc1.transact([](auto& tx) {
        tx.put(root, "from_1", std::int64_t{1});
    });
    doc2.transact([](auto& tx) {
        tx.put(root, "from_2", std::int64_t{2});
    });

    sync_docs(doc1, doc2);

    // Both should have all three keys
    EXPECT_EQ(doc1.length(root), 3u);
    EXPECT_EQ(doc2.length(root), 3u);
    EXPECT_EQ(get_int(doc1.get(root, "base")), 0);
    EXPECT_EQ(get_int(doc1.get(root, "from_1")), 1);
    EXPECT_EQ(get_int(doc1.get(root, "from_2")), 2);
    EXPECT_EQ(get_int(doc2.get(root, "base")), 0);
    EXPECT_EQ(get_int(doc2.get(root, "from_1")), 1);
    EXPECT_EQ(get_int(doc2.get(root, "from_2")), 2);
}

TEST(Document, sync_with_list_operations) {
    auto doc1 = make_doc(1);
    auto list_id = ObjId{};
    doc1.transact([&](auto& tx) {
        list_id = tx.put_object(root, "items", ObjType::list);
        tx.insert(list_id, 0, std::string{"A"});
    });

    auto doc2 = make_doc(2);

    sync_docs(doc1, doc2);

    EXPECT_EQ(doc2.length(list_id), 1u);
    EXPECT_EQ(get_str(doc2.get(list_id, std::size_t{0})), "A");
}

TEST(Document, sync_with_text_operations) {
    auto doc1 = make_doc(1);
    auto text_id = ObjId{};
    doc1.transact([&](auto& tx) {
        text_id = tx.put_object(root, "content", ObjType::text);
        tx.splice_text(text_id, 0, 0, "Hello");
    });

    auto doc2 = make_doc(2);

    sync_docs(doc1, doc2);

    EXPECT_EQ(doc2.text(text_id), "Hello");
}

TEST(Document, sync_multiple_transactions) {
    auto doc1 = make_doc(1);
    doc1.transact([](auto& tx) {
        tx.put(root, "x", std::int64_t{1});
    });
    doc1.transact([](auto& tx) {
        tx.put(root, "x", std::int64_t{2});
    });
    doc1.transact([](auto& tx) {
        tx.put(root, "y", std::int64_t{3});
    });

    auto doc2 = make_doc(2);

    sync_docs(doc1, doc2);

    EXPECT_EQ(get_int(doc2.get(root, "x")), 2);
    EXPECT_EQ(get_int(doc2.get(root, "y")), 3);
    EXPECT_EQ(doc2.get_changes().size(), 3u);
}

TEST(Document, sync_three_peers) {
    auto doc1 = make_doc(1);
    doc1.transact([](auto& tx) {
        tx.put(root, "a", std::int64_t{1});
    });

    auto doc2 = make_doc(2);
    doc2.transact([](auto& tx) {
        tx.put(root, "b", std::int64_t{2});
    });

    auto doc3 = make_doc(3);
    doc3.transact([](auto& tx) {
        tx.put(root, "c", std::int64_t{3});
    });

    // Sync doc1 <-> doc2
    sync_docs(doc1, doc2);
    // Sync doc2 <-> doc3
    sync_docs(doc2, doc3);
    // Sync doc1 <-> doc3 (should get doc3's changes via doc2)
    sync_docs(doc1, doc3);

    // All three should have all keys
    for (auto* doc : {&doc1, &doc2, &doc3}) {
        EXPECT_EQ(doc->length(root), 3u);
        EXPECT_EQ(get_int(doc->get(root, "a")), 1);
        EXPECT_EQ(get_int(doc->get(root, "b")), 2);
        EXPECT_EQ(get_int(doc->get(root, "c")), 3);
    }
}

TEST(Document, sync_incremental_changes) {
    auto doc1 = make_doc(1);
    auto doc2 = make_doc(2);

    // First sync: initial data
    doc1.transact([](auto& tx) {
        tx.put(root, "v", std::int64_t{1});
    });
    sync_docs(doc1, doc2);
    EXPECT_EQ(get_int(doc2.get(root, "v")), 1);

    // Second sync: incremental update
    doc1.transact([](auto& tx) {
        tx.put(root, "v", std::int64_t{2});
    });
    sync_docs(doc1, doc2);
    EXPECT_EQ(get_int(doc2.get(root, "v")), 2);

    // Third sync: update from other direction
    doc2.transact([](auto& tx) {
        tx.put(root, "w", std::int64_t{99});
    });
    sync_docs(doc1, doc2);
    EXPECT_EQ(get_int(doc1.get(root, "w")), 99);
}

TEST(Document, sync_with_counter_increments) {
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

    sync_docs(doc1, doc2);

    auto val1 = doc1.get(root, "count");
    ASSERT_TRUE(val1.has_value());
    auto c1 = std::get<Counter>(std::get<ScalarValue>(*val1));
    EXPECT_EQ(c1.value, 8);

    auto val2 = doc2.get(root, "count");
    ASSERT_TRUE(val2.has_value());
    auto c2 = std::get<Counter>(std::get<ScalarValue>(*val2));
    EXPECT_EQ(c2.value, 8);
}

TEST(Document, sync_with_nested_objects) {
    auto doc1 = make_doc(1);
    auto nested = ObjId{};
    doc1.transact([&](auto& tx) {
        nested = tx.put_object(root, "config", ObjType::map);
        tx.put(nested, "debug", true);
        tx.put(nested, "version", std::int64_t{3});
    });

    auto doc2 = make_doc(2);
    sync_docs(doc1, doc2);

    EXPECT_EQ(doc2.length(nested), 2u);
    auto debug = doc2.get(nested, "debug");
    ASSERT_TRUE(debug.has_value());
    EXPECT_TRUE(std::get<bool>(std::get<ScalarValue>(*debug)));
    EXPECT_EQ(get_int(doc2.get(nested, "version")), 3);
}

TEST(Document, sync_generates_first_message_from_empty) {
    auto doc = make_doc(1);
    auto state = SyncState{};

    auto msg = doc.generate_sync_message(state);
    ASSERT_TRUE(msg.has_value());  // always send first message
    EXPECT_TRUE(msg->changes.empty());  // no changes for empty doc
}

TEST(Document, sync_state_encode_decode_round_trip) {
    auto state = SyncState{};

    // Simulate setting shared_heads (normally done by sync protocol)
    auto doc = make_doc(1);
    doc.transact([](auto& tx) {
        tx.put(root, "x", std::int64_t{1});
    });

    auto doc2 = make_doc(2);
    sync_docs(doc, doc2);

    // Encode/decode a fresh state
    auto encoded = state.encode();
    EXPECT_FALSE(encoded.empty());
    auto decoded = SyncState::decode(encoded);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->shared_heads(), state.shared_heads());
}

TEST(Document, sync_state_decode_invalid_returns_nullopt) {
    auto bad_data = std::vector<std::byte>{std::byte{0xFF}};
    EXPECT_FALSE(SyncState::decode(bad_data).has_value());
}

TEST(Document, sync_bidirectional_concurrent) {
    auto doc1 = make_doc(1);
    auto doc2 = make_doc(2);

    // Both start with shared base
    doc1.transact([](auto& tx) {
        tx.put(root, "base", std::int64_t{0});
    });
    sync_docs(doc1, doc2);

    // Both make concurrent changes
    doc1.transact([](auto& tx) {
        tx.put(root, "x", std::int64_t{1});
    });
    doc2.transact([](auto& tx) {
        tx.put(root, "y", std::int64_t{2});
    });

    // Sync again
    sync_docs(doc1, doc2);

    // Should converge
    EXPECT_EQ(doc1.keys(root), doc2.keys(root));
    EXPECT_EQ(doc1.length(root), 3u);
    EXPECT_EQ(doc2.length(root), 3u);
}

TEST(Document, sync_with_deletes) {
    auto doc1 = make_doc(1);
    doc1.transact([](auto& tx) {
        tx.put(root, "keep", std::int64_t{1});
        tx.put(root, "remove", std::int64_t{2});
    });

    auto doc2 = make_doc(2);
    sync_docs(doc1, doc2);
    EXPECT_EQ(doc2.length(root), 2u);

    // Delete on doc1
    doc1.transact([](auto& tx) {
        tx.delete_key(root, "remove");
    });
    sync_docs(doc1, doc2);

    EXPECT_EQ(doc2.length(root), 1u);
    EXPECT_TRUE(doc2.get(root, "keep").has_value());
    EXPECT_FALSE(doc2.get(root, "remove").has_value());
}

// =============================================================================
// Phase 6: Patches
// =============================================================================

TEST(Document, transact_with_patches_map_put) {
    auto doc = Document{};
    auto patches = doc.transact_with_patches([](auto& tx) {
        tx.put(root, "name", std::string{"Alice"});
        tx.put(root, "age", std::int64_t{30});
    });

    EXPECT_EQ(patches.size(), 2u);

    // First patch: put "name"
    EXPECT_EQ(patches[0].obj, root);
    EXPECT_EQ(std::get<std::string>(patches[0].key), "name");
    auto* put0 = std::get_if<PatchPut>(&patches[0].action);
    ASSERT_NE(put0, nullptr);
    auto* sv0 = std::get_if<ScalarValue>(&put0->value);
    ASSERT_NE(sv0, nullptr);
    EXPECT_EQ(std::get<std::string>(*sv0), "Alice");
    EXPECT_FALSE(put0->conflict);

    // Second patch: put "age"
    auto* put1 = std::get_if<PatchPut>(&patches[1].action);
    ASSERT_NE(put1, nullptr);
    auto* sv1 = std::get_if<ScalarValue>(&put1->value);
    ASSERT_NE(sv1, nullptr);
    EXPECT_EQ(std::get<std::int64_t>(*sv1), 30);
}

TEST(Document, transact_with_patches_map_delete) {
    auto doc = Document{};
    doc.transact([](auto& tx) {
        tx.put(root, "key", std::int64_t{1});
    });

    auto patches = doc.transact_with_patches([](auto& tx) {
        tx.delete_key(root, "key");
    });

    EXPECT_EQ(patches.size(), 1u);
    auto* del = std::get_if<PatchDelete>(&patches[0].action);
    ASSERT_NE(del, nullptr);
    EXPECT_EQ(del->count, 1u);
    EXPECT_EQ(std::get<std::string>(patches[0].key), "key");
}

TEST(Document, transact_with_patches_list_insert) {
    auto doc = Document{};
    ObjId list_id;
    doc.transact([&](auto& tx) {
        list_id = tx.put_object(root, "items", ObjType::list);
    });

    auto patches = doc.transact_with_patches([&](auto& tx) {
        tx.insert(list_id, 0, std::string{"a"});
        tx.insert(list_id, 1, std::string{"b"});
    });

    EXPECT_EQ(patches.size(), 2u);
    auto* ins0 = std::get_if<PatchInsert>(&patches[0].action);
    ASSERT_NE(ins0, nullptr);
    EXPECT_EQ(ins0->index, 0u);
    auto* sv0 = std::get_if<ScalarValue>(&ins0->value);
    ASSERT_NE(sv0, nullptr);
    EXPECT_EQ(std::get<std::string>(*sv0), "a");

    auto* ins1 = std::get_if<PatchInsert>(&patches[1].action);
    ASSERT_NE(ins1, nullptr);
    EXPECT_EQ(ins1->index, 1u);
}

TEST(Document, transact_with_patches_list_delete) {
    auto doc = Document{};
    ObjId list_id;
    doc.transact([&](auto& tx) {
        list_id = tx.put_object(root, "items", ObjType::list);
        tx.insert(list_id, 0, std::string{"a"});
        tx.insert(list_id, 1, std::string{"b"});
    });

    auto patches = doc.transact_with_patches([&](auto& tx) {
        tx.delete_index(list_id, 0);
    });

    EXPECT_EQ(patches.size(), 1u);
    auto* del = std::get_if<PatchDelete>(&patches[0].action);
    ASSERT_NE(del, nullptr);
    EXPECT_EQ(del->index, 0u);
    EXPECT_EQ(del->count, 1u);
}

TEST(Document, transact_with_patches_splice_text_insert_only) {
    auto doc = Document{};
    ObjId text_id;
    doc.transact([&](auto& tx) {
        text_id = tx.put_object(root, "content", ObjType::text);
    });

    auto patches = doc.transact_with_patches([&](auto& tx) {
        tx.splice_text(text_id, 0, 0, "Hello");
    });

    // Should coalesce into a single PatchSpliceText
    EXPECT_EQ(patches.size(), 1u);
    auto* splice = std::get_if<PatchSpliceText>(&patches[0].action);
    ASSERT_NE(splice, nullptr);
    EXPECT_EQ(splice->index, 0u);
    EXPECT_EQ(splice->delete_count, 0u);
    EXPECT_EQ(splice->text, "Hello");
}

TEST(Document, transact_with_patches_splice_text_replace) {
    auto doc = Document{};
    ObjId text_id;
    doc.transact([&](auto& tx) {
        text_id = tx.put_object(root, "content", ObjType::text);
        tx.splice_text(text_id, 0, 0, "Hello World");
    });

    auto patches = doc.transact_with_patches([&](auto& tx) {
        tx.splice_text(text_id, 5, 6, " C++23");
    });

    // Should coalesce: delete 6 + insert " C++23"
    EXPECT_EQ(patches.size(), 1u);
    auto* splice = std::get_if<PatchSpliceText>(&patches[0].action);
    ASSERT_NE(splice, nullptr);
    EXPECT_EQ(splice->index, 5u);
    EXPECT_EQ(splice->delete_count, 6u);
    EXPECT_EQ(splice->text, " C++23");

    // Verify final text
    EXPECT_EQ(doc.text(text_id), "Hello C++23");
}

TEST(Document, transact_with_patches_counter_increment) {
    auto doc = Document{};
    doc.transact([](auto& tx) {
        tx.put(root, "views", Counter{0});
    });

    auto patches = doc.transact_with_patches([](auto& tx) {
        tx.increment(root, "views", 5);
    });

    EXPECT_EQ(patches.size(), 1u);
    auto* inc = std::get_if<PatchIncrement>(&patches[0].action);
    ASSERT_NE(inc, nullptr);
    EXPECT_EQ(inc->delta, 5);
}

TEST(Document, transact_with_patches_make_object) {
    auto doc = Document{};
    auto patches = doc.transact_with_patches([](auto& tx) {
        tx.put_object(root, "nested", ObjType::map);
    });

    EXPECT_EQ(patches.size(), 1u);
    auto* put = std::get_if<PatchPut>(&patches[0].action);
    ASSERT_NE(put, nullptr);
    auto* obj_type = std::get_if<ObjType>(&put->value);
    ASSERT_NE(obj_type, nullptr);
    EXPECT_EQ(*obj_type, ObjType::map);
}

TEST(Document, transact_with_patches_empty_transaction) {
    auto doc = Document{};
    auto patches = doc.transact_with_patches([](auto& /*tx*/) {
        // no ops
    });

    EXPECT_TRUE(patches.empty());
}

// =============================================================================
// Phase 6: Historical Reads (Time Travel)
// =============================================================================

TEST(Document, get_at_reads_past_map_value) {
    auto doc = Document{};
    doc.transact([](auto& tx) {
        tx.put(root, "x", std::int64_t{1});
    });

    auto heads_v1 = doc.get_heads();

    doc.transact([](auto& tx) {
        tx.put(root, "x", std::int64_t{2});
    });

    // Current value is 2
    auto current = doc.get(root, "x");
    ASSERT_TRUE(current.has_value());
    EXPECT_EQ(std::get<std::int64_t>(std::get<ScalarValue>(*current)), 2);

    // Value at v1 was 1
    auto past = doc.get_at(root, "x", heads_v1);
    ASSERT_TRUE(past.has_value());
    EXPECT_EQ(std::get<std::int64_t>(std::get<ScalarValue>(*past)), 1);
}

TEST(Document, get_at_reads_past_list_value) {
    auto doc = Document{};
    ObjId list_id;
    doc.transact([&](auto& tx) {
        list_id = tx.put_object(root, "items", ObjType::list);
        tx.insert(list_id, 0, std::string{"first"});
    });

    auto heads_v1 = doc.get_heads();

    doc.transact([&](auto& tx) {
        tx.insert(list_id, 1, std::string{"second"});
    });

    // Current length is 2
    EXPECT_EQ(doc.length(list_id), 2u);

    // At v1, list index 0 had "first"
    auto past = doc.get_at(list_id, std::size_t{0}, heads_v1);
    ASSERT_TRUE(past.has_value());
    EXPECT_EQ(std::get<std::string>(std::get<ScalarValue>(*past)), "first");
}

TEST(Document, keys_at_reads_past_keys) {
    auto doc = Document{};
    doc.transact([](auto& tx) {
        tx.put(root, "a", std::int64_t{1});
    });

    auto heads_v1 = doc.get_heads();

    doc.transact([](auto& tx) {
        tx.put(root, "b", std::int64_t{2});
    });

    // Current keys: a, b
    EXPECT_EQ(doc.keys(root).size(), 2u);

    // At v1: only "a"
    auto past_keys = doc.keys_at(root, heads_v1);
    EXPECT_EQ(past_keys.size(), 1u);
    EXPECT_EQ(past_keys[0], "a");
}

TEST(Document, values_at_reads_past_values) {
    auto doc = Document{};
    doc.transact([](auto& tx) {
        tx.put(root, "x", std::int64_t{10});
    });

    auto heads_v1 = doc.get_heads();

    doc.transact([](auto& tx) {
        tx.put(root, "x", std::int64_t{20});
    });

    auto past_vals = doc.values_at(root, heads_v1);
    EXPECT_EQ(past_vals.size(), 1u);
    EXPECT_EQ(std::get<std::int64_t>(std::get<ScalarValue>(past_vals[0])), 10);
}

TEST(Document, length_at_reads_past_length) {
    auto doc = Document{};
    ObjId list_id;
    doc.transact([&](auto& tx) {
        list_id = tx.put_object(root, "items", ObjType::list);
        tx.insert(list_id, 0, std::string{"a"});
    });

    auto heads_v1 = doc.get_heads();

    doc.transact([&](auto& tx) {
        tx.insert(list_id, 1, std::string{"b"});
        tx.insert(list_id, 2, std::string{"c"});
    });

    EXPECT_EQ(doc.length(list_id), 3u);
    EXPECT_EQ(doc.length_at(list_id, heads_v1), 1u);
}

TEST(Document, text_at_reads_past_text) {
    auto doc = Document{};
    ObjId text_id;
    doc.transact([&](auto& tx) {
        text_id = tx.put_object(root, "content", ObjType::text);
        tx.splice_text(text_id, 0, 0, "Hello");
    });

    auto heads_v1 = doc.get_heads();

    doc.transact([&](auto& tx) {
        tx.splice_text(text_id, 5, 0, " World");
    });

    EXPECT_EQ(doc.text(text_id), "Hello World");
    EXPECT_EQ(doc.text_at(text_id, heads_v1), "Hello");
}

TEST(Document, get_at_missing_key_returns_nullopt) {
    auto doc = Document{};
    doc.transact([](auto& tx) {
        tx.put(root, "x", std::int64_t{1});
    });

    auto heads = doc.get_heads();

    auto result = doc.get_at(root, "nonexistent", heads);
    EXPECT_FALSE(result.has_value());
}

TEST(Document, get_at_deleted_key_returns_nullopt) {
    auto doc = Document{};
    doc.transact([](auto& tx) {
        tx.put(root, "x", std::int64_t{1});
    });

    doc.transact([](auto& tx) {
        tx.delete_key(root, "x");
    });

    auto heads_after_delete = doc.get_heads();

    auto result = doc.get_at(root, "x", heads_after_delete);
    EXPECT_FALSE(result.has_value());
}

TEST(Document, historical_read_multiple_versions) {
    auto doc = Document{};
    doc.transact([](auto& tx) {
        tx.put(root, "x", std::int64_t{1});
    });
    auto heads_v1 = doc.get_heads();

    doc.transact([](auto& tx) {
        tx.put(root, "x", std::int64_t{2});
    });
    auto heads_v2 = doc.get_heads();

    doc.transact([](auto& tx) {
        tx.put(root, "x", std::int64_t{3});
    });

    // Read each version
    auto v1 = doc.get_at(root, "x", heads_v1);
    ASSERT_TRUE(v1.has_value());
    EXPECT_EQ(std::get<std::int64_t>(std::get<ScalarValue>(*v1)), 1);

    auto v2 = doc.get_at(root, "x", heads_v2);
    ASSERT_TRUE(v2.has_value());
    EXPECT_EQ(std::get<std::int64_t>(std::get<ScalarValue>(*v2)), 2);

    auto current = doc.get(root, "x");
    ASSERT_TRUE(current.has_value());
    EXPECT_EQ(std::get<std::int64_t>(std::get<ScalarValue>(*current)), 3);
}

// =============================================================================
// Phase 6: Cursors
// =============================================================================

TEST(Document, cursor_and_resolve_basic) {
    auto doc = Document{};
    ObjId list_id;
    doc.transact([&](auto& tx) {
        list_id = tx.put_object(root, "items", ObjType::list);
        tx.insert(list_id, 0, std::string{"a"});
        tx.insert(list_id, 1, std::string{"b"});
        tx.insert(list_id, 2, std::string{"c"});
    });

    // Create cursor at index 1 ("b")
    auto cur = doc.cursor(list_id, 1);
    ASSERT_TRUE(cur.has_value());

    // Resolve cursor — should be at index 1
    auto idx = doc.resolve_cursor(list_id, *cur);
    ASSERT_TRUE(idx.has_value());
    EXPECT_EQ(*idx, 1u);
}

TEST(Document, cursor_survives_insert_before) {
    auto doc = Document{};
    ObjId list_id;
    doc.transact([&](auto& tx) {
        list_id = tx.put_object(root, "items", ObjType::list);
        tx.insert(list_id, 0, std::string{"a"});
        tx.insert(list_id, 1, std::string{"b"});
        tx.insert(list_id, 2, std::string{"c"});
    });

    // Cursor at index 1 ("b")
    auto cur = doc.cursor(list_id, 1);
    ASSERT_TRUE(cur.has_value());

    // Insert before "b"
    doc.transact([&](auto& tx) {
        tx.insert(list_id, 0, std::string{"z"});
    });

    // "b" is now at index 2
    auto idx = doc.resolve_cursor(list_id, *cur);
    ASSERT_TRUE(idx.has_value());
    EXPECT_EQ(*idx, 2u);

    // Verify the element at the cursor position is still "b"
    auto val = doc.get(list_id, *idx);
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(std::get<std::string>(std::get<ScalarValue>(*val)), "b");
}

TEST(Document, cursor_survives_insert_after) {
    auto doc = Document{};
    ObjId list_id;
    doc.transact([&](auto& tx) {
        list_id = tx.put_object(root, "items", ObjType::list);
        tx.insert(list_id, 0, std::string{"a"});
        tx.insert(list_id, 1, std::string{"b"});
        tx.insert(list_id, 2, std::string{"c"});
    });

    // Cursor at index 1 ("b")
    auto cur = doc.cursor(list_id, 1);
    ASSERT_TRUE(cur.has_value());

    // Insert after "b"
    doc.transact([&](auto& tx) {
        tx.insert(list_id, 2, std::string{"z"});
    });

    // "b" is still at index 1
    auto idx = doc.resolve_cursor(list_id, *cur);
    ASSERT_TRUE(idx.has_value());
    EXPECT_EQ(*idx, 1u);
}

TEST(Document, cursor_on_deleted_element_returns_nullopt) {
    auto doc = Document{};
    ObjId list_id;
    doc.transact([&](auto& tx) {
        list_id = tx.put_object(root, "items", ObjType::list);
        tx.insert(list_id, 0, std::string{"a"});
        tx.insert(list_id, 1, std::string{"b"});
    });

    auto cur = doc.cursor(list_id, 1);
    ASSERT_TRUE(cur.has_value());

    // Delete "b"
    doc.transact([&](auto& tx) {
        tx.delete_index(list_id, 1);
    });

    // Cursor should resolve to nullopt (element deleted)
    auto idx = doc.resolve_cursor(list_id, *cur);
    EXPECT_FALSE(idx.has_value());
}

TEST(Document, cursor_out_of_bounds_returns_nullopt) {
    auto doc = Document{};
    ObjId list_id;
    doc.transact([&](auto& tx) {
        list_id = tx.put_object(root, "items", ObjType::list);
        tx.insert(list_id, 0, std::string{"a"});
    });

    auto cur = doc.cursor(list_id, 5);  // out of bounds
    EXPECT_FALSE(cur.has_value());
}

TEST(Document, cursor_on_text) {
    auto doc = Document{};
    ObjId text_id;
    doc.transact([&](auto& tx) {
        text_id = tx.put_object(root, "content", ObjType::text);
        tx.splice_text(text_id, 0, 0, "Hello");
    });

    // Cursor at position 2 ('l')
    auto cur = doc.cursor(text_id, 2);
    ASSERT_TRUE(cur.has_value());

    // Insert at beginning
    doc.transact([&](auto& tx) {
        tx.splice_text(text_id, 0, 0, ">>> ");
    });

    // 'l' should now be at position 6
    auto idx = doc.resolve_cursor(text_id, *cur);
    ASSERT_TRUE(idx.has_value());
    EXPECT_EQ(*idx, 6u);

    EXPECT_EQ(doc.text(text_id), ">>> Hello");
}

TEST(Document, cursor_survives_merge) {
    auto doc1 = Document{};
    const std::uint8_t raw1[16] = {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    doc1.set_actor_id(ActorId{raw1});

    ObjId list_id;
    doc1.transact([&](auto& tx) {
        list_id = tx.put_object(root, "items", ObjType::list);
        tx.insert(list_id, 0, std::string{"a"});
        tx.insert(list_id, 1, std::string{"b"});
        tx.insert(list_id, 2, std::string{"c"});
    });

    auto doc2 = doc1.fork();

    // Create cursor at "b" (index 1) on doc1
    auto cur = doc1.cursor(list_id, 1);
    ASSERT_TRUE(cur.has_value());

    // doc2 inserts at the beginning
    doc2.transact([&](auto& tx) {
        tx.insert(list_id, 0, std::string{"x"});
        tx.insert(list_id, 1, std::string{"y"});
    });

    doc1.merge(doc2);

    // After merge, "b" should have shifted to accommodate x, y
    auto idx = doc1.resolve_cursor(list_id, *cur);
    ASSERT_TRUE(idx.has_value());

    // Verify cursor still points to "b"
    auto val = doc1.get(list_id, *idx);
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(std::get<std::string>(std::get<ScalarValue>(*val)), "b");
}

// =============================================================================
// Phase 6: Rich Text Marks
// =============================================================================

TEST(Document, mark_basic_apply_and_query) {
    auto doc = Document{};
    ObjId text_id;
    doc.transact([&](auto& tx) {
        text_id = tx.put_object(root, "content", ObjType::text);
        tx.splice_text(text_id, 0, 0, "Hello World");
    });

    // Mark "Hello" (indices 0..5) as bold
    doc.transact([&](auto& tx) {
        tx.mark(text_id, 0, 5, "bold", true);
    });

    auto marks = doc.marks(text_id);
    ASSERT_EQ(marks.size(), 1u);
    EXPECT_EQ(marks[0].start, 0u);
    EXPECT_EQ(marks[0].end, 5u);
    EXPECT_EQ(marks[0].name, "bold");
    EXPECT_TRUE(std::get<bool>(marks[0].value));
}

TEST(Document, mark_multiple_non_overlapping) {
    auto doc = Document{};
    ObjId text_id;
    doc.transact([&](auto& tx) {
        text_id = tx.put_object(root, "content", ObjType::text);
        tx.splice_text(text_id, 0, 0, "Hello World");
    });

    doc.transact([&](auto& tx) {
        tx.mark(text_id, 0, 5, "bold", true);
        tx.mark(text_id, 6, 11, "italic", true);
    });

    auto marks = doc.marks(text_id);
    ASSERT_EQ(marks.size(), 2u);

    // Sort by start to make assertions deterministic
    std::ranges::sort(marks, {}, &Mark::start);

    EXPECT_EQ(marks[0].start, 0u);
    EXPECT_EQ(marks[0].end, 5u);
    EXPECT_EQ(marks[0].name, "bold");

    EXPECT_EQ(marks[1].start, 6u);
    EXPECT_EQ(marks[1].end, 11u);
    EXPECT_EQ(marks[1].name, "italic");
}

TEST(Document, mark_overlapping_ranges) {
    auto doc = Document{};
    ObjId text_id;
    doc.transact([&](auto& tx) {
        text_id = tx.put_object(root, "content", ObjType::text);
        tx.splice_text(text_id, 0, 0, "Hello World");
    });

    doc.transact([&](auto& tx) {
        tx.mark(text_id, 0, 8, "bold", true);
        tx.mark(text_id, 3, 11, "italic", true);
    });

    auto marks = doc.marks(text_id);
    ASSERT_EQ(marks.size(), 2u);

    // Sort by name for determinism
    std::ranges::sort(marks, {}, &Mark::name);

    EXPECT_EQ(marks[0].name, "bold");
    EXPECT_EQ(marks[0].start, 0u);
    EXPECT_EQ(marks[0].end, 8u);

    EXPECT_EQ(marks[1].name, "italic");
    EXPECT_EQ(marks[1].start, 3u);
    EXPECT_EQ(marks[1].end, 11u);
}

TEST(Document, mark_with_string_value) {
    auto doc = Document{};
    ObjId text_id;
    doc.transact([&](auto& tx) {
        text_id = tx.put_object(root, "content", ObjType::text);
        tx.splice_text(text_id, 0, 0, "click here");
    });

    doc.transact([&](auto& tx) {
        tx.mark(text_id, 0, 10, "link", std::string{"https://example.com"});
    });

    auto marks = doc.marks(text_id);
    ASSERT_EQ(marks.size(), 1u);
    EXPECT_EQ(marks[0].name, "link");
    EXPECT_EQ(std::get<std::string>(marks[0].value), "https://example.com");
}

TEST(Document, mark_survives_insert_before_range) {
    auto doc = Document{};
    ObjId text_id;
    doc.transact([&](auto& tx) {
        text_id = tx.put_object(root, "content", ObjType::text);
        tx.splice_text(text_id, 0, 0, "Hello");
    });

    // Mark all of "Hello" as bold
    doc.transact([&](auto& tx) {
        tx.mark(text_id, 0, 5, "bold", true);
    });

    // Insert ">>> " before "Hello"
    doc.transact([&](auto& tx) {
        tx.splice_text(text_id, 0, 0, ">>> ");
    });

    EXPECT_EQ(doc.text(text_id), ">>> Hello");

    auto marks = doc.marks(text_id);
    ASSERT_EQ(marks.size(), 1u);
    // Mark should shift to cover indices 4..9
    EXPECT_EQ(marks[0].start, 4u);
    EXPECT_EQ(marks[0].end, 9u);
    EXPECT_EQ(marks[0].name, "bold");
}

TEST(Document, mark_survives_insert_within_range) {
    auto doc = Document{};
    ObjId text_id;
    doc.transact([&](auto& tx) {
        text_id = tx.put_object(root, "content", ObjType::text);
        tx.splice_text(text_id, 0, 0, "abcde");
    });

    // Mark all as bold (indices 0..5)
    doc.transact([&](auto& tx) {
        tx.mark(text_id, 0, 5, "bold", true);
    });

    // Insert "XY" after position 2 (between c and d)
    doc.transact([&](auto& tx) {
        tx.splice_text(text_id, 3, 0, "XY");
    });

    EXPECT_EQ(doc.text(text_id), "abcXYde");

    auto marks = doc.marks(text_id);
    ASSERT_EQ(marks.size(), 1u);
    // Mark should expand: start element 'a' is at 0, end element 'e' is at 6
    EXPECT_EQ(marks[0].start, 0u);
    EXPECT_EQ(marks[0].end, 7u);
    EXPECT_EQ(marks[0].name, "bold");
}

TEST(Document, mark_no_marks_returns_empty) {
    auto doc = Document{};
    ObjId text_id;
    doc.transact([&](auto& tx) {
        text_id = tx.put_object(root, "content", ObjType::text);
        tx.splice_text(text_id, 0, 0, "plain text");
    });

    auto marks = doc.marks(text_id);
    EXPECT_TRUE(marks.empty());
}

TEST(Document, mark_survives_merge) {
    auto doc1 = Document{};
    const std::uint8_t raw1[16] = {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    doc1.set_actor_id(ActorId{raw1});

    ObjId text_id;
    doc1.transact([&](auto& tx) {
        text_id = tx.put_object(root, "content", ObjType::text);
        tx.splice_text(text_id, 0, 0, "Hello World");
    });

    auto doc2 = doc1.fork();

    // doc1 marks "Hello" as bold
    doc1.transact([&](auto& tx) {
        tx.mark(text_id, 0, 5, "bold", true);
    });

    // doc2 marks "World" as italic
    doc2.transact([&](auto& tx) {
        tx.mark(text_id, 6, 11, "italic", true);
    });

    doc1.merge(doc2);

    auto marks = doc1.marks(text_id);
    ASSERT_EQ(marks.size(), 2u);

    std::ranges::sort(marks, {}, &Mark::name);
    EXPECT_EQ(marks[0].name, "bold");
    EXPECT_EQ(marks[0].start, 0u);
    EXPECT_EQ(marks[0].end, 5u);

    EXPECT_EQ(marks[1].name, "italic");
    EXPECT_EQ(marks[1].start, 6u);
    EXPECT_EQ(marks[1].end, 11u);
}

TEST(Document, marks_at_historical_read) {
    auto doc = Document{};
    ObjId text_id;
    doc.transact([&](auto& tx) {
        text_id = tx.put_object(root, "content", ObjType::text);
        tx.splice_text(text_id, 0, 0, "Hello World");
    });

    doc.transact([&](auto& tx) {
        tx.mark(text_id, 0, 5, "bold", true);
    });

    auto heads_v1 = doc.get_heads();

    // Add another mark after snapshot
    doc.transact([&](auto& tx) {
        tx.mark(text_id, 6, 11, "italic", true);
    });

    // Current state: 2 marks
    EXPECT_EQ(doc.marks(text_id).size(), 2u);

    // At v1: only 1 mark
    auto past_marks = doc.marks_at(text_id, heads_v1);
    ASSERT_EQ(past_marks.size(), 1u);
    EXPECT_EQ(past_marks[0].name, "bold");
    EXPECT_EQ(past_marks[0].start, 0u);
    EXPECT_EQ(past_marks[0].end, 5u);
}

TEST(Document, mark_save_and_load_round_trip) {
    auto doc = make_doc(1);
    ObjId text_id;
    doc.transact([&](auto& tx) {
        text_id = tx.put_object(root, "content", ObjType::text);
        tx.splice_text(text_id, 0, 0, "Hello World");
    });

    doc.transact([&](auto& tx) {
        tx.mark(text_id, 0, 5, "bold", true);
        tx.mark(text_id, 6, 11, "link", std::string{"https://example.com"});
    });

    auto loaded = Document::load(doc.save());
    ASSERT_TRUE(loaded.has_value());

    EXPECT_EQ(loaded->text(text_id), "Hello World");

    auto marks = loaded->marks(text_id);
    ASSERT_EQ(marks.size(), 2u);

    std::ranges::sort(marks, {}, &Mark::name);
    EXPECT_EQ(marks[0].name, "bold");
    EXPECT_EQ(marks[0].start, 0u);
    EXPECT_EQ(marks[0].end, 5u);
    EXPECT_TRUE(std::get<bool>(marks[0].value));

    EXPECT_EQ(marks[1].name, "link");
    EXPECT_EQ(marks[1].start, 6u);
    EXPECT_EQ(marks[1].end, 11u);
    EXPECT_EQ(std::get<std::string>(marks[1].value), "https://example.com");
}

TEST(Document, mark_sync_round_trip) {
    auto doc1 = make_doc(1);
    ObjId text_id;
    doc1.transact([&](auto& tx) {
        text_id = tx.put_object(root, "content", ObjType::text);
        tx.splice_text(text_id, 0, 0, "Hello World");
    });

    doc1.transact([&](auto& tx) {
        tx.mark(text_id, 0, 5, "bold", true);
    });

    auto doc2 = make_doc(2);
    sync_docs(doc1, doc2);

    EXPECT_EQ(doc2.text(text_id), "Hello World");

    auto marks = doc2.marks(text_id);
    ASSERT_EQ(marks.size(), 1u);
    EXPECT_EQ(marks[0].name, "bold");
    EXPECT_EQ(marks[0].start, 0u);
    EXPECT_EQ(marks[0].end, 5u);
}

TEST(Document, mark_transact_with_patches_mark_only_transaction) {
    auto doc = Document{};
    ObjId text_id;
    doc.transact([&](auto& tx) {
        text_id = tx.put_object(root, "content", ObjType::text);
        tx.splice_text(text_id, 0, 0, "Hello");
    });

    // Mark-only transaction: marks are metadata that don't produce
    // element-level patches (put/insert/delete). The mark is stored
    // and queryable via marks() instead.
    auto patches = doc.transact_with_patches([&](auto& tx) {
        tx.mark(text_id, 0, 5, "bold", true);
    });

    // Marks don't produce element-level patches
    EXPECT_TRUE(patches.empty());

    // But the mark is queryable
    auto marks = doc.marks(text_id);
    ASSERT_EQ(marks.size(), 1u);
    EXPECT_EQ(marks[0].name, "bold");
}

// -- Thread safety and parallelism (11C) --------------------------------------

TEST(Document, constructor_with_thread_count) {
    auto doc1 = Document{};          // default: hardware_concurrency()
    auto doc2 = Document{4u};        // explicit 4 threads
    auto doc3 = Document{1u};        // sequential, no pool
    auto doc4 = Document{0u};        // 0 = auto

    // All should work identically
    for (auto* doc : {&doc1, &doc2, &doc3, &doc4}) {
        doc->transact([](auto& tx) {
            tx.put(root, "key", std::string{"value"});
        });
        auto val = doc->get(root, "key");
        ASSERT_TRUE(val.has_value());
    }
}

TEST(Document, constructor_with_shared_pool) {
    auto pool = std::make_shared<automerge_cpp::thread_pool>(2);
    auto doc1 = Document{pool};
    auto doc2 = Document{pool};

    doc1.transact([](auto& tx) {
        tx.put(root, "from", std::string{"doc1"});
    });
    doc2.transact([](auto& tx) {
        tx.put(root, "from", std::string{"doc2"});
    });

    EXPECT_EQ(doc1.get_thread_pool(), doc2.get_thread_pool());

    auto v1 = doc1.get(root, "from");
    auto v2 = doc2.get(root, "from");
    ASSERT_TRUE(v1.has_value());
    ASSERT_TRUE(v2.has_value());
}

TEST(Document, fork_shares_thread_pool) {
    auto doc = Document{4u};
    doc.transact([](auto& tx) {
        tx.put(root, "key", std::string{"value"});
    });
    auto forked = doc.fork();
    EXPECT_EQ(doc.get_thread_pool(), forked.get_thread_pool());
}

TEST(Document, concurrent_reads_are_safe) {
    auto doc = Document{};
    doc.transact([](auto& tx) {
        for (int i = 0; i < 100; ++i) {
            tx.put(root, "key_" + std::to_string(i),
                   std::string{"val_" + std::to_string(i)});
        }
    });

    // Launch multiple threads doing concurrent reads
    auto threads = std::vector<std::jthread>{};
    auto errors = std::atomic<int>{0};
    for (int t = 0; t < 8; ++t) {
        threads.emplace_back([&doc, &errors, t]() {
            for (int i = 0; i < 100; ++i) {
                auto key = "key_" + std::to_string((t * 13 + i) % 100);
                auto val = doc.get(root, key);
                if (!val.has_value()) {
                    errors.fetch_add(1, std::memory_order_relaxed);
                }
                auto keys = doc.keys(root);
                if (keys.size() != 100) {
                    errors.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }
    threads.clear();  // join all
    EXPECT_EQ(errors.load(), 0);
}

TEST(Document, fork_merge_batch_put) {
    // Simulate parallel batch put via fork/merge
    auto doc = Document{1u};  // sequential base
    doc.transact([](auto& tx) {
        tx.put(root, "existing", std::string{"keep_me"});
    });

    // Fork N copies, each puts a partition of keys, merge back
    constexpr int num_forks = 4;
    constexpr int keys_per_fork = 25;

    auto forks = std::vector<Document>{};
    forks.reserve(num_forks);
    for (int f = 0; f < num_forks; ++f) {
        forks.push_back(doc.fork());
    }

    // Each fork puts its partition
    for (int f = 0; f < num_forks; ++f) {
        forks[f].transact([f](auto& tx) {
            for (int i = 0; i < keys_per_fork; ++i) {
                auto key = "batch_" + std::to_string(f * keys_per_fork + i);
                tx.put(root, key, std::int64_t{f * keys_per_fork + i});
            }
        });
    }

    // Merge all forks back
    for (auto& f : forks) {
        doc.merge(f);
    }

    // Verify all keys present
    auto val = doc.get(root, "existing");
    ASSERT_TRUE(val.has_value());

    for (int i = 0; i < num_forks * keys_per_fork; ++i) {
        auto key = "batch_" + std::to_string(i);
        auto v = doc.get(root, key);
        ASSERT_TRUE(v.has_value()) << "Missing key: " << key;
    }

    EXPECT_EQ(doc.length(root),
              static_cast<std::size_t>(1 + num_forks * keys_per_fork));
}

TEST(Document, threaded_fork_merge_batch_put) {
    // Same as above but forks execute on separate threads
    auto doc = Document{};
    doc.transact([](auto& tx) {
        tx.put(root, "base", std::string{"value"});
    });

    constexpr int num_forks = 8;
    constexpr int keys_per_fork = 50;

    auto forks = std::vector<Document>{};
    forks.reserve(num_forks);
    for (int f = 0; f < num_forks; ++f) {
        forks.push_back(doc.fork());
    }

    // Execute fork mutations in parallel
    auto threads = std::vector<std::jthread>{};
    for (int f = 0; f < num_forks; ++f) {
        threads.emplace_back([&forks, f]() {
            forks[f].transact([f](auto& tx) {
                for (int i = 0; i < keys_per_fork; ++i) {
                    auto key = "p" + std::to_string(f) + "_" + std::to_string(i);
                    tx.put(root, key, std::int64_t{f * 1000 + i});
                }
            });
        });
    }
    threads.clear();  // join all

    // Merge sequentially (merge order doesn't matter — CRDT guarantee)
    for (auto& f : forks) {
        doc.merge(f);
    }

    // Verify: base key + all fork keys
    EXPECT_TRUE(doc.get(root, "base").has_value());
    for (int f = 0; f < num_forks; ++f) {
        for (int i = 0; i < keys_per_fork; ++i) {
            auto key = "p" + std::to_string(f) + "_" + std::to_string(i);
            auto v = doc.get(root, key);
            ASSERT_TRUE(v.has_value()) << "Missing: " << key;
        }
    }

    EXPECT_EQ(doc.length(root),
              static_cast<std::size_t>(1 + num_forks * keys_per_fork));
}

TEST(Document, save_load_with_thread_pool) {
    auto doc = Document{4u};
    doc.transact([](auto& tx) {
        for (int i = 0; i < 50; ++i) {
            tx.put(root, "k" + std::to_string(i), std::int64_t{i});
        }
    });

    auto bytes = doc.save();
    auto loaded = Document::load(bytes);
    ASSERT_TRUE(loaded.has_value());

    for (int i = 0; i < 50; ++i) {
        auto key = "k" + std::to_string(i);
        auto v = loaded->get(root, key);
        ASSERT_TRUE(v.has_value()) << "Missing: " << key;
    }
}
