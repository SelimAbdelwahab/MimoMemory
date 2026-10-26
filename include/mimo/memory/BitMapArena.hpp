#pragma once

#include <cstddef>
#include <climits>
#include <limits>
#include <memory>
#include <new>
#include <utility>

namespace mimo::memory
{
template<typename T>
class BitMapArena
{
public:
    explicit BitMapArena(std::size_t capacity);
    ~BitMapArena();

    BitMapArena() = delete;
    BitMapArena(const BitMapArena<T>&) = delete;
    BitMapArena(BitMapArena<T>&&) = delete;

    BitMapArena<T>& operator=(const BitMapArena<T>&) = delete;
    BitMapArena<T>& operator=(BitMapArena<T>&&) = delete;

    void destroy(T* ptr) noexcept;

    template<typename... Args>
    T* create(Args&&... args);

private:
    T* alloc() noexcept;
    void dealloc(T* ptr) noexcept;

    T*              m_buffer;
    std::size_t     m_capacity;

    // Bookkeeping
    std::size_t     m_bytes;
    std::byte*      m_book;

    static constexpr std::byte FILLED = ~static_cast<std::byte>(0);
    static constexpr std::byte AVAILABLE{};
};

template<typename T>
BitMapArena<T>::BitMapArena(const std::size_t capacity)
    : m_buffer(nullptr)
    , m_capacity(capacity)
    , m_bytes(capacity / CHAR_BIT + (capacity % CHAR_BIT != 0))
    , m_book(nullptr)
{
    if (capacity > std::numeric_limits<std::size_t>::max() / sizeof(T))
        throw std::bad_alloc();

    auto book = std::make_unique<std::byte[]>(m_bytes);
    m_buffer = static_cast<T*>(operator new(capacity * sizeof(T), std::align_val_t{alignof(T)}));
    m_book = book.release();

    // Mark trailing bits as used
    if (const auto remainder = capacity % CHAR_BIT; remainder != 0)
    {
        for (std::size_t offset = remainder; offset < CHAR_BIT; ++offset)
            m_book[m_bytes - 1] |= static_cast<std::byte>(1) << offset;
    }
}

template<typename T>
BitMapArena<T>::~BitMapArena()
{
    for (std::size_t objectIndex = 0; objectIndex < m_capacity; ++objectIndex)
    {
        const std::size_t byteIndex = objectIndex / CHAR_BIT;
        const std::size_t bitIndex = objectIndex % CHAR_BIT;

        if ((m_book[byteIndex] & (static_cast<std::byte>(1) << bitIndex)) != AVAILABLE)
            destroy(m_buffer + objectIndex);
    }

    operator delete (m_buffer, std::align_val_t{alignof(T)});
    delete[] m_book;
}

template<typename T>
T* BitMapArena<T>::alloc() noexcept
{
    for (std::size_t byteIndex = 0; byteIndex < m_bytes; ++byteIndex)
    {
        std::byte& byte = m_book[byteIndex];
        if (byte == FILLED)
            continue;

        for (std::size_t index = 0; index < CHAR_BIT; ++index)
        {
            if ((byte & (static_cast<std::byte>(1) << index)) == AVAILABLE)
            {
                byte |= (static_cast<std::byte>(1) << index);
                return m_buffer + CHAR_BIT * byteIndex + index;
            }
        }
    }

    return nullptr;
}

template<typename T>
void BitMapArena<T>::dealloc(T* ptr) noexcept
{
    const auto byteIndex = static_cast<std::size_t>((ptr - m_buffer) / CHAR_BIT);
    const auto bitIndex = static_cast<std::size_t>((ptr - m_buffer) % CHAR_BIT);
    m_book[byteIndex] &= ~(static_cast<std::byte>(1) << bitIndex);
}

template<typename T>
template<typename... Args>
T* BitMapArena<T>::create(Args&&... args)
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

template<typename T>
void BitMapArena<T>::destroy(T* ptr) noexcept
{
    std::destroy_at(ptr);
    dealloc(ptr);
}
}
