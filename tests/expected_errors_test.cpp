#include <automerge-cpp/automerge.hpp>
#include <automerge-cpp/json.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <vector>

using namespace automerge_cpp;

namespace {

auto unknown_object() -> ObjId {
    return ObjId{OpId{999, ActorId{}}};
}

} // namespace

TEST(ExpectedErrors, mutator_rejects_unknown_object_without_mutation) {
    auto document = Document{};
    const auto before = document.save();
    auto error = std::optional<Error>{};

    document.transact([&](Transaction& transaction) {
        const auto result = transaction.put(unknown_object(), "key", 1);
        ASSERT_FALSE(result);
        error = result.error();
    });

    ASSERT_TRUE(error);
    EXPECT_EQ(error->kind, ErrorKind::invalid_obj_id);
    EXPECT_EQ(document.save(), before);
}

TEST(ExpectedErrors, map_mutator_rejects_list_object_without_mutation) {
    auto document = Document{};
    const auto list = *document.transact(
        [](Transaction& transaction) { return *transaction.put(root, "list", ObjType::list); });
    const auto before = document.save();
    auto error = std::optional<Error>{};

    document.transact([&](Transaction& transaction) {
        const auto result = transaction.put(list, "key", 1);
        ASSERT_FALSE(result);
        error = result.error();
    });

    ASSERT_TRUE(error);
    EXPECT_EQ(error->kind, ErrorKind::invalid_object_type);
    EXPECT_EQ(document.save(), before);
}

TEST(ExpectedErrors, list_mutator_rejects_out_of_bounds_index_without_mutation) {
    auto document = Document{};
    const auto list = *document.transact(
        [](Transaction& transaction) { return *transaction.put(root, "list", ObjType::list); });
    const auto before = document.save();
    auto error = std::optional<Error>{};

    document.transact([&](Transaction& transaction) {
        const auto result = transaction.insert(list, 1, "value");
        ASSERT_FALSE(result);
        error = result.error();
    });

    ASSERT_TRUE(error);
    EXPECT_EQ(error->kind, ErrorKind::index_out_of_bounds);
    EXPECT_EQ(document.save(), before);
}

TEST(ExpectedErrors, mutator_rejects_invalid_utf8_key_without_mutation) {
    auto document = Document{};
    const auto before = document.save();
    const auto invalid_key = std::string{"\x80", 1};
    auto error = std::optional<Error>{};

    document.transact([&](Transaction& transaction) {
        const auto result = transaction.put(root, invalid_key, 1);
        ASSERT_FALSE(result);
        error = result.error();
    });

    ASSERT_TRUE(error);
    EXPECT_EQ(error->kind, ErrorKind::invalid_key);
    EXPECT_EQ(document.save(), before);
}

TEST(ExpectedErrors, mutator_rejects_invalid_utf8_string_without_mutation) {
    auto document = Document{};
    const auto before = document.save();
    const auto invalid_value = std::string{"\x80", 1};
    auto error = std::optional<Error>{};

    document.transact([&](Transaction& transaction) {
        const auto result = transaction.put(root, "key", invalid_value);
        ASSERT_FALSE(result);
        error = result.error();
    });

    ASSERT_TRUE(error);
    EXPECT_EQ(error->kind, ErrorKind::invalid_utf8);
    EXPECT_EQ(document.save(), before);
}

TEST(ExpectedErrors, string_overloads_reject_null_pointers_without_mutation) {
    auto document = Document{};
    const auto list = *document.transact([](Transaction& transaction) {
        const auto result = *transaction.put(root, "list", ObjType::list);
        transaction.insert(result, 0, "value");
        return result;
    });
    const auto before = document.save();
    const auto* null_string = static_cast<const char*>(nullptr);

    const auto put = document.transact(
        [&](Transaction& transaction) { return transaction.put(root, "key", null_string); });
    const auto insert = document.transact(
        [&](Transaction& transaction) { return transaction.insert(list, 1, null_string); });
    const auto set = document.transact(
        [&](Transaction& transaction) { return transaction.set(list, 0, null_string); });

    ASSERT_FALSE(put);
    ASSERT_FALSE(insert);
    ASSERT_FALSE(set);
    EXPECT_EQ(put.error().kind, ErrorKind::invalid_value_type);
    EXPECT_EQ(insert.error().kind, ErrorKind::invalid_value_type);
    EXPECT_EQ(set.error().kind, ErrorKind::invalid_value_type);
    EXPECT_EQ(document.save(), before);
}

TEST(ExpectedErrors, compound_mutator_validates_all_entries_before_mutation) {
    auto document = Document{};
    const auto before = document.save();
    const auto invalid_key = std::string{"\x80", 1};
    auto error = std::optional<Error>{};

    document.transact([&](Transaction& transaction) {
        const auto result = transaction.put(
            root, "container", Map{{"valid", 1}, {std::string_view{invalid_key}, 2}});
        ASSERT_FALSE(result);
        error = result.error();
    });

    ASSERT_TRUE(error);
    EXPECT_EQ(error->kind, ErrorKind::invalid_key);
    EXPECT_EQ(document.save(), before);
}

TEST(ExpectedErrors, text_splice_rejects_out_of_bounds_range_without_mutation) {
    auto document = Document{};
    const auto text = *document.transact(
        [](Transaction& transaction) { return *transaction.put(root, "text", ObjType::text); });
    const auto before = document.save();
    auto error = std::optional<Error>{};

    document.transact([&](Transaction& transaction) {
        const auto result = transaction.splice_text(text, 1, 0, "x");
        ASSERT_FALSE(result);
        error = result.error();
    });

    ASSERT_TRUE(error);
    EXPECT_EQ(error->kind, ErrorKind::index_out_of_bounds);
    EXPECT_EQ(document.save(), before);
}

TEST(ExpectedErrors, increment_rejects_non_counter_without_mutation) {
    auto document = Document{};
    document.transact([](Transaction& transaction) { transaction.put(root, "value", 1); });
    const auto before = document.save();
    auto error = std::optional<Error>{};

    document.transact([&](Transaction& transaction) {
        const auto result = transaction.increment(root, "value", 1);
        ASSERT_FALSE(result);
        error = result.error();
    });

    ASSERT_TRUE(error);
    EXPECT_EQ(error->kind, ErrorKind::invalid_value_type);
    EXPECT_EQ(document.save(), before);
}

TEST(ExpectedErrors, mark_rejects_invalid_range_without_mutation) {
    auto document = Document{};
    const auto text = *document.transact([](Transaction& transaction) {
        const auto object = *transaction.put(root, "text", ObjType::text);
        transaction.splice_text(object, 0, 0, "x");
        return object;
    });
    const auto before = document.save();
    auto error = std::optional<Error>{};

    document.transact([&](Transaction& transaction) {
        const auto result = transaction.mark(text, 1, 1, "bold", true);
        ASSERT_FALSE(result);
        error = result.error();
    });

    ASSERT_TRUE(error);
    EXPECT_EQ(error->kind, ErrorKind::invalid_mark);
    EXPECT_EQ(document.save(), before);
}

TEST(ExpectedErrors, committed_transaction_rejects_further_use_without_mutation) {
    auto document = Document{};
    const auto before = document.save();
    auto error = std::optional<Error>{};

    document.transact([&](Transaction& transaction) {
        ASSERT_TRUE(transaction.commit());
        const auto result = transaction.put(root, "key", 1);
        ASSERT_FALSE(result);
        error = result.error();
    });

    ASSERT_TRUE(error);
    EXPECT_EQ(error->kind, ErrorKind::inactive_transaction);
    EXPECT_EQ(document.save(), before);
}

TEST(ExpectedErrors, rolled_back_transaction_rejects_further_use_without_mutation) {
    auto document = Document{};
    const auto before = document.save();
    auto error = std::optional<Error>{};

    document.transact([&](Transaction& transaction) {
        ASSERT_TRUE(transaction.rollback());
        const auto result = transaction.put(root, "key", 1);
        ASSERT_FALSE(result);
        error = result.error();
    });

    ASSERT_TRUE(error);
    EXPECT_EQ(error->kind, ErrorKind::inactive_transaction);
    EXPECT_EQ(document.save(), before);
}

TEST(ExpectedErrors, transaction_runner_rolls_back_a_returned_error) {
    auto document = Document{};
    const auto before = document.save();

    const auto result = document.transact([](Transaction& transaction) {
        const auto first = transaction.put(root, "temporary", 1);
        if (!first) {
            return first;
        }
        return transaction.put(unknown_object(), "key", 2);
    });

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().kind, ErrorKind::invalid_obj_id);
    EXPECT_EQ(document.save(), before);
}

TEST(ExpectedErrors, patch_transaction_runner_rolls_back_a_returned_error) {
    auto document = Document{};
    const auto before = document.save();

    const auto result = document.transact_with_patches([](Transaction& transaction) {
        const auto first = transaction.put(root, "temporary", 1);
        if (!first) {
            return first;
        }
        return transaction.put(unknown_object(), "key", 2);
    });

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().kind, ErrorKind::invalid_obj_id);
    EXPECT_EQ(document.save(), before);
}

TEST(ExpectedErrors, transaction_lookup_rejects_wrong_object_type_without_mutation) {
    auto document = Document{};
    const auto list = *document.transact(
        [](Transaction& transaction) { return *transaction.put(root, "list", ObjType::list); });
    const auto before = document.save();
    auto error = std::optional<Error>{};

    document.transact([&](Transaction& transaction) {
        const auto result = transaction.get(list, "key");
        ASSERT_FALSE(result);
        error = result.error();
    });

    ASSERT_TRUE(error);
    EXPECT_EQ(error->kind, ErrorKind::invalid_object_type);
    EXPECT_EQ(document.save(), before);
}

TEST(ExpectedErrors, transaction_list_lookup_rejects_out_of_bounds_index_without_mutation) {
    auto document = Document{};
    const auto list = *document.transact(
        [](Transaction& transaction) { return *transaction.put(root, "list", ObjType::list); });
    const auto before = document.save();
    auto error = std::optional<Error>{};

    document.transact([&](Transaction& transaction) {
        const auto result = transaction.get(list, 0);
        ASSERT_FALSE(result);
        error = result.error();
    });

    ASSERT_TRUE(error);
    EXPECT_EQ(error->kind, ErrorKind::index_out_of_bounds);
    EXPECT_EQ(document.save(), before);
}

TEST(ExpectedErrors, document_lookup_rejects_unknown_object_without_mutation) {
    auto document = Document{};
    const auto before = document.save();

    const auto result = document.get(unknown_object(), "key");

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().kind, ErrorKind::invalid_obj_id);
    EXPECT_EQ(document.save(), before);
}

TEST(ExpectedErrors, document_map_lookup_rejects_sequence_object_without_mutation) {
    auto document = Document{};
    const auto list = *document.transact(
        [](Transaction& transaction) { return *transaction.put(root, "list", ObjType::list); });
    const auto before = document.save();

    const auto result = document.get(list, "key");

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().kind, ErrorKind::invalid_object_type);
    EXPECT_EQ(document.save(), before);
}

TEST(ExpectedErrors, document_list_lookup_rejects_out_of_bounds_index_without_mutation) {
    auto document = Document{};
    const auto list = *document.transact(
        [](Transaction& transaction) { return *transaction.put(root, "list", ObjType::list); });
    const auto before = document.save();

    const auto result = document.get(list, 0);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().kind, ErrorKind::index_out_of_bounds);
    EXPECT_EQ(document.save(), before);
}

TEST(ExpectedErrors, document_lookup_rejects_invalid_utf8_key_without_mutation) {
    auto document = Document{};
    const auto before = document.save();
    const auto invalid_key = std::string{"\x80", 1};

    const auto result = document.get(root, invalid_key);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().kind, ErrorKind::invalid_key);
    EXPECT_EQ(document.save(), before);
}

TEST(ExpectedErrors, document_queries_reject_wrong_object_types_without_mutation) {
    auto document = Document{};
    const auto list = *document.transact(
        [](Transaction& transaction) { return *transaction.put(root, "list", ObjType::list); });
    const auto before = document.save();

    const auto keys = document.keys(list);
    const auto text = document.text(root);

    ASSERT_FALSE(keys);
    ASSERT_FALSE(text);
    EXPECT_EQ(keys.error().kind, ErrorKind::invalid_object_type);
    EXPECT_EQ(text.error().kind, ErrorKind::invalid_object_type);
    EXPECT_EQ(document.save(), before);
}

TEST(ExpectedErrors, object_type_rejects_unknown_object_without_mutation) {
    auto document = Document{};
    const auto before = document.save();

    const auto result = document.object_type(unknown_object());

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().kind, ErrorKind::invalid_obj_id);
    EXPECT_EQ(document.save(), before);
}

TEST(ExpectedErrors, path_lookup_rejects_scalar_traversal_without_mutation) {
    auto document = Document{};
    document.transact([](Transaction& transaction) { transaction.put(root, "value", 1); });
    const auto before = document.save();

    const auto result = document.get_path("value", "nested");

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().kind, ErrorKind::invalid_value_type);
    EXPECT_EQ(document.save(), before);
}

TEST(ExpectedErrors, cursors_reject_invalid_indices_and_foreign_objects_without_mutation) {
    auto document = Document{};
    const auto first = *document.transact([](Transaction& transaction) {
        const auto list = *transaction.put(root, "first", ObjType::list);
        transaction.insert(list, 0, "value");
        return list;
    });
    const auto second = *document.transact([](Transaction& transaction) {
        const auto list = *transaction.put(root, "second", ObjType::list);
        transaction.insert(list, 0, "value");
        return list;
    });
    const auto cursor = document.cursor(first, 0);
    const auto before = document.save();

    const auto out_of_bounds = document.cursor(first, 1);
    const auto foreign = document.resolve_cursor(second, *cursor);

    ASSERT_FALSE(out_of_bounds);
    ASSERT_FALSE(foreign);
    EXPECT_EQ(out_of_bounds.error().kind, ErrorKind::index_out_of_bounds);
    EXPECT_EQ(foreign.error().kind, ErrorKind::invalid_cursor);
    EXPECT_EQ(document.save(), before);
}

TEST(ExpectedErrors, mark_query_rejects_map_object_without_mutation) {
    auto document = Document{};
    const auto before = document.save();

    const auto result = document.marks(root);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().kind, ErrorKind::invalid_object_type);
    EXPECT_EQ(document.save(), before);
}

TEST(ExpectedErrors, historical_queries_reject_unknown_heads_without_mutation) {
    auto document = Document{};
    const auto before = document.save();
    const auto heads = std::vector<ChangeHash>{ChangeHash{}};

    const auto value = document.get_at(root, "key", heads);
    const auto changes = document.get_changes_since(heads);

    ASSERT_FALSE(value);
    ASSERT_FALSE(changes);
    EXPECT_EQ(value.error().kind, ErrorKind::invalid_change);
    EXPECT_EQ(changes.error().kind, ErrorKind::invalid_change);
    EXPECT_EQ(document.save(), before);
}

TEST(ExpectedErrors, malformed_byte_decoders_report_kinds_without_mutation) {
    auto document = Document{};
    const auto before = document.save();
    const auto bytes = std::vector<std::byte>{std::byte{0xff}};
    auto loaded_with_patches = Document{};
    const auto loaded_with_patches_before = loaded_with_patches.save();

    const auto loaded = Document::load(bytes);
    const auto shallow = Document::load_shallow(bytes);
    const auto patches = Document::load_with_patches(bytes, loaded_with_patches);
    const auto incremental = document.load_incremental(bytes);
    const auto sync_message = SyncMessage::decode(bytes);
    const auto sync_state = SyncState::decode(bytes);

    ASSERT_FALSE(loaded);
    ASSERT_FALSE(shallow);
    ASSERT_FALSE(patches);
    ASSERT_FALSE(incremental);
    ASSERT_FALSE(sync_message);
    ASSERT_FALSE(sync_state);
    EXPECT_EQ(loaded.error().kind, ErrorKind::invalid_document);
    EXPECT_EQ(shallow.error().kind, ErrorKind::invalid_document);
    EXPECT_EQ(patches.error().kind, ErrorKind::invalid_document);
    EXPECT_EQ(incremental.error().kind, ErrorKind::invalid_document);
    EXPECT_EQ(sync_message.error().kind, ErrorKind::decoding_error);
    EXPECT_EQ(sync_state.error().kind, ErrorKind::decoding_error);
    EXPECT_EQ(document.save(), before);
    EXPECT_EQ(loaded_with_patches.save(), loaded_with_patches_before);
}

TEST(ExpectedErrors, external_change_operations_reject_malformed_changes_without_mutation) {
    auto document = Document{};
    const auto before = document.save();
    const auto changes = std::vector<Change>{Change{}};
    auto sync_state = SyncState{};
    const auto message = SyncMessage{.changes = changes};

    const auto applied = document.apply_changes(changes);
    const auto applied_patches = document.apply_changes_with_patches(changes);
    const auto received = document.receive_sync_message(sync_state, message);
    const auto received_patches = document.receive_sync_message_with_patches(sync_state, message);

    ASSERT_FALSE(applied);
    ASSERT_FALSE(applied_patches);
    ASSERT_FALSE(received);
    ASSERT_FALSE(received_patches);
    EXPECT_EQ(applied.error().kind, ErrorKind::invalid_change);
    EXPECT_EQ(applied_patches.error().kind, ErrorKind::invalid_change);
    EXPECT_EQ(received.error().kind, ErrorKind::invalid_change);
    EXPECT_EQ(received_patches.error().kind, ErrorKind::invalid_change);
    EXPECT_EQ(document.save(), before);
}

TEST(ExpectedErrors, sync_receive_rejects_invalid_bloom_filters_without_mutation) {
    auto document = Document{};
    const auto before = document.save();
    auto sync_state = SyncState{};
    const auto message = SyncMessage{
        .have = {Have{.bloom_bytes = {std::byte{0xff}}}},
    };

    const auto result = document.receive_sync_message(sync_state, message);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().kind, ErrorKind::sync_error);
    EXPECT_EQ(document.save(), before);
}

TEST(ExpectedErrors, json_exports_reject_unknown_objects_without_mutation) {
    auto document = Document{};
    const auto before = document.save();

    const auto exported = json::export_json(document, unknown_object());
    const auto flattened = json::flatten(document, unknown_object());

    ASSERT_FALSE(exported);
    ASSERT_FALSE(flattened);
    EXPECT_EQ(exported.error().kind, ErrorKind::invalid_obj_id);
    EXPECT_EQ(flattened.error().kind, ErrorKind::invalid_obj_id);
    EXPECT_EQ(document.save(), before);
}

TEST(ExpectedErrors, historical_json_export_rejects_unknown_heads_without_mutation) {
    auto document = Document{};
    const auto before = document.save();
    const auto heads = std::vector<ChangeHash>{ChangeHash{}};

    const auto result = json::export_json_at(document, heads);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().kind, ErrorKind::invalid_change);
    EXPECT_EQ(document.save(), before);
}

TEST(ExpectedErrors, json_import_rejects_unknown_targets_without_mutation) {
    auto document = Document{};
    const auto before = document.save();

    const auto document_result =
        json::import_json(document, nlohmann::json{{"key", "value"}}, unknown_object());
    const auto transaction_result = document.transact([](Transaction& transaction) {
        return json::import_json(transaction, nlohmann::json{{"key", "value"}}, unknown_object());
    });

    ASSERT_FALSE(document_result);
    ASSERT_FALSE(transaction_result);
    EXPECT_EQ(document_result.error().kind, ErrorKind::invalid_obj_id);
    EXPECT_EQ(transaction_result.error().kind, ErrorKind::invalid_obj_id);
    EXPECT_EQ(document.save(), before);
}

TEST(ExpectedErrors, json_pointer_operations_reject_malformed_paths_without_mutation) {
    auto document = Document{};
    const auto before = document.save();

    const auto read = json::get_pointer(document, "missing-slash");
    const auto write = json::put_pointer(document, "/invalid~2escape", std::int64_t{1});
    const auto remove = json::delete_pointer(document, "");

    ASSERT_FALSE(read);
    ASSERT_FALSE(write);
    ASSERT_FALSE(remove);
    EXPECT_EQ(read.error().kind, ErrorKind::invalid_operation);
    EXPECT_EQ(write.error().kind, ErrorKind::invalid_operation);
    EXPECT_EQ(remove.error().kind, ErrorKind::invalid_operation);
    EXPECT_EQ(document.save(), before);
}

TEST(ExpectedErrors, json_patch_failure_rolls_back_prior_operations) {
    auto document = Document{};
    const auto before = document.save();
    const auto patch = nlohmann::json::parse(R"([
        {"op": "add", "path": "/temporary", "value": 1},
        {"op": "test", "path": "/temporary", "value": 2}
    ])");

    const auto result = json::apply_json_patch(document, patch);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().kind, ErrorKind::invalid_operation);
    EXPECT_EQ(document.save(), before);
}

TEST(ExpectedErrors, json_merge_patch_rejects_unknown_targets_without_mutation) {
    auto document = Document{};
    const auto before = document.save();

    const auto result =
        json::apply_merge_patch(document, nlohmann::json{{"key", "value"}}, unknown_object());

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().kind, ErrorKind::invalid_obj_id);
    EXPECT_EQ(document.save(), before);
}

TEST(ExpectedErrors, json_unflatten_rejects_unknown_targets_without_mutation) {
    auto document = Document{};
    const auto before = document.save();
    const auto flat = std::map<std::string, nlohmann::json>{{"/key", "value"}};

    const auto result = json::unflatten(document, flat, unknown_object());

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().kind, ErrorKind::invalid_obj_id);
    EXPECT_EQ(document.save(), before);
}
