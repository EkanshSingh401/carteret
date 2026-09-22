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
// implementation. tests/test_rng_golden.cpp pins their output, and CI runs it
// under both compilers.
//
// ONE PLATFORM DEPENDENCY REMAINS, and it is named rather than hidden:
// exponential_ns() calls std::log1p. A correctly rounded log1p gives identical
// results everywhere, but the last-ulp behaviour of a math library is not
// guaranteed by the standard. A difference there would move a placement time
// by a few nanoseconds, which is far below the microsecond spacing of
// messages and so is very unlikely to change which message a placement lands
// on -- but the golden test asserts exact equality anyway, so a platform whose
// log1p differs is reported immediately instead of quietly producing different
// results.

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

    [[nodiscard]] bool coin() noexcept { return (rng_() & 1u) != 0; }

private:
    std::mt19937_64 rng_;
};

} // namespace carteret
