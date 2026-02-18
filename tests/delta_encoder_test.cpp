#include "../src/encoding/delta_encoder.hpp"
#include "../src/encoding/boolean_encoder.hpp"

#include <gtest/gtest.h>

using namespace automerge_cpp::encoding;

// -- Delta encoder tests ------------------------------------------------------

TEST(DeltaEncoder, empty_produces_no_bytes) {
    auto enc = DeltaEncoder{};
    enc.finish();
    EXPECT_TRUE(enc.data().empty());
}

TEST(DeltaEncoder, single_value_round_trips) {
    auto enc = DeltaEncoder{};
    enc.append(42);
    enc.finish();

    auto dec = DeltaDecoder{enc.data()};
    auto val = dec.next();
    ASSERT_TRUE(val.has_value());
    ASSERT_TRUE(val->has_value());
    EXPECT_EQ(**val, 42);
    EXPECT_FALSE(dec.next().has_value());
}

TEST(DeltaEncoder, monotonic_sequence_round_trips) {
    auto enc = DeltaEncoder{};
    enc.append(1);
    enc.append(2);
    enc.append(3);
    enc.append(4);
    enc.append(5);
    enc.finish();

    auto dec = DeltaDecoder{enc.data()};
    for (std::int64_t i = 1; i <= 5; ++i) {
        auto val = dec.next();
        ASSERT_TRUE(val.has_value() && val->has_value());
        EXPECT_EQ(**val, i);
    }
    EXPECT_FALSE(dec.next().has_value());
}

TEST(DeltaEncoder, monotonic_sequence_compresses_well) {
    auto enc = DeltaEncoder{};
    for (int i = 1; i <= 100; ++i) enc.append(i);
    enc.finish();

    // All deltas are 1, so RLE should compress this to very few bytes
    EXPECT_LT(enc.data().size(), 10u);

    // Verify round-trip
    auto dec = DeltaDecoder{enc.data()};
    for (std::int64_t i = 1; i <= 100; ++i) {
        auto val = dec.next();
        ASSERT_TRUE(val.has_value() && val->has_value());
        EXPECT_EQ(**val, i);
    }
    EXPECT_FALSE(dec.next().has_value());
}

TEST(DeltaEncoder, negative_deltas_round_trip) {
    auto enc = DeltaEncoder{};
    enc.append(10);
    enc.append(5);
    enc.append(0);
    enc.append(-3);
    enc.finish();

    auto dec = DeltaDecoder{enc.data()};

    auto v1 = dec.next();
    ASSERT_TRUE(v1.has_value() && v1->has_value());
    EXPECT_EQ(**v1, 10);

    auto v2 = dec.next();
    ASSERT_TRUE(v2.has_value() && v2->has_value());
    EXPECT_EQ(**v2, 5);

    auto v3 = dec.next();
    ASSERT_TRUE(v3.has_value() && v3->has_value());
    EXPECT_EQ(**v3, 0);

    auto v4 = dec.next();
    ASSERT_TRUE(v4.has_value() && v4->has_value());
    EXPECT_EQ(**v4, -3);

    EXPECT_FALSE(dec.next().has_value());
}

TEST(DeltaEncoder, nulls_round_trip) {
    auto enc = DeltaEncoder{};
    enc.append(10);
    enc.append_null();
    enc.append(20);
    enc.finish();

    auto dec = DeltaDecoder{enc.data()};

    auto v1 = dec.next();
    ASSERT_TRUE(v1.has_value() && v1->has_value());
    EXPECT_EQ(**v1, 10);

    auto v2 = dec.next();
    ASSERT_TRUE(v2.has_value());
    EXPECT_FALSE(v2->has_value());  // null

    auto v3 = dec.next();
    ASSERT_TRUE(v3.has_value() && v3->has_value());
    EXPECT_EQ(**v3, 20);

    EXPECT_FALSE(dec.next().has_value());
}

TEST(DeltaEncoder, large_gaps_round_trip) {
    auto enc = DeltaEncoder{};
    enc.append(0);
    enc.append(1000000);
    enc.append(1000001);
    enc.finish();

    auto dec = DeltaDecoder{enc.data()};

    auto v1 = dec.next();
    ASSERT_TRUE(v1.has_value() && v1->has_value());
    EXPECT_EQ(**v1, 0);

    auto v2 = dec.next();
    ASSERT_TRUE(v2.has_value() && v2->has_value());
    EXPECT_EQ(**v2, 1000000);

    auto v3 = dec.next();
    ASSERT_TRUE(v3.has_value() && v3->has_value());
    EXPECT_EQ(**v3, 1000001);

    EXPECT_FALSE(dec.next().has_value());
}

// -- Boolean encoder tests ----------------------------------------------------

TEST(BooleanEncoder, empty_produces_no_bytes) {
    auto enc = BooleanEncoder{};
    enc.finish();
    EXPECT_TRUE(enc.data().empty());
}

TEST(BooleanEncoder, all_false_round_trips) {
    auto enc = BooleanEncoder{};
    enc.append(false);
    enc.append(false);
    enc.append(false);
    enc.finish();

    auto dec = BooleanDecoder{enc.data()};
    for (int i = 0; i < 3; ++i) {
        auto val = dec.next();
        ASSERT_TRUE(val.has_value());
        EXPECT_FALSE(*val);
    }
    EXPECT_FALSE(dec.next().has_value());
}

TEST(BooleanEncoder, all_true_round_trips) {
    auto enc = BooleanEncoder{};
    enc.append(true);
    enc.append(true);
    enc.append(true);
    enc.finish();

    auto dec = BooleanDecoder{enc.data()};
    for (int i = 0; i < 3; ++i) {
        auto val = dec.next();
        ASSERT_TRUE(val.has_value());
        EXPECT_TRUE(*val);
    }
    EXPECT_FALSE(dec.next().has_value());
}

TEST(BooleanEncoder, alternating_values_round_trip) {
    auto enc = BooleanEncoder{};
    enc.append(false);
    enc.append(true);
    enc.append(false);
    enc.append(true);
    enc.finish();

    auto dec = BooleanDecoder{enc.data()};
    auto expected = std::vector<bool>{false, true, false, true};
    for (auto e : expected) {
        auto val = dec.next();
        ASSERT_TRUE(val.has_value());
        EXPECT_EQ(*val, e);
    }
    EXPECT_FALSE(dec.next().has_value());
}

TEST(BooleanEncoder, runs_compress_well) {
    auto enc = BooleanEncoder{};
    for (int i = 0; i < 100; ++i) enc.append(false);
    for (int i = 0; i < 100; ++i) enc.append(true);
    for (int i = 0; i < 100; ++i) enc.append(false);
    enc.finish();

    // Should be very small: three counts
    EXPECT_LT(enc.data().size(), 10u);

    auto dec = BooleanDecoder{enc.data()};
    for (int i = 0; i < 100; ++i) {
        auto val = dec.next();
        ASSERT_TRUE(val.has_value());
        EXPECT_FALSE(*val);
    }
    for (int i = 0; i < 100; ++i) {
        auto val = dec.next();
        ASSERT_TRUE(val.has_value());
        EXPECT_TRUE(*val);
    }
    for (int i = 0; i < 100; ++i) {
        auto val = dec.next();
        ASSERT_TRUE(val.has_value());
        EXPECT_FALSE(*val);
    }
    EXPECT_FALSE(dec.next().has_value());
}
