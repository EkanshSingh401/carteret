// Replaces the global allocation functions so bench_book can log where the
// book's structures were placed. See alloc_map.hpp.

#include "alloc_map.hpp"

#include <atomic>
#include <cstdlib>
#include <new>

namespace carteret::bench {
namespace {

// Two windows per symbol at most, for 16,384 symbols, the large structures,
// and every overflow node allocated over a session, repeats included. The
// table is zero-initialised static storage, so only the part used is touched.
constexpr std::size_t kMaxRecords = 1u << 22;
constexpr std::size_t kLarge = 1u << 20;

AllocRecord g_records[kMaxRecords];
std::atomic<std::size_t> g_count{0};
std::atomic<bool> g_on{false};
std::atomic<bool> g_overflowed{false};
std::size_t g_window = 0;
std::size_t g_node = 0;
std::atomic<bool> g_learn{false};
std::size_t g_learned = 0;

void note(void* p, std::size_t n) noexcept {
    if (g_learn.load(std::memory_order_relaxed)) {
        g_learned = n;
        g_learn.store(false, std::memory_order_relaxed);
    }
    if (!g_on.load(std::memory_order_relaxed)) return;
    if (n < kLarge && n != g_window && n != g_node) return;
    const std::size_t i = g_count.fetch_add(1, std::memory_order_relaxed);
    if (i >= kMaxRecords) {
        g_overflowed.store(true, std::memory_order_relaxed);
        return;
    }
    g_records[i] = {reinterpret_cast<std::uintptr_t>(p), n};
}

} // namespace

void alloc_map_start(std::size_t window_bytes, std::size_t node_bytes) noexcept {
    g_window = window_bytes;
    g_node = node_bytes;
    g_count.store(0, std::memory_order_relaxed);
    g_overflowed.store(false, std::memory_order_relaxed);
    g_on.store(true, std::memory_order_relaxed);
}
void alloc_map_learn() noexcept {
    g_learn.store(true, std::memory_order_relaxed);
}
std::size_t alloc_map_learned() noexcept {
    return g_learned;
}
void alloc_map_stop() noexcept {
    g_on.store(false, std::memory_order_relaxed);
}
std::size_t alloc_map_count() noexcept {
    const std::size_t n = g_count.load(std::memory_order_relaxed);
    return n < kMaxRecords ? n : kMaxRecords;
}
const AllocRecord* alloc_map_records() noexcept {
    return g_records;
}
bool alloc_map_overflowed() noexcept {
    return g_overflowed.load(std::memory_order_relaxed);
}

} // namespace carteret::bench

void* operator new(std::size_t n) {
    void* p = std::malloc(n ? n : 1);
    if (!p) throw std::bad_alloc();
    carteret::bench::note(p, n);
    return p;
}
void* operator new[](std::size_t n) {
    return ::operator new(n);
}
void operator delete(void* p) noexcept {
    std::free(p);
}
void operator delete[](void* p) noexcept {
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept {
    std::free(p);
}
void operator delete[](void* p, std::size_t) noexcept {
    std::free(p);
}
