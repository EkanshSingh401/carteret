// Clock probing: capability flags, tick-to-nanosecond calibration, and the
// cost of the instrument itself.

#include "carteret/bench/timer.hpp"

#include <algorithm>
#include <cstdio>
#include <vector>

#if defined(__linux__)
#include <cstring>
#endif

namespace carteret::bench {

bool read_tsc_flags(bool& constant_tsc, bool& nonstop_tsc, bool& rdtscp) noexcept {
    constant_tsc = nonstop_tsc = rdtscp = false;
#if defined(__linux__)
    std::FILE* f = std::fopen("/proc/cpuinfo", "r");
    if (!f) return false;
    char line[4096];
    bool saw_flags = false;
    while (std::fgets(line, sizeof line, f)) {
        if (std::strncmp(line, "flags", 5) != 0) continue;
        saw_flags = true;
        constant_tsc = std::strstr(line, "constant_tsc") != nullptr;
        nonstop_tsc = std::strstr(line, "nonstop_tsc") != nullptr;
        rdtscp = std::strstr(line, "rdtscp") != nullptr;
        break;
    }
    std::fclose(f);
    return saw_flags;
#else
    // No equivalent is read on other platforms, and absence is reported as
    // absence. Guessing here would be the one place a non-publishable host
    // could pass itself off as a publishable one.
    return false;
#endif
}

ClockInfo probe_clock(int rounds, int calibration_ms) {
    ClockInfo info;
    read_tsc_flags(info.constant_tsc, info.nonstop_tsc, info.rdtscp);

#if CARTERET_HAVE_RDTSC
    info.kind = ClockKind::InvariantTsc;
#else
    info.kind = ClockKind::Portable;
    info.note = "no rdtsc on this target; the portable clock is a smoke test only";
#endif

    // Tick-to-nanosecond calibration against the monotonic clock. Repeated, so
    // that the spread across rounds is reported rather than a single ratio
    // that could have been taken across a scheduling gap.
    std::vector<double> ratios;
    ratios.reserve(static_cast<std::size_t>(rounds));
    for (int r = 0; r < rounds; ++r) {
        const auto wall0 = std::chrono::steady_clock::now();
        const std::uint64_t t0 = tick_begin();
        const auto deadline = wall0 + std::chrono::milliseconds(calibration_ms);
        while (std::chrono::steady_clock::now() < deadline) {
            // Spin: sleeping would hand the core away and, on a host without
            // nonstop_tsc, stop the counter being calibrated.
        }
        const std::uint64_t t1 = tick_end();
        const auto wall1 = std::chrono::steady_clock::now();
        const double ns = std::chrono::duration<double, std::nano>(wall1 - wall0).count();
        const double ticks = static_cast<double>(t1 - t0);
        if (ticks > 0) ratios.push_back(ns / ticks);
    }
    if (!ratios.empty()) {
        std::sort(ratios.begin(), ratios.end());
        info.ns_per_tick = ratios[ratios.size() / 2];
        if (info.ns_per_tick > 0) {
            info.calibration_error = (ratios.back() - ratios.front()) / info.ns_per_tick;
        }
    }

    // Cost of one begin/end pair with nothing between them. In per-message
    // mode this is subtracted from every sample, and its size relative to a
    // book update is reported, because an instrument that perturbs the
    // measurement by a tenth is worth more attention than most of the
    // optimisations it is used to evaluate.
    constexpr int kOverheadSamples = 4096;
    std::vector<std::uint64_t> overhead;
    overhead.reserve(kOverheadSamples);
    for (int i = 0; i < kOverheadSamples; ++i) {
        const std::uint64_t a = tick_begin();
        const std::uint64_t b = tick_end();
        overhead.push_back(b > a ? b - a : 0);
    }
    std::sort(overhead.begin(), overhead.end());
    info.overhead_ticks = overhead[overhead.size() / 2];

    // The clock's own resolution: the smallest nonzero step it takes. Read it
    // by spinning until the value changes, repeatedly, and keeping the
    // smallest change seen. A clock that reports in nanoseconds but advances
    // in steps of 41.667 of them is reporting a unit it cannot resolve, and
    // every percentile taken from it is a multiple of that step.
    {
        std::vector<std::uint64_t> steps;
        steps.reserve(256);
        for (int r = 0; r < 256; ++r) {
            const std::uint64_t a = tick_begin();
            std::uint64_t b = a;
            // Bounded so a pathological clock cannot hang the probe.
            for (int spin = 0; spin < 1000000 && b == a; ++spin) b = tick_end();
            if (b > a) steps.push_back(b - a);
        }
        std::uint64_t smallest = 0;
        std::uint64_t typical = 0;
        if (!steps.empty()) {
            std::sort(steps.begin(), steps.end());
            smallest = steps.front();
            // The MEDIAN, not the maximum. A spin that is preempted between
            // the two reads returns a step of several quanta, and taking the
            // largest would report that scheduling artifact as the clock's
            // resolution -- it measured 125 against a true 41.667 on the
            // development Mac. The median is unmoved by a handful of them.
            typical = steps[steps.size() / 2];
        }
        // Both ends are kept because a clock whose reported unit is FINER than
        // its counter does not take a constant step: the development Mac
        // reports nanoseconds from a 24 MHz counter, so consecutive steps
        // alternate between 41 and 42 ns and the true quantum of 41.667 is
        // neither. Reporting only the minimum would state a resolution better
        // than the clock has.
        info.resolution_ticks = smallest;
        info.resolution_ticks_max = typical;
    }

    return info;
}

} // namespace carteret::bench
