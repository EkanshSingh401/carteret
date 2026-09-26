// alloc_map.hpp -- where the book's structures live, for perf mem attribution.
//
// perf mem reports the data address of each sampled load. Turning an address
// into "the order pool" or "the index" needs the address range of every
// structure, and FastBook keeps its storage private. Reading /proc/self/maps
// does not answer it either: the kernel merges adjacent anonymous mappings
// with the same permissions into one, so two consecutive large vectors can
// appear as a single region.
//
// So bench_book replaces the global operator new and, while recording is on,
// logs every allocation of at least 1 MiB and every allocation of two exact
// small sizes: a level window (kWindowTicks levels) and an overflow map node,
// whose size belongs to the standard library and is learned by allocating one
// rather than assumed. The book's constructor
// allocates its large structures in member-declaration order (the index, then
// the pool, the reference array and the symbol table), so the order of the
// log names them; the windows are allocated lazily as symbols first trade.
//
// Recording writes into a fixed static table and never allocates, because it
// runs inside operator new.

#pragma once

#include <cstddef>
#include <cstdint>

namespace carteret::bench {

struct AllocRecord {
    std::uintptr_t addr;
    std::size_t bytes;
};

// Starts recording large allocations and allocations of exactly window_bytes
// or node_bytes.
void alloc_map_start(std::size_t window_bytes, std::size_t node_bytes) noexcept;
// Returns the size of the next allocation made after calling it, via
// alloc_map_learned(). Used to learn a std::map node's size from one insert.
void alloc_map_learn() noexcept;
[[nodiscard]] std::size_t alloc_map_learned() noexcept;
void alloc_map_stop() noexcept;
[[nodiscard]] std::size_t alloc_map_count() noexcept;
[[nodiscard]] const AllocRecord* alloc_map_records() noexcept;
// True if the table filled and later allocations went unrecorded.
[[nodiscard]] bool alloc_map_overflowed() noexcept;

} // namespace carteret::bench
