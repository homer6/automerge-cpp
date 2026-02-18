#include "../src/encoding/rle.hpp"

#include <gtest/gtest.h>

using namespace automerge_cpp::encoding;

// -- RLE round-trip tests -----------------------------------------------------

TEST(RleEncoder, empty_produces_no_bytes) {
    auto enc = RleEncoder<std::uint64_t>{};
    enc.finish();
    EXPECT_TRUE(enc.data().empty());
}

TEST(RleEncoder, single_value_round_trips) {
    auto enc = RleEncoder<std::uint64_t>{};
    enc.append(42);
    enc.finish();

    auto dec = RleDecoder<std::uint64_t>{enc.data()};
    auto val = dec.next();
    ASSERT_TRUE(val.has_value());
    ASSERT_TRUE(val->has_value());
    EXPECT_EQ(**val, 42u);

    auto end = dec.next();
    EXPECT_FALSE(end.has_value());
}

TEST(RleEncoder, run_of_same_value_round_trips) {
    auto enc = RleEncoder<std::uint64_t>{};
    for (int i = 0; i < 5; ++i) enc.append(7);
    enc.finish();

    auto dec = RleDecoder<std::uint64_t>{enc.data()};
    for (int i = 0; i < 5; ++i) {
        auto val = dec.next();
        ASSERT_TRUE(val.has_value()) << "at i=" << i;
        ASSERT_TRUE(val->has_value()) << "at i=" << i;
        EXPECT_EQ(**val, 7u) << "at i=" << i;
    }
    EXPECT_FALSE(dec.next().has_value());
}

TEST(RleEncoder, literal_run_round_trips) {
    auto enc = RleEncoder<std::uint64_t>{};
    enc.append(1);
    enc.append(2);
    enc.append(3);
    enc.append(4);
    enc.append(5);
    enc.finish();

    auto dec = RleDecoder<std::uint64_t>{enc.data()};
    for (std::uint64_t i = 1; i <= 5; ++i) {
        auto val = dec.next();
        ASSERT_TRUE(val.has_value());
        ASSERT_TRUE(val->has_value());
        EXPECT_EQ(**val, i);
    }
    EXPECT_FALSE(dec.next().has_value());
}

TEST(RleEncoder, null_run_round_trips) {
    auto enc = RleEncoder<std::uint64_t>{};
    enc.append_null();
    enc.append_null();
    enc.append_null();
    enc.finish();

    auto dec = RleDecoder<std::uint64_t>{enc.data()};
    for (int i = 0; i < 3; ++i) {
        auto val = dec.next();
        ASSERT_TRUE(val.has_value()) << "at i=" << i;
        EXPECT_FALSE(val->has_value()) << "at i=" << i;  // null
    }
    EXPECT_FALSE(dec.next().has_value());
}

TEST(RleEncoder, mixed_runs_and_literals_round_trip) {
    auto enc = RleEncoder<std::uint64_t>{};
    // Run of 3
    enc.append(10); enc.append(10); enc.append(10);
    // Literal run
    enc.append(20); enc.append(30);
    // Another run
    enc.append(40); enc.append(40);
    enc.finish();

    auto dec = RleDecoder<std::uint64_t>{enc.data()};

    // Run of 3 x 10
    for (int i = 0; i < 3; ++i) {
        auto val = dec.next();
        ASSERT_TRUE(val.has_value() && val->has_value());
        EXPECT_EQ(**val, 10u);
    }
    // Literals 20, 30
    auto v = dec.next();
    ASSERT_TRUE(v.has_value() && v->has_value());
    EXPECT_EQ(**v, 20u);
    v = dec.next();
    ASSERT_TRUE(v.has_value() && v->has_value());
    EXPECT_EQ(**v, 30u);
    // Run of 2 x 40
    for (int i = 0; i < 2; ++i) {
        v = dec.next();
        ASSERT_TRUE(v.has_value() && v->has_value());
        EXPECT_EQ(**v, 40u);
    }
    EXPECT_FALSE(dec.next().has_value());
}

TEST(RleEncoder, nulls_between_values_round_trip) {
    auto enc = RleEncoder<std::uint64_t>{};
    enc.append(1);
    enc.append_null();
    enc.append_null();
    enc.append(2);
    enc.finish();

    auto dec = RleDecoder<std::uint64_t>{enc.data()};

    auto v1 = dec.next();
    ASSERT_TRUE(v1.has_value() && v1->has_value());
    EXPECT_EQ(**v1, 1u);

    auto n1 = dec.next();
    ASSERT_TRUE(n1.has_value());
    EXPECT_FALSE(n1->has_value());

    auto n2 = dec.next();
    ASSERT_TRUE(n2.has_value());
    EXPECT_FALSE(n2->has_value());

    auto v2 = dec.next();
    ASSERT_TRUE(v2.has_value() && v2->has_value());
    EXPECT_EQ(**v2, 2u);

    EXPECT_FALSE(dec.next().has_value());
}

TEST(RleEncoder, signed_values_round_trip) {
    auto enc = RleEncoder<std::int64_t>{};
    enc.append(-5);
    enc.append(-5);
    enc.append(10);
    enc.append(-3);
    enc.finish();

    auto dec = RleDecoder<std::int64_t>{enc.data()};

    auto v1 = dec.next();
    ASSERT_TRUE(v1.has_value() && v1->has_value());
    EXPECT_EQ(**v1, -5);

    auto v2 = dec.next();
    ASSERT_TRUE(v2.has_value() && v2->has_value());
    EXPECT_EQ(**v2, -5);

    auto v3 = dec.next();
    ASSERT_TRUE(v3.has_value() && v3->has_value());
    EXPECT_EQ(**v3, 10);

    auto v4 = dec.next();
    ASSERT_TRUE(v4.has_value() && v4->has_value());
    EXPECT_EQ(**v4, -3);

    EXPECT_FALSE(dec.next().has_value());
}

TEST(RleEncoder, string_values_round_trip) {
    auto enc = RleEncoder<std::string>{};
    enc.append("hello");
    enc.append("hello");
    enc.append("world");
    enc.finish();

    auto dec = RleDecoder<std::string>{enc.data()};

    auto v1 = dec.next();
    ASSERT_TRUE(v1.has_value() && v1->has_value());
    EXPECT_EQ(**v1, "hello");

    auto v2 = dec.next();
    ASSERT_TRUE(v2.has_value() && v2->has_value());
    EXPECT_EQ(**v2, "hello");

    auto v3 = dec.next();
    ASSERT_TRUE(v3.has_value() && v3->has_value());
    EXPECT_EQ(**v3, "world");

    EXPECT_FALSE(dec.next().has_value());
}

TEST(RleEncoder, large_run_round_trips) {
    auto enc = RleEncoder<std::uint64_t>{};
    for (int i = 0; i < 1000; ++i) enc.append(99);
    enc.finish();

    auto dec = RleDecoder<std::uint64_t>{enc.data()};
    for (int i = 0; i < 1000; ++i) {
        auto val = dec.next();
        ASSERT_TRUE(val.has_value() && val->has_value());
        EXPECT_EQ(**val, 99u);
    }
    EXPECT_FALSE(dec.next().has_value());

    // Should be much smaller than 1000 values
    EXPECT_LT(enc.data().size(), 20u);
}

// =============================================================================
// Single-element edge cases
// =============================================================================

TEST(RleEncoder, single_null_round_trips) {
    auto enc = RleEncoder<std::uint64_t>{};
    enc.append_null();
    enc.finish();

    auto dec = RleDecoder<std::uint64_t>{enc.data()};
    auto val = dec.next();
    ASSERT_TRUE(val.has_value());
    EXPECT_FALSE(val->has_value());  // null
    EXPECT_FALSE(dec.next().has_value());  // end
}

TEST(RleEncoder, single_zero_value_round_trips) {
    auto enc = RleEncoder<std::uint64_t>{};
    enc.append(0);
    enc.finish();

    auto dec = RleDecoder<std::uint64_t>{enc.data()};
    auto val = dec.next();
    ASSERT_TRUE(val.has_value());
    ASSERT_TRUE(val->has_value());
    EXPECT_EQ(**val, 0u);
    EXPECT_FALSE(dec.next().has_value());
}

TEST(RleEncoder, single_max_uint64_round_trips) {
    auto enc = RleEncoder<std::uint64_t>{};
    enc.append(std::numeric_limits<std::uint64_t>::max());
    enc.finish();

    auto dec = RleDecoder<std::uint64_t>{enc.data()};
    auto val = dec.next();
    ASSERT_TRUE(val.has_value());
    ASSERT_TRUE(val->has_value());
    EXPECT_EQ(**val, std::numeric_limits<std::uint64_t>::max());
    EXPECT_FALSE(dec.next().has_value());
}

TEST(RleEncoder, single_int64_min_round_trips) {
    auto enc = RleEncoder<std::int64_t>{};
    enc.append(std::numeric_limits<std::int64_t>::min());
    enc.finish();

    auto dec = RleDecoder<std::int64_t>{enc.data()};
    auto val = dec.next();
    ASSERT_TRUE(val.has_value());
    ASSERT_TRUE(val->has_value());
    EXPECT_EQ(**val, std::numeric_limits<std::int64_t>::min());
    EXPECT_FALSE(dec.next().has_value());
}

TEST(RleEncoder, single_int64_max_round_trips) {
    auto enc = RleEncoder<std::int64_t>{};
    enc.append(std::numeric_limits<std::int64_t>::max());
    enc.finish();

    auto dec = RleDecoder<std::int64_t>{enc.data()};
    auto val = dec.next();
    ASSERT_TRUE(val.has_value());
    ASSERT_TRUE(val->has_value());
    EXPECT_EQ(**val, std::numeric_limits<std::int64_t>::max());
    EXPECT_FALSE(dec.next().has_value());
}

TEST(RleEncoder, single_string_round_trips) {
    auto enc = RleEncoder<std::string>{};
    enc.append("test");
    enc.finish();

    auto dec = RleDecoder<std::string>{enc.data()};
    auto val = dec.next();
    ASSERT_TRUE(val.has_value());
    ASSERT_TRUE(val->has_value());
    EXPECT_EQ(**val, "test");
    EXPECT_FALSE(dec.next().has_value());
}

TEST(RleEncoder, single_empty_string_round_trips) {
    auto enc = RleEncoder<std::string>{};
    enc.append("");
    enc.finish();

    auto dec = RleDecoder<std::string>{enc.data()};
    auto val = dec.next();
    ASSERT_TRUE(val.has_value());
    ASSERT_TRUE(val->has_value());
    EXPECT_EQ(**val, "");
    EXPECT_FALSE(dec.next().has_value());
}

TEST(RleEncoder, single_null_int64_round_trips) {
    auto enc = RleEncoder<std::int64_t>{};
    enc.append_null();
    enc.finish();

    auto dec = RleDecoder<std::int64_t>{enc.data()};
    auto val = dec.next();
    ASSERT_TRUE(val.has_value());
    EXPECT_FALSE(val->has_value());  // null
    EXPECT_FALSE(dec.next().has_value());
}

TEST(RleEncoder, single_null_string_round_trips) {
    auto enc = RleEncoder<std::string>{};
    enc.append_null();
    enc.finish();

    auto dec = RleDecoder<std::string>{enc.data()};
    auto val = dec.next();
    ASSERT_TRUE(val.has_value());
    EXPECT_FALSE(val->has_value());  // null
    EXPECT_FALSE(dec.next().has_value());
}

// =============================================================================
// Null positioning
// =============================================================================

TEST(RleEncoder, null_then_value_round_trips) {
    auto enc = RleEncoder<std::uint64_t>{};
    enc.append_null();
    enc.append(42);
    enc.finish();

    auto dec = RleDecoder<std::uint64_t>{enc.data()};
    auto n = dec.next();
    ASSERT_TRUE(n.has_value());
    EXPECT_FALSE(n->has_value());

    auto v = dec.next();
    ASSERT_TRUE(v.has_value());
    ASSERT_TRUE(v->has_value());
    EXPECT_EQ(**v, 42u);

    EXPECT_FALSE(dec.next().has_value());
}

TEST(RleEncoder, value_then_null_round_trips) {
    auto enc = RleEncoder<std::uint64_t>{};
    enc.append(42);
    enc.append_null();
    enc.finish();

    auto dec = RleDecoder<std::uint64_t>{enc.data()};
    auto v = dec.next();
    ASSERT_TRUE(v.has_value());
    ASSERT_TRUE(v->has_value());
    EXPECT_EQ(**v, 42u);

    auto n = dec.next();
    ASSERT_TRUE(n.has_value());
    EXPECT_FALSE(n->has_value());

    EXPECT_FALSE(dec.next().has_value());
}

TEST(RleEncoder, null_at_start_middle_end) {
    auto enc = RleEncoder<std::uint64_t>{};
    enc.append_null();
    enc.append(1);
    enc.append_null();
    enc.append(2);
    enc.append_null();
    enc.finish();

    auto dec = RleDecoder<std::uint64_t>{enc.data()};
    std::vector<std::optional<std::uint64_t>> results;
    while (auto val = dec.next()) {
        results.push_back(*val);
    }

    ASSERT_EQ(results.size(), 5u);
    EXPECT_FALSE(results[0].has_value());
    ASSERT_TRUE(results[1].has_value());
    EXPECT_EQ(*results[1], 1u);
    EXPECT_FALSE(results[2].has_value());
    ASSERT_TRUE(results[3].has_value());
    EXPECT_EQ(*results[3], 2u);
    EXPECT_FALSE(results[4].has_value());
}

TEST(RleEncoder, multiple_nulls_at_start) {
    auto enc = RleEncoder<std::uint64_t>{};
    enc.append_null();
    enc.append_null();
    enc.append_null();
    enc.append(5);
    enc.finish();

    auto dec = RleDecoder<std::uint64_t>{enc.data()};
    for (int i = 0; i < 3; ++i) {
        auto val = dec.next();
        ASSERT_TRUE(val.has_value()) << "null at i=" << i;
        EXPECT_FALSE(val->has_value()) << "null at i=" << i;
    }
    auto v = dec.next();
    ASSERT_TRUE(v.has_value());
    ASSERT_TRUE(v->has_value());
    EXPECT_EQ(**v, 5u);
    EXPECT_FALSE(dec.next().has_value());
}

TEST(RleEncoder, multiple_nulls_at_end) {
    auto enc = RleEncoder<std::uint64_t>{};
    enc.append(5);
    enc.append_null();
    enc.append_null();
    enc.append_null();
    enc.finish();

    auto dec = RleDecoder<std::uint64_t>{enc.data()};
    auto v = dec.next();
    ASSERT_TRUE(v.has_value());
    ASSERT_TRUE(v->has_value());
    EXPECT_EQ(**v, 5u);
    for (int i = 0; i < 3; ++i) {
        auto val = dec.next();
        ASSERT_TRUE(val.has_value()) << "null at i=" << i;
        EXPECT_FALSE(val->has_value()) << "null at i=" << i;
    }
    EXPECT_FALSE(dec.next().has_value());
}

// =============================================================================
// Null interleaved with runs
// =============================================================================

TEST(RleEncoder, run_null_run_round_trips) {
    auto enc = RleEncoder<std::uint64_t>{};
    enc.append(7); enc.append(7); enc.append(7);
    enc.append_null();
    enc.append_null();
    enc.append(8); enc.append(8);
    enc.finish();

    auto dec = RleDecoder<std::uint64_t>{enc.data()};
    for (int i = 0; i < 3; ++i) {
        auto val = dec.next();
        ASSERT_TRUE(val.has_value() && val->has_value());
        EXPECT_EQ(**val, 7u);
    }
    for (int i = 0; i < 2; ++i) {
        auto val = dec.next();
        ASSERT_TRUE(val.has_value());
        EXPECT_FALSE(val->has_value());
    }
    for (int i = 0; i < 2; ++i) {
        auto val = dec.next();
        ASSERT_TRUE(val.has_value() && val->has_value());
        EXPECT_EQ(**val, 8u);
    }
    EXPECT_FALSE(dec.next().has_value());
}

TEST(RleEncoder, null_run_null_round_trips) {
    auto enc = RleEncoder<std::uint64_t>{};
    enc.append_null();
    enc.append(5); enc.append(5); enc.append(5);
    enc.append_null();
    enc.finish();

    auto dec = RleDecoder<std::uint64_t>{enc.data()};
    auto n1 = dec.next();
    ASSERT_TRUE(n1.has_value());
    EXPECT_FALSE(n1->has_value());
    for (int i = 0; i < 3; ++i) {
        auto val = dec.next();
        ASSERT_TRUE(val.has_value() && val->has_value());
        EXPECT_EQ(**val, 5u);
    }
    auto n2 = dec.next();
    ASSERT_TRUE(n2.has_value());
    EXPECT_FALSE(n2->has_value());
    EXPECT_FALSE(dec.next().has_value());
}

TEST(RleEncoder, alternating_null_and_value) {
    auto enc = RleEncoder<std::uint64_t>{};
    for (int i = 0; i < 5; ++i) {
        enc.append_null();
        enc.append(static_cast<std::uint64_t>(i));
    }
    enc.finish();

    auto dec = RleDecoder<std::uint64_t>{enc.data()};
    for (int i = 0; i < 5; ++i) {
        auto n = dec.next();
        ASSERT_TRUE(n.has_value()) << "null at i=" << i;
        EXPECT_FALSE(n->has_value()) << "null at i=" << i;
        auto v = dec.next();
        ASSERT_TRUE(v.has_value()) << "value at i=" << i;
        ASSERT_TRUE(v->has_value()) << "value at i=" << i;
        EXPECT_EQ(**v, static_cast<std::uint64_t>(i)) << "value at i=" << i;
    }
    EXPECT_FALSE(dec.next().has_value());
}

// =============================================================================
// String-specific tests
// =============================================================================

TEST(RleEncoder, string_run_of_same_round_trips) {
    auto enc = RleEncoder<std::string>{};
    enc.append("abc");
    enc.append("abc");
    enc.append("abc");
    enc.finish();

    auto dec = RleDecoder<std::string>{enc.data()};
    for (int i = 0; i < 3; ++i) {
        auto val = dec.next();
        ASSERT_TRUE(val.has_value() && val->has_value());
        EXPECT_EQ(**val, "abc");
    }
    EXPECT_FALSE(dec.next().has_value());
}

TEST(RleEncoder, string_literal_run_round_trips) {
    auto enc = RleEncoder<std::string>{};
    enc.append("alpha");
    enc.append("beta");
    enc.append("gamma");
    enc.finish();

    auto dec = RleDecoder<std::string>{enc.data()};
    auto v1 = dec.next();
    ASSERT_TRUE(v1.has_value() && v1->has_value());
    EXPECT_EQ(**v1, "alpha");
    auto v2 = dec.next();
    ASSERT_TRUE(v2.has_value() && v2->has_value());
    EXPECT_EQ(**v2, "beta");
    auto v3 = dec.next();
    ASSERT_TRUE(v3.has_value() && v3->has_value());
    EXPECT_EQ(**v3, "gamma");
    EXPECT_FALSE(dec.next().has_value());
}

TEST(RleEncoder, string_null_between_values_round_trips) {
    auto enc = RleEncoder<std::string>{};
    enc.append("hello");
    enc.append_null();
    enc.append("world");
    enc.finish();

    auto dec = RleDecoder<std::string>{enc.data()};
    auto v1 = dec.next();
    ASSERT_TRUE(v1.has_value() && v1->has_value());
    EXPECT_EQ(**v1, "hello");
    auto n = dec.next();
    ASSERT_TRUE(n.has_value());
    EXPECT_FALSE(n->has_value());
    auto v2 = dec.next();
    ASSERT_TRUE(v2.has_value() && v2->has_value());
    EXPECT_EQ(**v2, "world");
    EXPECT_FALSE(dec.next().has_value());
}

TEST(RleEncoder, string_null_only_round_trips) {
    auto enc = RleEncoder<std::string>{};
    enc.append_null();
    enc.append_null();
    enc.finish();

    auto dec = RleDecoder<std::string>{enc.data()};
    auto n1 = dec.next();
    ASSERT_TRUE(n1.has_value());
    EXPECT_FALSE(n1->has_value());
    auto n2 = dec.next();
    ASSERT_TRUE(n2.has_value());
    EXPECT_FALSE(n2->has_value());
    EXPECT_FALSE(dec.next().has_value());
}

TEST(RleEncoder, string_with_special_characters_round_trips) {
    auto enc = RleEncoder<std::string>{};
    enc.append("hello\nworld");
    enc.append("tab\there");
    enc.append(std::string("null\0byte", 9));
    enc.finish();

    auto dec = RleDecoder<std::string>{enc.data()};
    auto v1 = dec.next();
    ASSERT_TRUE(v1.has_value() && v1->has_value());
    EXPECT_EQ(**v1, "hello\nworld");
    auto v2 = dec.next();
    ASSERT_TRUE(v2.has_value() && v2->has_value());
    EXPECT_EQ(**v2, "tab\there");
    auto v3 = dec.next();
    ASSERT_TRUE(v3.has_value() && v3->has_value());
    EXPECT_EQ(**v3, std::string("null\0byte", 9));
    EXPECT_FALSE(dec.next().has_value());
}

TEST(RleEncoder, string_empty_and_nonempty_mixed_round_trips) {
    auto enc = RleEncoder<std::string>{};
    enc.append("");
    enc.append("x");
    enc.append("");
    enc.append("y");
    enc.finish();

    auto dec = RleDecoder<std::string>{enc.data()};
    auto v1 = dec.next();
    ASSERT_TRUE(v1.has_value() && v1->has_value());
    EXPECT_EQ(**v1, "");
    auto v2 = dec.next();
    ASSERT_TRUE(v2.has_value() && v2->has_value());
    EXPECT_EQ(**v2, "x");
    auto v3 = dec.next();
    ASSERT_TRUE(v3.has_value() && v3->has_value());
    EXPECT_EQ(**v3, "");
    auto v4 = dec.next();
    ASSERT_TRUE(v4.has_value() && v4->has_value());
    EXPECT_EQ(**v4, "y");
    EXPECT_FALSE(dec.next().has_value());
}

TEST(RleEncoder, string_run_of_empty_round_trips) {
    auto enc = RleEncoder<std::string>{};
    enc.append("");
    enc.append("");
    enc.append("");
    enc.finish();

    auto dec = RleDecoder<std::string>{enc.data()};
    for (int i = 0; i < 3; ++i) {
        auto val = dec.next();
        ASSERT_TRUE(val.has_value() && val->has_value());
        EXPECT_EQ(**val, "");
    }
    EXPECT_FALSE(dec.next().has_value());
}

TEST(RleEncoder, long_string_round_trips) {
    auto long_str = std::string(10000, 'z');
    auto enc = RleEncoder<std::string>{};
    enc.append(long_str);
    enc.finish();

    auto dec = RleDecoder<std::string>{enc.data()};
    auto val = dec.next();
    ASSERT_TRUE(val.has_value() && val->has_value());
    EXPECT_EQ(**val, long_str);
    EXPECT_FALSE(dec.next().has_value());
}

// =============================================================================
// Signed integer edge cases
// =============================================================================

TEST(RleEncoder, int64_zero_round_trips) {
    auto enc = RleEncoder<std::int64_t>{};
    enc.append(0);
    enc.finish();

    auto dec = RleDecoder<std::int64_t>{enc.data()};
    auto val = dec.next();
    ASSERT_TRUE(val.has_value());
    ASSERT_TRUE(val->has_value());
    EXPECT_EQ(**val, 0);
    EXPECT_FALSE(dec.next().has_value());
}

TEST(RleEncoder, int64_negative_run_round_trips) {
    auto enc = RleEncoder<std::int64_t>{};
    for (int i = 0; i < 4; ++i) enc.append(-1);
    enc.finish();

    auto dec = RleDecoder<std::int64_t>{enc.data()};
    for (int i = 0; i < 4; ++i) {
        auto val = dec.next();
        ASSERT_TRUE(val.has_value() && val->has_value());
        EXPECT_EQ(**val, -1);
    }
    EXPECT_FALSE(dec.next().has_value());
}

TEST(RleEncoder, int64_alternating_sign_round_trips) {
    auto enc = RleEncoder<std::int64_t>{};
    enc.append(-100);
    enc.append(100);
    enc.append(-200);
    enc.append(200);
    enc.finish();

    auto dec = RleDecoder<std::int64_t>{enc.data()};
    auto v1 = dec.next(); ASSERT_TRUE(v1.has_value() && v1->has_value()); EXPECT_EQ(**v1, -100);
    auto v2 = dec.next(); ASSERT_TRUE(v2.has_value() && v2->has_value()); EXPECT_EQ(**v2, 100);
    auto v3 = dec.next(); ASSERT_TRUE(v3.has_value() && v3->has_value()); EXPECT_EQ(**v3, -200);
    auto v4 = dec.next(); ASSERT_TRUE(v4.has_value() && v4->has_value()); EXPECT_EQ(**v4, 200);
    EXPECT_FALSE(dec.next().has_value());
}

TEST(RleEncoder, int64_null_between_negatives_round_trips) {
    auto enc = RleEncoder<std::int64_t>{};
    enc.append(-5);
    enc.append_null();
    enc.append(-10);
    enc.finish();

    auto dec = RleDecoder<std::int64_t>{enc.data()};
    auto v1 = dec.next(); ASSERT_TRUE(v1.has_value() && v1->has_value()); EXPECT_EQ(**v1, -5);
    auto n = dec.next(); ASSERT_TRUE(n.has_value()); EXPECT_FALSE(n->has_value());
    auto v2 = dec.next(); ASSERT_TRUE(v2.has_value() && v2->has_value()); EXPECT_EQ(**v2, -10);
    EXPECT_FALSE(dec.next().has_value());
}

// =============================================================================
// Transition patterns
// =============================================================================

TEST(RleEncoder, run_then_literal_round_trips) {
    auto enc = RleEncoder<std::uint64_t>{};
    enc.append(1); enc.append(1); enc.append(1);  // run of 3
    enc.append(2); enc.append(3);  // literal of 2
    enc.finish();

    auto dec = RleDecoder<std::uint64_t>{enc.data()};
    for (int i = 0; i < 3; ++i) {
        auto val = dec.next();
        ASSERT_TRUE(val.has_value() && val->has_value());
        EXPECT_EQ(**val, 1u);
    }
    auto v2 = dec.next(); ASSERT_TRUE(v2.has_value() && v2->has_value()); EXPECT_EQ(**v2, 2u);
    auto v3 = dec.next(); ASSERT_TRUE(v3.has_value() && v3->has_value()); EXPECT_EQ(**v3, 3u);
    EXPECT_FALSE(dec.next().has_value());
}

TEST(RleEncoder, literal_then_run_round_trips) {
    auto enc = RleEncoder<std::uint64_t>{};
    enc.append(1); enc.append(2);  // literal of 2
    enc.append(3); enc.append(3); enc.append(3);  // run of 3
    enc.finish();

    auto dec = RleDecoder<std::uint64_t>{enc.data()};
    auto v1 = dec.next(); ASSERT_TRUE(v1.has_value() && v1->has_value()); EXPECT_EQ(**v1, 1u);
    auto v2 = dec.next(); ASSERT_TRUE(v2.has_value() && v2->has_value()); EXPECT_EQ(**v2, 2u);
    for (int i = 0; i < 3; ++i) {
        auto val = dec.next();
        ASSERT_TRUE(val.has_value() && val->has_value());
        EXPECT_EQ(**val, 3u);
    }
    EXPECT_FALSE(dec.next().has_value());
}

TEST(RleEncoder, run_literal_null_run_round_trips) {
    auto enc = RleEncoder<std::uint64_t>{};
    enc.append(5); enc.append(5);  // run of 2
    enc.append(6); enc.append(7);  // literal of 2
    enc.append_null(); enc.append_null();  // null of 2
    enc.append(8); enc.append(8); enc.append(8);  // run of 3
    enc.finish();

    auto dec = RleDecoder<std::uint64_t>{enc.data()};
    std::vector<std::optional<std::uint64_t>> results;
    while (auto val = dec.next()) results.push_back(*val);

    ASSERT_EQ(results.size(), 9u);
    EXPECT_TRUE(results[0].has_value()); EXPECT_EQ(*results[0], 5u);
    EXPECT_TRUE(results[1].has_value()); EXPECT_EQ(*results[1], 5u);
    EXPECT_TRUE(results[2].has_value()); EXPECT_EQ(*results[2], 6u);
    EXPECT_TRUE(results[3].has_value()); EXPECT_EQ(*results[3], 7u);
    EXPECT_FALSE(results[4].has_value());
    EXPECT_FALSE(results[5].has_value());
    EXPECT_TRUE(results[6].has_value()); EXPECT_EQ(*results[6], 8u);
    EXPECT_TRUE(results[7].has_value()); EXPECT_EQ(*results[7], 8u);
    EXPECT_TRUE(results[8].has_value()); EXPECT_EQ(*results[8], 8u);
}

TEST(RleEncoder, null_literal_null_run_null_round_trips) {
    auto enc = RleEncoder<std::uint64_t>{};
    enc.append_null();
    enc.append(1); enc.append(2);  // literal
    enc.append_null();
    enc.append(3); enc.append(3);  // run
    enc.append_null();
    enc.finish();

    auto dec = RleDecoder<std::uint64_t>{enc.data()};
    std::vector<std::optional<std::uint64_t>> results;
    while (auto val = dec.next()) results.push_back(*val);

    ASSERT_EQ(results.size(), 7u);
    EXPECT_FALSE(results[0].has_value());
    EXPECT_EQ(*results[1], 1u);
    EXPECT_EQ(*results[2], 2u);
    EXPECT_FALSE(results[3].has_value());
    EXPECT_EQ(*results[4], 3u);
    EXPECT_EQ(*results[5], 3u);
    EXPECT_FALSE(results[6].has_value());
}

// =============================================================================
// Two-element patterns
// =============================================================================

TEST(RleEncoder, two_same_values_round_trips) {
    auto enc = RleEncoder<std::uint64_t>{};
    enc.append(42);
    enc.append(42);
    enc.finish();

    auto dec = RleDecoder<std::uint64_t>{enc.data()};
    auto v1 = dec.next(); ASSERT_TRUE(v1.has_value() && v1->has_value()); EXPECT_EQ(**v1, 42u);
    auto v2 = dec.next(); ASSERT_TRUE(v2.has_value() && v2->has_value()); EXPECT_EQ(**v2, 42u);
    EXPECT_FALSE(dec.next().has_value());
}

TEST(RleEncoder, two_different_values_round_trips) {
    auto enc = RleEncoder<std::uint64_t>{};
    enc.append(1);
    enc.append(2);
    enc.finish();

    auto dec = RleDecoder<std::uint64_t>{enc.data()};
    auto v1 = dec.next(); ASSERT_TRUE(v1.has_value() && v1->has_value()); EXPECT_EQ(**v1, 1u);
    auto v2 = dec.next(); ASSERT_TRUE(v2.has_value() && v2->has_value()); EXPECT_EQ(**v2, 2u);
    EXPECT_FALSE(dec.next().has_value());
}

TEST(RleEncoder, two_nulls_round_trips) {
    auto enc = RleEncoder<std::uint64_t>{};
    enc.append_null();
    enc.append_null();
    enc.finish();

    auto dec = RleDecoder<std::uint64_t>{enc.data()};
    auto n1 = dec.next(); ASSERT_TRUE(n1.has_value()); EXPECT_FALSE(n1->has_value());
    auto n2 = dec.next(); ASSERT_TRUE(n2.has_value()); EXPECT_FALSE(n2->has_value());
    EXPECT_FALSE(dec.next().has_value());
}

TEST(RleEncoder, null_and_value_round_trips) {
    auto enc = RleEncoder<std::uint64_t>{};
    enc.append_null();
    enc.append(99);
    enc.finish();

    auto dec = RleDecoder<std::uint64_t>{enc.data()};
    auto n = dec.next(); ASSERT_TRUE(n.has_value()); EXPECT_FALSE(n->has_value());
    auto v = dec.next(); ASSERT_TRUE(v.has_value() && v->has_value()); EXPECT_EQ(**v, 99u);
    EXPECT_FALSE(dec.next().has_value());
}

TEST(RleEncoder, value_and_null_round_trips) {
    auto enc = RleEncoder<std::uint64_t>{};
    enc.append(99);
    enc.append_null();
    enc.finish();

    auto dec = RleDecoder<std::uint64_t>{enc.data()};
    auto v = dec.next(); ASSERT_TRUE(v.has_value() && v->has_value()); EXPECT_EQ(**v, 99u);
    auto n = dec.next(); ASSERT_TRUE(n.has_value()); EXPECT_FALSE(n->has_value());
    EXPECT_FALSE(dec.next().has_value());
}

// =============================================================================
// Large runs and compression
// =============================================================================

TEST(RleEncoder, large_null_run_round_trips) {
    auto enc = RleEncoder<std::uint64_t>{};
    for (int i = 0; i < 10000; ++i) enc.append_null();
    enc.finish();

    auto dec = RleDecoder<std::uint64_t>{enc.data()};
    for (int i = 0; i < 10000; ++i) {
        auto val = dec.next();
        ASSERT_TRUE(val.has_value()) << "at i=" << i;
        EXPECT_FALSE(val->has_value()) << "at i=" << i;
    }
    EXPECT_FALSE(dec.next().has_value());

    // Null run should be very compact
    EXPECT_LT(enc.data().size(), 10u);
}

TEST(RleEncoder, large_literal_run_round_trips) {
    auto enc = RleEncoder<std::uint64_t>{};
    for (std::uint64_t i = 0; i < 100; ++i) enc.append(i);
    enc.finish();

    auto dec = RleDecoder<std::uint64_t>{enc.data()};
    for (std::uint64_t i = 0; i < 100; ++i) {
        auto val = dec.next();
        ASSERT_TRUE(val.has_value() && val->has_value()) << "at i=" << i;
        EXPECT_EQ(**val, i) << "at i=" << i;
    }
    EXPECT_FALSE(dec.next().has_value());
}

TEST(RleEncoder, many_short_runs_round_trip) {
    auto enc = RleEncoder<std::uint64_t>{};
    for (std::uint64_t v = 0; v < 50; ++v) {
        enc.append(v);
        enc.append(v);
    }
    enc.finish();

    auto dec = RleDecoder<std::uint64_t>{enc.data()};
    for (std::uint64_t v = 0; v < 50; ++v) {
        auto v1 = dec.next();
        ASSERT_TRUE(v1.has_value() && v1->has_value()) << "first at v=" << v;
        EXPECT_EQ(**v1, v);
        auto v2 = dec.next();
        ASSERT_TRUE(v2.has_value() && v2->has_value()) << "second at v=" << v;
        EXPECT_EQ(**v2, v);
    }
    EXPECT_FALSE(dec.next().has_value());
}

// =============================================================================
// Decoder edge cases
// =============================================================================

TEST(RleDecoder, empty_data_returns_nullopt) {
    auto dec = RleDecoder<std::uint64_t>{std::span<const std::byte>{}};
    EXPECT_FALSE(dec.next().has_value());
}

TEST(RleDecoder, empty_data_done) {
    auto dec = RleDecoder<std::uint64_t>{std::span<const std::byte>{}};
    EXPECT_TRUE(dec.done());
}

TEST(RleDecoder, done_after_consuming_all) {
    auto enc = RleEncoder<std::uint64_t>{};
    enc.append(1);
    enc.append(2);
    enc.finish();

    auto dec = RleDecoder<std::uint64_t>{enc.data()};
    EXPECT_FALSE(dec.done());
    dec.next();
    dec.next();
    dec.next();  // past end
    EXPECT_TRUE(dec.done());
}

TEST(RleDecoder, done_during_run) {
    auto enc = RleEncoder<std::uint64_t>{};
    enc.append(1); enc.append(1); enc.append(1);
    enc.finish();

    auto dec = RleDecoder<std::uint64_t>{enc.data()};
    dec.next();  // consumed 1 of 3
    EXPECT_FALSE(dec.done());
    dec.next();
    dec.next();
    dec.next();  // past end
    EXPECT_TRUE(dec.done());
}

TEST(RleDecoder, done_during_null_run) {
    auto enc = RleEncoder<std::uint64_t>{};
    enc.append_null(); enc.append_null();
    enc.finish();

    auto dec = RleDecoder<std::uint64_t>{enc.data()};
    dec.next();  // consumed 1 of 2
    EXPECT_FALSE(dec.done());
    dec.next();
    dec.next();  // past end
    EXPECT_TRUE(dec.done());
}

// =============================================================================
// Stress / property-style tests
// =============================================================================

TEST(RleEncoder, all_unique_values_round_trip) {
    // Every value is different — all literals
    auto enc = RleEncoder<std::uint64_t>{};
    std::vector<std::uint64_t> expected;
    for (std::uint64_t i = 0; i < 200; ++i) {
        expected.push_back(i * 7 + 3);
        enc.append(i * 7 + 3);
    }
    enc.finish();

    auto dec = RleDecoder<std::uint64_t>{enc.data()};
    for (std::size_t i = 0; i < expected.size(); ++i) {
        auto val = dec.next();
        ASSERT_TRUE(val.has_value() && val->has_value()) << "at i=" << i;
        EXPECT_EQ(**val, expected[i]) << "at i=" << i;
    }
    EXPECT_FALSE(dec.next().has_value());
}

TEST(RleEncoder, repeating_pattern_round_trips) {
    // Pattern: 0,0,1,1,2,2,...,9,9 repeated
    auto enc = RleEncoder<std::uint64_t>{};
    std::vector<std::uint64_t> expected;
    for (int repeat = 0; repeat < 3; ++repeat) {
        for (std::uint64_t v = 0; v < 10; ++v) {
            enc.append(v);
            enc.append(v);
            expected.push_back(v);
            expected.push_back(v);
        }
    }
    enc.finish();

    auto dec = RleDecoder<std::uint64_t>{enc.data()};
    for (std::size_t i = 0; i < expected.size(); ++i) {
        auto val = dec.next();
        ASSERT_TRUE(val.has_value() && val->has_value()) << "at i=" << i;
        EXPECT_EQ(**val, expected[i]) << "at i=" << i;
    }
    EXPECT_FALSE(dec.next().has_value());
}

TEST(RleEncoder, mixed_nulls_runs_literals_stress) {
    // Complex interleaving: null(3), run(5x42), literal(1,2,3), null(1), run(2x0)
    auto enc = RleEncoder<std::uint64_t>{};
    std::vector<std::optional<std::uint64_t>> expected;

    for (int i = 0; i < 3; ++i) { enc.append_null(); expected.push_back(std::nullopt); }
    for (int i = 0; i < 5; ++i) { enc.append(42); expected.push_back(42u); }
    enc.append(1); expected.push_back(1u);
    enc.append(2); expected.push_back(2u);
    enc.append(3); expected.push_back(3u);
    enc.append_null(); expected.push_back(std::nullopt);
    enc.append(0); expected.push_back(0u);
    enc.append(0); expected.push_back(0u);
    enc.finish();

    auto dec = RleDecoder<std::uint64_t>{enc.data()};
    for (std::size_t i = 0; i < expected.size(); ++i) {
        auto val = dec.next();
        ASSERT_TRUE(val.has_value()) << "at i=" << i;
        if (expected[i].has_value()) {
            ASSERT_TRUE(val->has_value()) << "expected value at i=" << i;
            EXPECT_EQ(**val, *expected[i]) << "at i=" << i;
        } else {
            EXPECT_FALSE(val->has_value()) << "expected null at i=" << i;
        }
    }
    EXPECT_FALSE(dec.next().has_value());
}

TEST(RleEncoder, string_mixed_nulls_runs_literals_stress) {
    auto enc = RleEncoder<std::string>{};
    std::vector<std::optional<std::string>> expected;

    // null, null
    enc.append_null(); expected.push_back(std::nullopt);
    enc.append_null(); expected.push_back(std::nullopt);
    // run of 3 "count"
    enc.append("count"); expected.push_back("count");
    enc.append("count"); expected.push_back("count");
    enc.append("count"); expected.push_back("count");
    // literal "a", "b"
    enc.append("a"); expected.push_back("a");
    enc.append("b"); expected.push_back("b");
    // null
    enc.append_null(); expected.push_back(std::nullopt);
    // single "end"
    enc.append("end"); expected.push_back("end");
    enc.finish();

    auto dec = RleDecoder<std::string>{enc.data()};
    for (std::size_t i = 0; i < expected.size(); ++i) {
        auto val = dec.next();
        ASSERT_TRUE(val.has_value()) << "at i=" << i;
        if (expected[i].has_value()) {
            ASSERT_TRUE(val->has_value()) << "expected value at i=" << i;
            EXPECT_EQ(**val, *expected[i]) << "at i=" << i;
        } else {
            EXPECT_FALSE(val->has_value()) << "expected null at i=" << i;
        }
    }
    EXPECT_FALSE(dec.next().has_value());
}

// =============================================================================
// Encoding compactness checks
// =============================================================================

TEST(RleEncoder, run_is_more_compact_than_literals) {
    auto run_enc = RleEncoder<std::uint64_t>{};
    for (int i = 0; i < 100; ++i) run_enc.append(42);
    run_enc.finish();

    auto lit_enc = RleEncoder<std::uint64_t>{};
    for (std::uint64_t i = 0; i < 100; ++i) lit_enc.append(i);
    lit_enc.finish();

    EXPECT_LT(run_enc.data().size(), lit_enc.data().size());
}

TEST(RleEncoder, null_run_is_very_compact) {
    auto enc = RleEncoder<std::uint64_t>{};
    for (int i = 0; i < 1000; ++i) enc.append_null();
    enc.finish();

    // Should just be: control(0) + count(1000) = ~4 bytes
    EXPECT_LE(enc.data().size(), 6u);
}

TEST(RleEncoder, take_returns_data_and_clears) {
    auto enc = RleEncoder<std::uint64_t>{};
    enc.append(1);
    enc.finish();
    auto d = enc.take();
    EXPECT_FALSE(d.empty());
    // After take, internal data should be moved (empty)
    EXPECT_TRUE(enc.data().empty());
}

// =============================================================================
// Patterns matching columnar op encoding
// =============================================================================

// These patterns mirror what the change_op_columns encoder produces for common
// op sequences, which is where the serializer bug was found.

TEST(RleEncoder, single_null_uint64_matches_map_key_actor) {
    // When a single op has a map key, key_actor column gets one null
    auto enc = RleEncoder<std::uint64_t>{};
    enc.append_null();
    enc.finish();

    EXPECT_FALSE(enc.data().empty());

    auto dec = RleDecoder<std::uint64_t>{enc.data()};
    auto val = dec.next();
    ASSERT_TRUE(val.has_value());
    EXPECT_FALSE(val->has_value());  // null
    EXPECT_FALSE(dec.next().has_value());
}

TEST(RleEncoder, single_string_matches_map_key_string) {
    // When a single op has a map key "count", key_string column gets one "count"
    auto enc = RleEncoder<std::string>{};
    enc.append("count");
    enc.finish();

    EXPECT_FALSE(enc.data().empty());

    auto dec = RleDecoder<std::string>{enc.data()};
    auto val = dec.next();
    ASSERT_TRUE(val.has_value());
    ASSERT_TRUE(val->has_value());
    EXPECT_EQ(**val, "count");
    EXPECT_FALSE(dec.next().has_value());
}

TEST(RleEncoder, two_nulls_then_string_matches_list_then_map_keys) {
    // Pattern: 2 list ops (null key_string), then 1 map op (string key_string)
    auto enc = RleEncoder<std::string>{};
    enc.append_null();
    enc.append_null();
    enc.append("name");
    enc.finish();

    auto dec = RleDecoder<std::string>{enc.data()};
    auto n1 = dec.next(); ASSERT_TRUE(n1.has_value()); EXPECT_FALSE(n1->has_value());
    auto n2 = dec.next(); ASSERT_TRUE(n2.has_value()); EXPECT_FALSE(n2->has_value());
    auto v = dec.next(); ASSERT_TRUE(v.has_value() && v->has_value()); EXPECT_EQ(**v, "name");
    EXPECT_FALSE(dec.next().has_value());
}

TEST(RleEncoder, multiple_different_strings_round_trip) {
    // Pattern: ops with different map keys
    auto enc = RleEncoder<std::string>{};
    enc.append("config");
    enc.append("count");
    enc.finish();

    auto dec = RleDecoder<std::string>{enc.data()};
    auto v1 = dec.next(); ASSERT_TRUE(v1.has_value() && v1->has_value()); EXPECT_EQ(**v1, "config");
    auto v2 = dec.next(); ASSERT_TRUE(v2.has_value() && v2->has_value()); EXPECT_EQ(**v2, "count");
    EXPECT_FALSE(dec.next().has_value());
}

// =============================================================================
// DeltaEncoder-specific round-trip patterns via RLE
// =============================================================================

TEST(RleEncoder, int64_all_same_deltas) {
    // Simulates what DeltaEncoder produces for monotonic sequences
    // (all deltas are 1)
    auto enc = RleEncoder<std::int64_t>{};
    for (int i = 0; i < 10; ++i) enc.append(1);
    enc.finish();

    auto dec = RleDecoder<std::int64_t>{enc.data()};
    for (int i = 0; i < 10; ++i) {
        auto val = dec.next();
        ASSERT_TRUE(val.has_value() && val->has_value());
        EXPECT_EQ(**val, 1);
    }
    EXPECT_FALSE(dec.next().has_value());
}

TEST(RleEncoder, int64_single_delta_null_delta) {
    // Delta pattern: value, null, value (like obj_counter with root then non-root)
    auto enc = RleEncoder<std::int64_t>{};
    enc.append(0);      // delta for root obj_counter
    enc.append_null();  // null for something
    enc.append(1);      // delta for non-root
    enc.finish();

    auto dec = RleDecoder<std::int64_t>{enc.data()};
    auto v1 = dec.next(); ASSERT_TRUE(v1.has_value() && v1->has_value()); EXPECT_EQ(**v1, 0);
    auto n = dec.next(); ASSERT_TRUE(n.has_value()); EXPECT_FALSE(n->has_value());
    auto v2 = dec.next(); ASSERT_TRUE(v2.has_value() && v2->has_value()); EXPECT_EQ(**v2, 1);
    EXPECT_FALSE(dec.next().has_value());
}
