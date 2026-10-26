#include <mimo/memory/BitMapArena.hpp>
#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <random>
#include <stdexcept>
#include <type_traits>
#include <unordered_set>
#include <vector>

namespace {
struct Tracked {
    static inline int alive = 0;
    static inline int destroyed = 0;
    explicit Tracked(bool fail = false)
    {
        if (fail) throw std::runtime_error("constructor failure");
        ++alive;
    }
    ~Tracked() { --alive; ++destroyed; }
};

TEST(BitMapArenaTest, ZeroCapacity)
{
    mimo::memory::BitMapArena<int> arena(0);
    EXPECT_EQ(arena.create(1), nullptr);
}

TEST(BitMapArenaTest, CapacityBoundaries)
{
    for (const auto capacity : {1u, 2u, 7u, 8u, 9u, 15u, 16u, 17u, 63u, 64u, 65u}) {
        SCOPED_TRACE(::testing::Message() << "capacity=" << capacity);
        mimo::memory::BitMapArena<int> arena(capacity);
        std::unordered_set<int*> addresses;
        for (unsigned i = 0; i < capacity; ++i) {
            auto* p = arena.create(static_cast<int>(i));
            ASSERT_NE(p, nullptr) << "allocation failed before capacity";
            ASSERT_TRUE(addresses.insert(p).second) << "live allocations overlap";
            EXPECT_EQ(*p, static_cast<int>(i));
        }
        EXPECT_EQ(arena.create(42), nullptr);
        EXPECT_EQ(arena.create(42), nullptr) << "repeated exhaustion changed state";
    }
}

TEST(BitMapArenaTest, Reuse)
{
    mimo::memory::BitMapArena<int> arena(17);
    std::array<int*, 17> pointers{};
    for (auto& p : pointers) {
        p = arena.create(12);
        ASSERT_NE(p, nullptr);
    }
    // Exercise holes on either side of bitmap byte boundaries.
    for (const auto index : {0, 7, 8, 16}) {
        SCOPED_TRACE(::testing::Message() << "slot=" << index);
        arena.destroy(pointers[index]);
        auto* replacement = arena.create(99);
        ASSERT_NE(replacement, nullptr);
        EXPECT_EQ(replacement, pointers[index]) << "sole free slot was not reused";
        EXPECT_EQ(*replacement, 99);
    }
    EXPECT_EQ(arena.create(), nullptr);
}

TEST(BitMapArenaTest, Lifetime)
{
    Tracked::alive = Tracked::destroyed = 0;
    {
        mimo::memory::BitMapArena<Tracked> arena(9);
        auto* p = arena.create();
        ASSERT_NE(p, nullptr);
        ASSERT_NE(arena.create(), nullptr);
        EXPECT_EQ(Tracked::alive, 2);
        arena.destroy(p);
        EXPECT_EQ(Tracked::alive, 1);
        EXPECT_EQ(Tracked::destroyed, 1);
        ASSERT_NE(arena.create(), nullptr);
    }
    EXPECT_EQ(Tracked::alive, 0);
    EXPECT_EQ(Tracked::destroyed, 3);
}

TEST(BitMapArenaTest, Forwarding)
{
    struct Value {
        std::unique_ptr<int> owned;
        int& reference;
        Value(std::unique_ptr<int> p, int& ref) : owned(std::move(p)), reference(ref) {}
    };
    int number = 23;
    auto owned = std::make_unique<int>(42);
    mimo::memory::BitMapArena<Value> arena(1);
    auto* p = arena.create(std::move(owned), number);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(owned, nullptr);
    ASSERT_NE(p->owned, nullptr);
    EXPECT_EQ(*p->owned, 42);
    EXPECT_EQ(&p->reference, &number);
}

TEST(BitMapArenaTest, Alignment)
{
    struct alignas(256) Aligned { int value; };
    mimo::memory::BitMapArena<Aligned> arena(9);
    for (int i = 0; i < 9; ++i) {
        auto* p = arena.create();
        ASSERT_NE(p, nullptr);
        EXPECT_EQ(reinterpret_cast<std::uintptr_t>(p) % alignof(Aligned), 0u);
    }
}

TEST(BitMapArenaTest, ConstructorFailure)
{
    mimo::memory::BitMapArena<Tracked> arena(1);
    EXPECT_THROW(arena.create(true), std::runtime_error);
    EXPECT_NE(arena.create(), nullptr) << "throwing constructor consumed capacity";
}

TEST(BitMapArenaTest, FailedLifetime)
{
    Tracked::alive = Tracked::destroyed = 0;
    {
        mimo::memory::BitMapArena<Tracked> arena(1);
        EXPECT_THROW(arena.create(true), std::runtime_error);
    }
    EXPECT_EQ(Tracked::alive, 0);
    EXPECT_EQ(Tracked::destroyed, 0) << "unconstructed object was destroyed";
}

TEST(BitMapArenaTest, Overflow)
{
    struct Large { std::array<std::byte, 256> data; };
    EXPECT_THROW({
        mimo::memory::BitMapArena<Large> arena(std::numeric_limits<std::size_t>::max() / sizeof(Large) + 1);
    }, std::bad_alloc);
}

TEST(BitMapArenaTest, Randomized)
{
    constexpr std::size_t capacity = 65;
    mimo::memory::BitMapArena<int> arena(capacity);
    std::vector<std::pair<int*, int>> live;
    std::mt19937 random(0xB17);
    for (int step = 0; step < 20000; ++step) {
        SCOPED_TRACE(::testing::Message() << "step=" << step);
        if (!live.empty() && random() % 2 == 0) {
            const auto index = random() % live.size();
            arena.destroy(live[index].first);
            live.erase(live.begin() + static_cast<std::ptrdiff_t>(index));
        } else {
            auto* p = arena.create(step);
            if (live.size() == capacity) EXPECT_EQ(p, nullptr);
            else {
                ASSERT_NE(p, nullptr) << "model expected available capacity";
                for (const auto& entry : live) ASSERT_NE(p, entry.first) << "duplicate live address";
                live.emplace_back(p, step);
            }
        }
        for (const auto& [p, value] : live) ASSERT_EQ(*p, value) << "live value corrupted";
    }
    for (const auto& entry : live) arena.destroy(entry.first);
    for (std::size_t i = 0; i < capacity; ++i) ASSERT_NE(arena.create(1), nullptr);
    EXPECT_EQ(arena.create(1), nullptr);
}

TEST(BitMapArenaTest, TypeTraits)
{
    using Arena = mimo::memory::BitMapArena<int>;
    static_assert(!std::is_default_constructible_v<Arena>);
    static_assert(!std::is_copy_constructible_v<Arena> && !std::is_copy_assignable_v<Arena>);
    static_assert(!std::is_move_constructible_v<Arena> && !std::is_move_assignable_v<Arena>);
    static_assert(noexcept(std::declval<Arena&>().destroy(nullptr)));
}
} // namespace
