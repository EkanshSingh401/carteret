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

    return info;
}

} // namespace carteret::bench
