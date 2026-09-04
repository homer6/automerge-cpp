#include <automerge-cpp/error.hpp>

#include <gtest/gtest.h>

using namespace automerge_cpp;

TEST(ErrorKind, to_string_view_covers_all_variants) {
    EXPECT_EQ(to_string_view(ErrorKind::invalid_document), "invalid_document");
    EXPECT_EQ(to_string_view(ErrorKind::invalid_change), "invalid_change");
    EXPECT_EQ(to_string_view(ErrorKind::invalid_obj_id), "invalid_obj_id");
    EXPECT_EQ(to_string_view(ErrorKind::invalid_object_type), "invalid_object_type");
    EXPECT_EQ(to_string_view(ErrorKind::index_out_of_bounds), "index_out_of_bounds");
    EXPECT_EQ(to_string_view(ErrorKind::invalid_key), "invalid_key");
    EXPECT_EQ(to_string_view(ErrorKind::invalid_utf8), "invalid_utf8");
    EXPECT_EQ(to_string_view(ErrorKind::invalid_cursor), "invalid_cursor");
    EXPECT_EQ(to_string_view(ErrorKind::invalid_mark), "invalid_mark");
    EXPECT_EQ(to_string_view(ErrorKind::invalid_value_type), "invalid_value_type");
    EXPECT_EQ(to_string_view(ErrorKind::inactive_transaction), "inactive_transaction");
    EXPECT_EQ(to_string_view(ErrorKind::encoding_error), "encoding_error");
    EXPECT_EQ(to_string_view(ErrorKind::decoding_error), "decoding_error");
    EXPECT_EQ(to_string_view(ErrorKind::sync_error), "sync_error");
    EXPECT_EQ(to_string_view(ErrorKind::invalid_operation), "invalid_operation");
}

TEST(Error, construction_and_equality) {
    const auto e1 = Error{ErrorKind::encoding_error, "bad bytes"};
    const auto e2 = Error{ErrorKind::encoding_error, "bad bytes"};
    const auto e3 = Error{ErrorKind::decoding_error, "bad bytes"};

    EXPECT_EQ(e1, e2);
    EXPECT_NE(e1, e3);
}

TEST(Error, different_messages_are_not_equal) {
    const auto e1 = Error{ErrorKind::encoding_error, "foo"};
    const auto e2 = Error{ErrorKind::encoding_error, "bar"};

    EXPECT_NE(e1, e2);
}

TEST(Error, kind_and_message_are_accessible) {
    const auto e = Error{ErrorKind::sync_error, "peer disconnected"};

    EXPECT_EQ(e.kind, ErrorKind::sync_error);
    EXPECT_EQ(e.message, "peer disconnected");
}
