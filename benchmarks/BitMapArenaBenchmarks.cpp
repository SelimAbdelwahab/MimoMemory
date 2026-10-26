#include <mimo/memory/BitMapArena.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <print>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;

struct alignas(64) Payload {
    std::array<std::uint64_t, 8> data;
    explicit Payload(std::uint64_t value) : data{value} {}
};

// Make the object escape so optimized builds retain allocation and initialization.
// Supported by Clang and GCC; fail explicitly on other compilers.
template<typename T>
void escape(T* pointer)
{
#if defined(__clang__) || defined(__GNUC__)
    asm volatile("" : : "g"(pointer) : "memory");
#else
#error "This benchmark requires a compiler escape barrier for this compiler"
#endif
}

template<typename Work>
void measure(std::string_view name, std::size_t capacity, std::size_t operations,
             std::size_t samples, Work work)
{
    work(); // Untimed warmup, with the same restored state as each sample.
    std::vector<double> timings;
    timings.reserve(samples);
    for (std::size_t sample = 0; sample < samples; ++sample) {
        const auto start = Clock::now();
        work();
        const auto end = Clock::now();
        timings.push_back(std::chrono::duration<double, std::nano>(end - start).count()
                          / static_cast<double>(operations));
    }
    std::sort(timings.begin(), timings.end());
    const auto median = (timings[(samples - 1) / 2] + timings[samples / 2]) / 2;
    std::println("{},{},{},{},{:.2f},{:.2f}", name, capacity, operations, samples,
                 median, timings.front());
}

void benchmark(std::size_t capacity, std::size_t operations, std::size_t samples)
{
    mimo::memory::BitMapArena<Payload> arena(capacity);
    std::vector<Payload*> pointers(capacity);
    const auto rounds = std::max(std::size_t{1}, operations / capacity);
    const auto batchOperations = rounds * capacity;

    measure("arena_pair", capacity, operations, samples, [&] {
        for (std::size_t i = 0; i < operations; ++i) {
            auto* p = arena.create(i);
            escape(p);
            arena.destroy(p);
        }
    });
    measure("heap_pair", capacity, operations, samples, [&] {
        for (std::size_t i = 0; i < operations; ++i) {
            auto* p = new Payload(i);
            escape(p);
            delete p;
        }
    });
    measure("arena_batch", capacity, batchOperations, samples, [&] {
        for (std::size_t round = 0; round < rounds; ++round) {
            for (std::size_t i = 0; i < capacity; ++i) {
                pointers[i] = arena.create(i);
                escape(pointers[i]);
            }
            for (auto* p : pointers) arena.destroy(p);
        }
    });
    measure("heap_batch", capacity, batchOperations, samples, [&] {
        for (std::size_t round = 0; round < rounds; ++round) {
            for (std::size_t i = 0; i < capacity; ++i) {
                pointers[i] = new Payload(i);
                escape(pointers[i]);
            }
            for (auto* p : pointers) delete p;
        }
    });

    for (auto& p : pointers) p = arena.create(0);
    measure("arena_exhausted", capacity, operations, samples, [&] {
        for (std::size_t i = 0; i < operations; ++i) {
            auto* p = arena.create(i);
            escape(p);
            if (p != nullptr) throw std::runtime_error("full arena accepted allocation");
        }
    });
    for (auto* p : pointers) arena.destroy(p);
}

std::size_t parse(std::string_view value)
{
    std::size_t result = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (error != std::errc{} || end != value.data() + value.size() || result == 0)
        throw std::invalid_argument("arguments must be positive integers");
    return result;
}
} // namespace

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        std::println("Usage: BitMapArenaBenchmarks [operations=100000] [samples=5]");
        return 0;
    }
    try {
        if (argc > 3) throw std::invalid_argument("expected at most two arguments; use --help");
        const auto operations = argc > 1 ? parse(argv[1]) : 100000;
        const auto samples = argc > 2 ? parse(argv[2]) : 5;
#ifndef NDEBUG
        std::println(stderr, "Warning: use a Release build for meaningful benchmark results.");
#endif
        std::println("workload,capacity,operations,samples,median_ns_per_op,min_ns_per_op");
        for (const auto capacity : {64u, 1024u, 8192u}) benchmark(capacity, operations, samples);
    } catch (const std::exception& e) {
        std::println(stderr, "{}", e.what());
        return 1;
    }
}
