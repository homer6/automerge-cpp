#include <automerge-cpp/types.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <unordered_set>
#include <vector>

using namespace automerge_cpp;

// -- ActorId ------------------------------------------------------------------

TEST(ActorId, default_constructed_is_all_zeros) {
    const auto id = ActorId{};
    EXPECT_TRUE(id.is_zero());
}

TEST(ActorId, constructed_from_raw_bytes) {
    const std::uint8_t raw[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    const auto id = ActorId{raw};

    EXPECT_FALSE(id.is_zero());
    EXPECT_EQ(id.bytes[0], std::byte{1});
    EXPECT_EQ(id.bytes[15], std::byte{16});
}

TEST(ActorId, equality) {
    const std::uint8_t raw[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    const auto a = ActorId{raw};
    const auto b = ActorId{raw};
    const auto c = ActorId{};

    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

TEST(ActorId, ordering_is_lexicographic_on_bytes) {
    const std::uint8_t low[16]  = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
    const std::uint8_t high[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2};
    const auto a = ActorId{low};
    const auto b = ActorId{high};

    EXPECT_LT(a, b);
    EXPECT_GT(b, a);
    EXPECT_LE(a, a);
    EXPECT_GE(b, b);
}

TEST(ActorId, ordering_higher_byte_dominates) {
    const std::uint8_t a_raw[16] = {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    const std::uint8_t b_raw[16] = {0,255,255,255,255,255,255,255,
                                     255,255,255,255,255,255,255,255};
    const auto a = ActorId{a_raw};
    const auto b = ActorId{b_raw};

    EXPECT_GT(a, b);
}

TEST(ActorId, hashable_and_usable_in_unordered_set) {
    const std::uint8_t r1[16] = {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    const std::uint8_t r2[16] = {2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};

    auto set = std::unordered_set<ActorId>{};
    set.insert(ActorId{r1});
    set.insert(ActorId{r2});
    set.insert(ActorId{r1});  // duplicate

    EXPECT_EQ(set.size(), 2u);
}

TEST(ActorId, sortable) {
    const std::uint8_t r1[16] = {3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    const std::uint8_t r2[16] = {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    const std::uint8_t r3[16] = {2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};

    auto ids = std::vector{ActorId{r1}, ActorId{r2}, ActorId{r3}};
    std::ranges::sort(ids);

    EXPECT_EQ(ids[0], ActorId{r2});
    EXPECT_EQ(ids[1], ActorId{r3});
    EXPECT_EQ(ids[2], ActorId{r1});
}

// -- ChangeHash ---------------------------------------------------------------

TEST(ChangeHash, default_constructed_is_all_zeros) {
    const auto h = ChangeHash{};
    EXPECT_TRUE(h.is_zero());
}

TEST(ChangeHash, equality_and_ordering) {
    const std::uint8_t r1[32] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
                                  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
    const std::uint8_t r2[32] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
                                  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2};
    const auto a = ChangeHash{r1};
    const auto b = ChangeHash{r2};

    EXPECT_NE(a, b);
    EXPECT_LT(a, b);
}

TEST(ChangeHash, hashable_and_usable_in_unordered_set) {
    const std::uint8_t r1[32] = {1};
    const std::uint8_t r2[32] = {2};

    auto set = std::unordered_set<ChangeHash>{};
    set.insert(ChangeHash{r1});
    set.insert(ChangeHash{r2});
    set.insert(ChangeHash{r1});

    EXPECT_EQ(set.size(), 2u);
}

// -- OpId ---------------------------------------------------------------------

TEST(OpId, default_constructed) {
    const auto id = OpId{};
    EXPECT_EQ(id.counter, 0u);
    EXPECT_TRUE(id.actor.is_zero());
}

TEST(OpId, ordering_by_counter_first) {
    const auto actor = ActorId{};
    const auto a = OpId{1, actor};
    const auto b = OpId{2, actor};

    EXPECT_LT(a, b);
}

TEST(OpId, ordering_by_actor_when_counter_equal) {
    const std::uint8_t r1[16] = {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    const std::uint8_t r2[16] = {2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};

    const auto a = OpId{5, ActorId{r1}};
    const auto b = OpId{5, ActorId{r2}};

    EXPECT_LT(a, b);
}

TEST(OpId, equality) {
    const std::uint8_t raw[16] = {7,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    const auto a = OpId{10, ActorId{raw}};
    const auto b = OpId{10, ActorId{raw}};

    EXPECT_EQ(a, b);
}

TEST(OpId, hashable) {
    const auto a = OpId{1, ActorId{}};
    const auto b = OpId{2, ActorId{}};

    auto set = std::unordered_set<OpId>{};
    set.insert(a);
    set.insert(b);
    set.insert(a);

    EXPECT_EQ(set.size(), 2u);
}

// -- ObjId --------------------------------------------------------------------

TEST(ObjId, default_is_root) {
    const auto id = ObjId{};
    EXPECT_TRUE(id.is_root());
}

TEST(ObjId, constructed_from_op_id_is_not_root) {
    const auto id = ObjId{OpId{1, ActorId{}}};
    EXPECT_FALSE(id.is_root());
}

TEST(ObjId, root_constant) {
    EXPECT_TRUE(root.is_root());
    EXPECT_EQ(ObjId{}, root);
}

TEST(ObjId, equality) {
    const auto a = ObjId{OpId{1, ActorId{}}};
    const auto b = ObjId{OpId{1, ActorId{}}};
    const auto c = ObjId{OpId{2, ActorId{}}};

    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
    EXPECT_NE(a, root);
}

// -- Prop ---------------------------------------------------------------------

TEST(Prop, map_key_holds_string) {
    const auto p = map_key("name");
    EXPECT_TRUE(std::holds_alternative<std::string>(p));
    EXPECT_EQ(std::get<std::string>(p), "name");
}

TEST(Prop, list_index_holds_size_t) {
    const auto p = list_index(42);
    EXPECT_TRUE(std::holds_alternative<std::size_t>(p));
    EXPECT_EQ(std::get<std::size_t>(p), 42u);
}

TEST(Prop, map_key_and_list_index_are_distinct) {
    const auto key = map_key("0");
    const auto idx = list_index(0);

    // Different alternatives in the variant — always unequal
    EXPECT_NE(key, idx);
}
