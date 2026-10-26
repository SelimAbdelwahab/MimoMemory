#include <mimo/memory/BitMapArena.hpp>
#include <mimo/memory/HierarchicalBitMapArena.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <concepts>
#include <cstdio>
#include <cstdint>
#include <print>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;

struct alignas(64) Payload {
    std::array<std::uint64_t, 8> data;
    explicit Payload(std::uint64_t value) : data{value} {}
};

// Give the regular heap the same API so it runs exactly the same workloads.
template<typename T>
struct HeapArena {
    explicit HeapArena(std::size_t) {}

    template<typename... Args>
    T* create(Args&&... args) { return new T(std::forward<Args>(args)...); }

    void destroy(T* ptr) noexcept { delete ptr; }
};

template<typename Arena>
concept BenchmarkArena = requires(Arena& arena, Payload* ptr, std::size_t value) {
    Arena{value};
    { arena.create(value) } -> std::same_as<Payload*>;
    { arena.destroy(ptr) } noexcept -> std::same_as<void>;
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
void measure(std::string_view allocator, std::string_view workload,
             std::size_t capacity, std::size_t operations,
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
    std::println("{},{},{},{},{},{:.2f},{:.2f}", allocator, workload, capacity, operations, samples,
                 median, timings.front());
}

template<BenchmarkArena Arena, bool HasCapacityLimit = true>
void benchmark(std::string_view name, std::size_t capacity,
               std::size_t operations, std::size_t samples)
{
    Arena arena(capacity);
    std::vector<Payload*> pointers(capacity);
    const auto rounds = std::max(std::size_t{1}, operations / capacity);
    const auto batchOperations = rounds * capacity;

    measure(name, "pair", capacity, operations, samples, [&] {
        for (std::size_t i = 0; i < operations; ++i) {
            auto* p = arena.create(i);
            escape(p);
            arena.destroy(p);
        }
    });
    measure(name, "batch", capacity, batchOperations, samples, [&] {
        for (std::size_t round = 0; round < rounds; ++round) {
            for (std::size_t i = 0; i < capacity; ++i) {
                pointers[i] = arena.create(i);
                escape(pointers[i]);
            }
            for (auto* p : pointers) arena.destroy(p);
        }
    });
    if constexpr (HasCapacityLimit) {
        for (auto& p : pointers) p = arena.create(0);
        measure(name, "exhausted", capacity, operations, samples, [&] {
            for (std::size_t i = 0; i < operations; ++i) {
                auto* p = arena.create(i);
                escape(p);
                if (p != nullptr) throw std::runtime_error("full arena accepted allocation");
            }
        });
        for (auto* p : pointers) arena.destroy(p);
    }
}

std::size_t parse(std::string_view value)
{
    std::size_t result = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (error != std::errc{} || end != value.data() + value.size() || result == 0)
        throw std::invalid_argument("operations and samples must be positive integers");
    return result;
}

struct Options {
    std::size_t operations = 100000;
    std::size_t samples = 5;
    bool hierarchical = false;
    bool flat = false;
    bool help = false;
};

Options parseOptions(int argc, char** argv)
{
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument = argv[i];
        if (argument == "HierarchicalBitMap") options.hierarchical = true;
        else if (argument == "FlatBitMap") options.flat = true;
        else if (argument == "--help" || argument == "-h") options.help = true;
        else if (argument == "--operations" || argument == "--samples") {
            if (++i == argc) throw std::invalid_argument("missing option value; use --help");
            const auto value = parse(argv[i]);
            if (argument == "--operations") options.operations = value;
            else options.samples = value;
        } else {
            throw std::invalid_argument("unknown allocator or option; use --help");
        }
    }
    if (!options.hierarchical && !options.flat)
        options.hierarchical = options.flat = true;
    return options;
}
} // namespace

int main(int argc, char** argv)
{
    try {
        const auto options = parseOptions(argc, argv);
        if (options.help) {
            std::println("Usage: benchmark [HierarchicalBitMap] [FlatBitMap] "
                         "[--operations N] [--samples N]");
            std::println("No allocator names selects both arenas. Heap is always included.");
            std::println("Defaults: --operations 100000 --samples 5");
            return 0;
        }
#ifndef NDEBUG
        std::println(stderr, "Warning: use a Release build for meaningful benchmark results.");
#endif
        std::println("allocator,workload,capacity,operations,samples,median_ns_per_op,min_ns_per_op");
        for (const auto capacity : {64u, 1024u, 8192u}) {
            benchmark<HeapArena<Payload>, false>("Heap", capacity, options.operations, options.samples);
            if (options.hierarchical)
                benchmark<mimo::memory::HierarchicalBitMapArena<Payload>>(
                    "HierarchicalBitMap", capacity, options.operations, options.samples);
            if (options.flat)
                benchmark<mimo::memory::BitMapArena<Payload>>(
                    "FlatBitMap", capacity, options.operations, options.samples);
        }
    } catch (const std::exception& e) {
        std::println(stderr, "{}", e.what());
        return 1;
    }
}
