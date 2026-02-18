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
