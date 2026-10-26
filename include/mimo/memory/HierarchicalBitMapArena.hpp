#pragma once
#include <bit>
#include <cstddef>
#include <cstdint>
#include <climits>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <utility>

namespace mimo::memory
{
template <typename T>
class HierarchicalBitMapArena
{
public:
    explicit HierarchicalBitMapArena(std::size_t capacity);
    ~HierarchicalBitMapArena();

    HierarchicalBitMapArena() = delete;
    HierarchicalBitMapArena(const HierarchicalBitMapArena&) = delete;
    HierarchicalBitMapArena(HierarchicalBitMapArena&&) = delete;

    HierarchicalBitMapArena& operator=(const HierarchicalBitMapArena&) = delete;
    HierarchicalBitMapArena& operator=(HierarchicalBitMapArena&&) = delete;

    void destroy(T* ptr) noexcept;

    template<typename... Args>
    T* create(Args&&... args);

    template<typename... Args>
    T* create_n(std::size_t n, Args&&... args);

private:
    static constexpr std::size_t WORD_BITS = sizeof(uint64_t) * CHAR_BIT;
    static constexpr std::uint64_t FILLED = ~std::uint64_t{0};
    struct Level
    {
        Level* prev = nullptr; // Parent summary level.
        Level* next = nullptr;

        std::size_t wordCount = 0;
        uint64_t*   words = nullptr;
    } m_dummy;

    static constexpr std::uint64_t bitMask(const std::size_t index) noexcept
    {
        // Logical child/slot zero is the most significant bit.
        return (std::uint64_t{1} << (WORD_BITS - 1 - index));
    }

    T* alloc() noexcept;
    void dealloc(T* ptr) noexcept;
    void setOccupied(std::size_t index, bool occupied) noexcept;
    void releaseLevels() noexcept;

    Level* m_leaf = nullptr;
    std::size_t m_capacity = 0;
    T* m_buffer = nullptr;
};

template<typename T>
HierarchicalBitMapArena<T>::HierarchicalBitMapArena(std::size_t capacity)
    : m_capacity(capacity)
{
    if (capacity == 0)
        throw std::invalid_argument("capacity must be greater than 0");

    if (capacity > std::numeric_limits<std::size_t>::max() / sizeof(T))
        throw std::bad_alloc();

    try
    {
        while (true)
        {
            const std::size_t wordCount = capacity / WORD_BITS + (capacity % WORD_BITS != 0);
            auto level = std::make_unique<Level>();
            auto words = std::make_unique<std::uint64_t[]>(wordCount);

            // Valid entries occupy the high bits. Unavailable padding occupies the low bits.
            if (const auto remainder = capacity % WORD_BITS; remainder != 0)
                words[wordCount - 1] = FILLED >> remainder;

            level->wordCount = wordCount;
            level->words = words.release();
            level->next = m_dummy.next;

            if (level->next != nullptr)
                level->next->prev = level.get();

            m_dummy.next = level.release();
            if (m_leaf == nullptr)
                m_leaf = m_dummy.next;

            if (wordCount == 1)
                break;

            capacity = wordCount;
        }

        m_buffer = static_cast<T*>(operator new(m_capacity * sizeof(T), std::align_val_t{alignof(T)}));
    }
    catch (...)
    {
        releaseLevels();
        throw;
    }
}

template<typename T>
HierarchicalBitMapArena<T>::~HierarchicalBitMapArena()
{
    for (std::size_t index = 0; index < m_capacity; ++index)
    {
        if ((m_leaf->words[index / WORD_BITS] & bitMask(index % WORD_BITS)) != 0)
            std::destroy_at(m_buffer + index);
    }
    operator delete(m_buffer, std::align_val_t{alignof(T)});
    releaseLevels();
}

template<typename T>
void HierarchicalBitMapArena<T>::releaseLevels() noexcept
{
    while (m_dummy.next != nullptr)
    {
        Level* level = m_dummy.next;
        m_dummy.next = level->next;
        delete[] level->words;
        delete level;
    }
    m_leaf = nullptr;
}

template<typename T>
T* HierarchicalBitMapArena<T>::alloc() noexcept
{
    Level* level = m_dummy.next;
    std::size_t index = 0;

    while (level)
    {
        const std::uint64_t available = ~level->words[index];
        if (available == 0)
            return nullptr;

        const auto bit = std::countl_zero(available);

        index = index * WORD_BITS + bit;
        level = level->next;
    }

    setOccupied(index, true);
    return m_buffer + index;
}

template<typename T>
void HierarchicalBitMapArena<T>::setOccupied(std::size_t index, bool occupied) noexcept
{
    for (Level* level = m_leaf; level != nullptr; level = level->prev)
    {
        const std::size_t   wordIndex   = index / WORD_BITS;
        uint64_t&           word        = level->words[wordIndex];
        const uint64_t      mask        = bitMask(index % WORD_BITS);
        const bool          wasFull     = word == FILLED;

        if (occupied)
            word |= mask;
        else
            word &= ~mask;

        const bool isFull = word == FILLED;
        if (wasFull == isFull)
            break; // Parent summaries only change when a child enters/leaves the full state.

        index = wordIndex;
        occupied = isFull;
    }
}

template<typename T>
void HierarchicalBitMapArena<T>::dealloc(T* ptr) noexcept
{
    setOccupied(static_cast<std::size_t>(ptr - m_buffer), false);
}

template<typename T>
void HierarchicalBitMapArena<T>::destroy(T* ptr) noexcept
{
    std::destroy_at(ptr);
    dealloc(ptr);
}

template<typename T>
template<typename... Args>
T* HierarchicalBitMapArena<T>::create(Args&&... args)
{
    T* ptr = alloc();
    if (ptr == nullptr)
        return nullptr;

    try
    {
        return std::construct_at(ptr, std::forward<Args>(args)...);
    }
    catch (...)
    {
        dealloc(ptr);
        throw;
    }
}
}
