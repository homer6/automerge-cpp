#include <automerge-cpp/value.hpp>

#include <gtest/gtest.h>

#include <string>

using namespace automerge_cpp;

// -- ObjType ------------------------------------------------------------------

TEST(ObjType, to_string_view_covers_all_variants) {
    EXPECT_EQ(to_string_view(ObjType::map),   "map");
    EXPECT_EQ(to_string_view(ObjType::list),  "list");
    EXPECT_EQ(to_string_view(ObjType::text),  "text");
    EXPECT_EQ(to_string_view(ObjType::table), "table");
}

// -- Null ---------------------------------------------------------------------

TEST(Null, all_nulls_are_equal) {
    EXPECT_EQ(Null{}, Null{});
}

// -- Counter ------------------------------------------------------------------

TEST(Counter, default_is_zero) {
    const auto c = Counter{};
    EXPECT_EQ(c.value, 0);
}

TEST(Counter, equality_and_ordering) {
    const auto a = Counter{5};
    const auto b = Counter{5};
    const auto c = Counter{10};

    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
    EXPECT_LT(a, c);
}

// -- Timestamp ----------------------------------------------------------------

TEST(Timestamp, default_is_zero) {
    const auto t = Timestamp{};
    EXPECT_EQ(t.millis_since_epoch, 0);
}

TEST(Timestamp, equality_and_ordering) {
    const auto a = Timestamp{1000};
    const auto b = Timestamp{1000};
    const auto c = Timestamp{2000};

    EXPECT_EQ(a, b);
    EXPECT_LT(a, c);
}

// -- ScalarValue --------------------------------------------------------------

TEST(ScalarValue, holds_null) {
    const auto v = ScalarValue{Null{}};
    EXPECT_TRUE(std::holds_alternative<Null>(v));
}

TEST(ScalarValue, holds_bool) {
    const auto v = ScalarValue{true};
    EXPECT_TRUE(std::holds_alternative<bool>(v));
    EXPECT_TRUE(std::get<bool>(v));
}

TEST(ScalarValue, holds_int64) {
    const auto v = ScalarValue{std::int64_t{-42}};
    EXPECT_TRUE(std::holds_alternative<std::int64_t>(v));
    EXPECT_EQ(std::get<std::int64_t>(v), -42);
}

TEST(ScalarValue, holds_uint64) {
    const auto v = ScalarValue{std::uint64_t{42}};
    EXPECT_TRUE(std::holds_alternative<std::uint64_t>(v));
    EXPECT_EQ(std::get<std::uint64_t>(v), 42u);
}

TEST(ScalarValue, holds_double) {
    const auto v = ScalarValue{3.14};
    EXPECT_TRUE(std::holds_alternative<double>(v));
    EXPECT_DOUBLE_EQ(std::get<double>(v), 3.14);
}

TEST(ScalarValue, holds_counter) {
    const auto v = ScalarValue{Counter{99}};
    EXPECT_TRUE(std::holds_alternative<Counter>(v));
    EXPECT_EQ(std::get<Counter>(v).value, 99);
}

TEST(ScalarValue, holds_timestamp) {
    const auto v = ScalarValue{Timestamp{1708000000000}};
    EXPECT_TRUE(std::holds_alternative<Timestamp>(v));
    EXPECT_EQ(std::get<Timestamp>(v).millis_since_epoch, 1708000000000);
}

TEST(ScalarValue, holds_string) {
    const auto v = ScalarValue{std::string{"hello"}};
    EXPECT_TRUE(std::holds_alternative<std::string>(v));
    EXPECT_EQ(std::get<std::string>(v), "hello");
}

TEST(ScalarValue, holds_bytes) {
    auto bytes = Bytes{std::byte{0xDE}, std::byte{0xAD}};
    const auto v = ScalarValue{std::move(bytes)};
    EXPECT_TRUE(std::holds_alternative<Bytes>(v));
    EXPECT_EQ(std::get<Bytes>(v).size(), 2u);
}

TEST(ScalarValue, int64_and_uint64_are_distinct_alternatives) {
    const auto signed_v   = ScalarValue{std::int64_t{42}};
    const auto unsigned_v = ScalarValue{std::uint64_t{42}};

    EXPECT_TRUE(std::holds_alternative<std::int64_t>(signed_v));
    EXPECT_TRUE(std::holds_alternative<std::uint64_t>(unsigned_v));
    EXPECT_FALSE(std::holds_alternative<std::uint64_t>(signed_v));
    EXPECT_FALSE(std::holds_alternative<std::int64_t>(unsigned_v));
}

// -- Value --------------------------------------------------------------------

TEST(Value, holds_obj_type) {
    const auto v = Value{ObjType::map};
    EXPECT_TRUE(is_object(v));
    EXPECT_FALSE(is_scalar(v));
    EXPECT_EQ(std::get<ObjType>(v), ObjType::map);
}

TEST(Value, holds_scalar_value) {
    const auto v = Value{ScalarValue{std::int64_t{7}}};
    EXPECT_TRUE(is_scalar(v));
    EXPECT_FALSE(is_object(v));
}

TEST(Value, all_obj_types_round_trip) {
    for (auto type : {ObjType::map, ObjType::list, ObjType::text, ObjType::table}) {
        const auto v = Value{type};
        EXPECT_EQ(std::get<ObjType>(v), type);
    }
}
