#pragma once
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <climits>

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
    struct Level
    {
        Level* prev = nullptr;
        Level* next = nullptr;

        std::size_t wordCount = 0;
        uint64_t*   words = nullptr;
    } m_dummy;

    T* m_buffer = nullptr;
};

template<typename T>
HierarchicalBitMapArena<T>::HierarchicalBitMapArena(std::size_t capacity)
{
    if (capacity == 0)
        throw std::invalid_argument("capacity must be greater than 0");

    Level* current = &m_dummy;
    while (current->wordCount != 1)
    {
        current->next = new Level
        {
            .prev = current,
            .next = nullptr,
        };
        current = current->next;

        std::size_t wordCount = capacity / WORD_BITS + (capacity % WORD_BITS != 0);
        current->wordCount = wordCount;
        current->words = new uint64_t[wordCount]{};

        if (const std::size_t remainder = capacity % WORD_BITS; remainder != 0)
        {
            for (std::size_t bit = remainder; bit < WORD_BITS; ++bit)
                current->words[wordCount - 1] |= std::uint64_t{1} << bit;
        }

        capacity = wordCount;
    }
}
}
