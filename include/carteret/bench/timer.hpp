// carteret/bench/timer.hpp -- fenced cycle counting, and an honest fallback.
//
// The only clock this project publishes numbers from is an invariant x86-64
// TSC read through a fenced rdtsc/rdtscp pair on Linux. Everywhere else the
// harness still builds and runs, on a portable clock, and reports itself as
// non-publishable. That distinction is enforced by the type: a Timings carries
// the clock that produced it, and the reporting path refuses to label results
// as measurements unless that clock is the reference one.
//
// Fencing. `rdtsc` alone can be reordered with surrounding work in both
// directions. `rdtscp` waits for earlier instructions to retire but does not
// stop later ones from starting before the counter is read. So a region opens
// with `lfence; rdtsc` and closes with `rdtscp; lfence`: the leading lfence
// keeps earlier work from drifting past the start read, and the trailing one
// keeps later work from drifting before the end read.
//
// Invariance. `rdtsc` is a clock only if the TSC ticks at a fixed rate
// regardless of core frequency (`constant_tsc`) and does not stop in deep
// C-states (`nonstop_tsc`). Without both, an interval in ticks has no fixed
// relationship to an interval in nanoseconds. Both are checked at runtime and
// a missing one disqualifies the run, rather than being noted in a footnote
// under a number that has already been quoted.

#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#if defined(__x86_64__) && defined(__linux__)
#define CARTERET_HAVE_RDTSC 1
#include <x86intrin.h>
#else
#define CARTERET_HAVE_RDTSC 0
#endif

namespace carteret::bench {

// Which clock produced a measurement. Anything other than InvariantTsc is a
// smoke test: it shows the harness runs, and its numbers are not published.
enum class ClockKind : unsigned char {
    InvariantTsc, // fenced rdtsc/rdtscp on a constant, nonstop TSC
    Portable,     // std::chrono::steady_clock; resolution unknown and coarse
};

inline const char* clock_name(ClockKind k) noexcept {
    return k == ClockKind::InvariantTsc ? "invariant TSC (fenced rdtsc/rdtscp)"
                                        : "std::chrono::steady_clock (portable fallback)";
}

// Opens a timed region.
[[gnu::always_inline]] inline std::uint64_t tick_begin() noexcept {
#if CARTERET_HAVE_RDTSC
    _mm_lfence();
    return __rdtsc();
#else
    return static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
#endif
}

// Closes a timed region.
[[gnu::always_inline]] inline std::uint64_t tick_end() noexcept {
#if CARTERET_HAVE_RDTSC
    unsigned aux;
    const std::uint64_t t = __rdtscp(&aux);
    _mm_lfence();
    return t;
#else
    return static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
#endif
}

// What the host can and cannot support, established once at startup.
struct ClockInfo {
    ClockKind kind = ClockKind::Portable;
    bool constant_tsc = false;
    bool nonstop_tsc = false;
    bool rdtscp = false;
    double ns_per_tick = 0.0;         // from calibration against CLOCK_MONOTONIC
    double calibration_error = 0.0;   // relative spread across calibration rounds
    std::uint64_t overhead_ticks = 0; // median cost of one begin/end pair
    // The smallest nonzero difference between two consecutive clock reads:
    // the quantum the clock actually resolves, which is not the same as the
    // unit it reports in. The development Mac's portable clock reports
    // nanoseconds and advances in steps of 41.667 of them, because it is
    // backed by a 24 MHz counter. Reporting a percentile of "42 ns" from it
    // states one tick of that counter and nothing finer -- two operations
    // differing by 30 ns print identically. Measured rather than assumed, so
    // a host whose clock is finer is not penalised by a constant.
    std::uint64_t resolution_ticks = 0;     // smallest nonzero step observed
    std::uint64_t resolution_ticks_max = 0; // median step; robust to preemption
    std::string note;

    // True only when a published latency number may be derived from this host.
    [[nodiscard]] bool publishable() const noexcept {
        return kind == ClockKind::InvariantTsc && constant_tsc && nonstop_tsc && rdtscp;
    }
};

// Reads the CPU flags the TSC's usability depends on. Returns false on any
// host where the flags cannot be read, which is treated as "not present"
// rather than as "probably fine".
bool read_tsc_flags(bool& constant_tsc, bool& nonstop_tsc, bool& rdtscp) noexcept;

// Calibrates ticks to nanoseconds against CLOCK_MONOTONIC and measures the
// cost of one begin/end pair. Both are needed: the first to report a duration,
// the second because in per-message mode the instrument is a measurable
// fraction of what is being measured.
ClockInfo probe_clock(int rounds = 5, int calibration_ms = 50);

} // namespace carteret::bench
