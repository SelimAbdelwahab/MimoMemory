#include <mimo/memory/HierarchicalBitMapArena.hpp>
#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <memory>
#include <random>
#include <stdexcept>
#include <vector>

namespace {
using mimo::memory::HierarchicalBitMapArena;

class HierarchicalCapacityTest : public testing::TestWithParam<std::size_t> {};

TEST_P(HierarchicalCapacityTest, FillAndReopenBranches)
{
    const auto capacity = GetParam();
    HierarchicalBitMapArena<int> arena(capacity);
    std::vector<int*> slots;
    slots.reserve(capacity);
    for (std::size_t i = 0; i < capacity; ++i) {
        auto* p = arena.create(static_cast<int>(i));
        ASSERT_NE(p, nullptr) << "slot=" << i;
        if (!slots.empty()) EXPECT_EQ(p, slots.front() + i);
        slots.push_back(p);
    }
    EXPECT_EQ(arena.create(), nullptr);
    EXPECT_EQ(arena.create(), nullptr);

    // Reopening a full branch must clear every necessary ancestor, including the root.
    for (const auto index : {capacity - 1, capacity / 2, std::size_t{0}}) {
        arena.destroy(slots[index]);
        auto* p = arena.create(42);
        ASSERT_EQ(p, slots[index]) << "slot=" << index;
        EXPECT_EQ(*p, 42);
        EXPECT_EQ(arena.create(), nullptr);
    }
}

INSTANTIATE_TEST_SUITE_P(
    BitmapBoundaries, HierarchicalCapacityTest,
    testing::Values(std::size_t{1}, 63, 64, 65, 127, 128, 129, 4095, 4096, 4097, 8192, 262145));

TEST(HierarchicalBitMapArenaTest, ReusesLowestFreeSlotAcrossBranches)
{
    constexpr std::size_t capacity = 8193;
    HierarchicalBitMapArena<int> arena(capacity);
    std::vector<int*> slots(capacity);
    for (auto& slot : slots) {
        slot = arena.create(1);
        ASSERT_NE(slot, nullptr);
    }

    const std::vector<std::size_t> holes{0, 63, 64, 65, 4095, 4096, 4097, 8192};
    for (auto index = holes.rbegin(); index != holes.rend(); ++index)
        arena.destroy(slots[*index]);
    for (const auto index : holes)
        EXPECT_EQ(arena.create(2), slots[index]);
    EXPECT_EQ(arena.create(), nullptr);
}

TEST(HierarchicalBitMapArenaTest, RandomizedAllocationMatchesReference)
{
    constexpr std::size_t capacity = 4097;
    HierarchicalBitMapArena<int> arena(capacity);
    std::vector<int*> slots(capacity);
    std::vector<int> values(capacity);
    // Start full so random operations exercise transitions in upper summaries too.
    for (std::size_t i = 0; i < capacity; ++i) {
        slots[i] = arena.create(static_cast<int>(i));
        ASSERT_NE(slots[i], nullptr);
        values[i] = static_cast<int>(i);
    }
    int* const base = slots.front();
    std::mt19937 random(0xB17);
    for (int step = 0; step < 20000; ++step) {
        if (random() % 2 == 0) {
            const auto index = random() % capacity;
            if (slots[index] != nullptr) {
                EXPECT_EQ(*slots[index], values[index]);
                arena.destroy(slots[index]);
                slots[index] = nullptr;
            }
        } else {
            std::size_t expected = 0;
            while (expected < capacity && slots[expected] != nullptr) ++expected;
            auto* p = arena.create(step);
            if (expected == capacity) {
                EXPECT_EQ(p, nullptr);
            } else {
                ASSERT_EQ(p, base + expected) << "step=" << step;
                slots[expected] = p;
                values[expected] = step;
            }
        }
    }
    for (std::size_t i = 0; i < capacity; ++i)
        if (slots[i] != nullptr) EXPECT_EQ(*slots[i], values[i]);
}

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

TEST(HierarchicalBitMapArenaTest, ConstructorFailureReopensFullAncestors)
{
    Tracked::alive = Tracked::destroyed = 0;
    {
        HierarchicalBitMapArena<Tracked> arena(4097);
        for (int i = 0; i < 4096; ++i) ASSERT_NE(arena.create(), nullptr);
        EXPECT_THROW(arena.create(true), std::runtime_error);
        EXPECT_EQ(Tracked::alive, 4096);
        EXPECT_EQ(Tracked::destroyed, 0);
        auto* last = arena.create();
        ASSERT_NE(last, nullptr);
        EXPECT_EQ(arena.create(), nullptr);
        arena.destroy(last);
        EXPECT_EQ(Tracked::destroyed, 1);
    }
    EXPECT_EQ(Tracked::alive, 0);
    EXPECT_EQ(Tracked::destroyed, 4097);
}

TEST(HierarchicalBitMapArenaTest, ForwardsMoveOnlyArgumentsIntoAlignedStorage)
{
    struct alignas(256) Value {
        std::unique_ptr<int> owned;
        explicit Value(std::unique_ptr<int> p) : owned(std::move(p)) {}
    };
    HierarchicalBitMapArena<Value> arena(65);
    for (int i = 0; i < 65; ++i) {
        auto owned = std::make_unique<int>(i);
        auto* p = arena.create(std::move(owned));
        ASSERT_NE(p, nullptr);
        EXPECT_EQ(owned, nullptr);
        ASSERT_NE(p->owned, nullptr);
        EXPECT_EQ(*p->owned, i);
        EXPECT_EQ(reinterpret_cast<std::uintptr_t>(p) % alignof(Value), 0u);
    }
}

TEST(HierarchicalBitMapArenaTest, RejectsZeroAndOverflowingCapacity)
{
    EXPECT_THROW(HierarchicalBitMapArena<int> arena(0), std::invalid_argument);
    EXPECT_THROW(HierarchicalBitMapArena<std::uint64_t> arena(
        std::numeric_limits<std::size_t>::max() / sizeof(std::uint64_t) + 1), std::bad_alloc);
}
} // namespace
