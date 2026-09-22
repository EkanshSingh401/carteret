// carteret/hash_policy.hpp -- interchangeable hash policies for the order index.
//
// Order reference numbers are day-unique, and the specification's guarantee
// that they increase was removed in February 2009. In practice they are
// roughly increasing, which makes the choice of hash a question about cache
// behaviour rather than about distribution alone:
//
//   Identity maps nearby references to nearby slots, so recent orders cluster
//   and may stay resident; it distributes worse.
//   Multiply-shift and std::hash deliberately scatter recent references across
//   the whole table, which distributes better and may be where locality dies.
//
// The direction of the net effect is not predictable from first principles,
// which is why the policy is a template parameter and all three are measured.
// Miss rate and mean probe length are reported, not wall time alone, so a win
// can be attributed. See docs/design.md record 016.
//
// Correctness must not depend on any ordering property of references: probing,
// wraparound and deletion are correct for arbitrary 64-bit values under every
// policy here (record 012).

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace carteret {

// A policy maps a reference to a slot index. It takes the table's mask
// (capacity - 1, capacity being a power of two) and shift (64 - log2 capacity),
// both precomputed by the index, so that a policy which wants the low bits and
// one which wants the high bits can each take what it needs without the index
// applying a mix of its own. An index-side fold would make every policy behave
// alike and leave nothing to compare.

// The low bits of the reference, taken directly. Roughly increasing references
// therefore land in roughly increasing slots.
struct IdentityHash {
    static constexpr const char* name = "identity";
    [[gnu::always_inline]] static std::size_t index(std::uint64_t ref, std::uint64_t mask,
                                                    unsigned) noexcept {
        return static_cast<std::size_t>(ref & mask);
    }
};

// Fibonacci hashing: multiply by the 64-bit approximation of 2^64 / phi and
// keep the high bits, which are the ones the multiply has mixed. One multiply
// and one shift, and unlike identity it depends on the whole reference rather
// than only its low bits.
struct MultiplyShiftHash {
    static constexpr const char* name = "multiply-shift";
    static constexpr std::uint64_t kPhi = 0x9E3779B97F4A7C15ULL;
    [[gnu::always_inline]] static std::size_t index(std::uint64_t ref, std::uint64_t,
                                                    unsigned shift) noexcept {
        return static_cast<std::size_t>((ref * kPhi) >> shift);
    }
};

// The policy a reader would reach for by default. On both libstdc++ and libc++
// std::hash is the identity for 64-bit integers, so this is expected to track
// IdentityHash closely; any gap between the two is measurement noise rather
// than a hashing effect, which makes it a useful control on the experiment.
struct StdHash {
    static constexpr const char* name = "std::hash";
    [[gnu::always_inline]] static std::size_t index(std::uint64_t ref, std::uint64_t mask,
                                                    unsigned) noexcept {
        return static_cast<std::size_t>(std::hash<std::uint64_t>{}(ref)&mask);
    }
};

} // namespace carteret
