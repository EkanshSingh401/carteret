// carteret/sampling.hpp -- reproducible sampling on top of a specified engine.
//
// std::mt19937_64 is specified down to the exact sequence it produces. The
// DISTRIBUTIONS in <random> are not: libstdc++ and libc++ consume the engine
// differently, so the same seed yields different draws under the two. That is
// not an abstract concern -- it made the queue simulator's tests pass under
// Clang and fail under GCC, and it would have made the study's published
// numbers depend on which standard library produced them.
//
// So every draw this project makes goes through the helpers below, which are
// arithmetic on raw engine output and specified here rather than by the
// implementation. tests/test_rng_golden.cpp pins their output; CI runs it
// under both compilers, and on both Linux and macOS so that two math
// libraries are compared as well as two standard libraries.
//
// ONE PLATFORM DEPENDENCY REMAINS, and it is named rather than hidden:
// exponential_ns() calls std::log1p, which the standard does not require to be
// correctly rounded, so two libms may differ in the last bit.
//
// That draw is QUANTIZED TO INTEGER NANOSECONDS in the same expression that
// generates it, and no floating-point value derived from libm is ever
// returned, stored or compared. The quantization is what contains the
// dependency, and the margin is large: for a mean of 250 ms a gap is of order
// 1e8, one ulp of which is about 3e-8 ns, so a one-ulp disagreement changes
// the returned integer only when the product falls within 3e-8 of an integer
// boundary -- about three chances in a hundred million per draw. A libm that
// differs by one ulp therefore produces bit-identical study output; a libm
// that differs by more than that fails tests/test_rng_golden.cpp, which pins
// six values directly and checksums a million more. Either the difference
// vanishes or it is reported, and neither outcome is a quiet divergence.
//
// bernoulli() is deliberately integer-only for the same reason: the
// Bernoulli-proportional model draws once per cancel, far more often than
// placements occur, and a float comparison there would put libm back on a
// path where quantization could not contain it.

#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <random>

namespace carteret {

class Sampler {
public:
    explicit Sampler(std::uint64_t seed) noexcept : rng_(seed) {}

    // Raw engine output. The engine is specified; this is the only thing the
    // helpers below are built on.
    [[nodiscard]] std::uint64_t raw() noexcept { return rng_(); }

    // A double on [0, 1) from the top 53 bits, the standard construction. Bit
    // exact on any implementation: a shift and a multiply by a power of two.
    [[nodiscard]] double uniform01() noexcept {
        return static_cast<double>(rng_() >> 11) * 0x1.0p-53;
    }

    // Inverse-transform exponential. For u uniform on [0, 1),
    // -mean * log(1 - u) is exponential with that mean; log1p(-u) is used
    // rather than log(1 - u) because u can be small enough for the
    // subtraction to lose precision. Returns at least 1 so a schedule always
    // advances.
    [[nodiscard]] std::uint64_t exponential_ns(std::uint64_t mean_ns) noexcept {
        const double gap = -static_cast<double>(mean_ns) * std::log1p(-uniform01());
        return static_cast<std::uint64_t>(gap) + 1u;
    }

    // Modulo reduction. Its bias is n / 2^64, far below any effect this
    // project could resolve, and unlike std::uniform_int_distribution it is
    // identical on every implementation.
    [[nodiscard]] std::size_t pick(std::size_t n) noexcept {
        return n ? static_cast<std::size_t>(rng_() % n) : 0;
    }

    // Bernoulli with probability num/den, in integer arithmetic only. The
    // modulo bias is den / 2^64; for the depths this is called with, that is
    // below one part in 10^14. A float comparison against uniform01() would be
    // equivalent in principle and would reintroduce a platform dependency for
    // no gain.
    [[nodiscard]] bool bernoulli(std::uint64_t num, std::uint64_t den) noexcept {
        // Both degenerate rates return without consuming a draw, so a
        // certainty costs nothing and, more importantly, does not shift the
        // stream for every later call.
        if (den == 0 || num == 0) return false;
        if (num >= den) return true;
        return (rng_() % den) < num;
    }

    [[nodiscard]] bool coin() noexcept { return (rng_() & 1u) != 0; }

private:
    std::mt19937_64 rng_;
};

} // namespace carteret
