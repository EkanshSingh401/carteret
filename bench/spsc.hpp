// spsc.hpp -- a single-producer single-consumer ring of message pointers.
//
// The SPSC experiment splits the replay across two cores: one parses and
// publishes a pointer to each book message, the other applies them. The
// question is whether taking the parse off the book's core pays for moving
// every message across cores. Each message's bytes are written into the
// producer's cache by the parse, so the consumer's first read of them is a
// transfer from another core's L2 through the shared L3 rather than a hit in
// its own.
//
// The ring holds pointers into the mapped session, not copies: a message is
// at most 50 bytes and the mapping is read-only and outlives the run, so a
// copy would add a second write of every message for nothing.
//
// Each side keeps a private copy of the other side's index and refreshes it
// only when the ring looks full (producer) or empty (consumer), so in steady
// state neither side reads the other's line on every operation. The indices
// are on separate cache lines so a write to one does not invalidate the other.

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace carteret::bench {

template<std::size_t N>
class SpscRing {
    static_assert(N >= 2 && (N & (N - 1)) == 0, "capacity must be a power of two");

public:
    using Item = const unsigned char*;

    // Producer side. Spins while the ring is full.
    [[gnu::always_inline]] void push(Item v) noexcept {
        const std::size_t h = head_.load(std::memory_order_relaxed);
        while (h - tail_cache_ >= N) tail_cache_ = tail_.load(std::memory_order_acquire);
        buf_[h & (N - 1)] = v;
        head_.store(h + 1, std::memory_order_release);
    }

    // Consumer side. Spins while the ring is empty.
    [[gnu::always_inline]] Item pop() noexcept {
        const std::size_t t = tail_.load(std::memory_order_relaxed);
        while (t == head_cache_) head_cache_ = head_.load(std::memory_order_acquire);
        Item v = buf_[t & (N - 1)];
        tail_.store(t + 1, std::memory_order_release);
        return v;
    }

private:
    alignas(64) std::atomic<std::size_t> head_{0}; // written by the producer
    alignas(64) std::size_t tail_cache_ = 0;       // producer's copy of tail_
    alignas(64) std::atomic<std::size_t> tail_{0}; // written by the consumer
    alignas(64) std::size_t head_cache_ = 0;       // consumer's copy of head_
    alignas(64) Item buf_[N] = {};
};

} // namespace carteret::bench
