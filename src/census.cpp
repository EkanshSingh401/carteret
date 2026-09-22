// census -- per-type message census over a session file.
//
// Correctness layer 2: an exact per-type match against an independently
// implemented counter establishes that the framing loop and the length table
// walk the file correctly. It says nothing about field decode, which layer 1
// covers. tools/census_vs_ritch.sh runs the comparison against the RITCH R
// package.
//
//   usage: census <session-file>

#include "carteret/mapped_file.hpp"
#include "carteret/spec.hpp"
#include "carteret/wire.hpp"

#include <array>
#include <chrono>
#include <cstdio>
#include <span>

using namespace carteret;

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: %s <session-file>\n", argv[0]); return 2; }

    MappedFile mf(argv[1]);
    FrameReader rd(mf.bytes());

    std::array<std::uint64_t, 256> counts{};
    std::uint64_t total = 0, book_msgs = 0;
    std::uint64_t first_ts = 0, last_ts = 0;
    bool saw_end = false;

    const auto t0 = std::chrono::steady_clock::now();

    MsgView m;
    for (;;) {
        const FrameStatus st = rd.next(m);
        if (st == FrameStatus::EndOfSession) { saw_end = true; break; }
        if (st == FrameStatus::Truncated) break;
        if (st != FrameStatus::Ok) continue;   // skipped and counted by the reader

        ++counts[m.type()];
        ++total;
        if (touches_book(m.type())) ++book_msgs;
        const std::uint64_t ts = m.ts();
        if (first_ts == 0) first_ts = ts;
        last_ts = ts;
    }

    const auto t1 = std::chrono::steady_clock::now();
    const double secs = std::chrono::duration<double>(t1 - t0).count();

    std::printf("file              %s\n", argv[1]);
    std::printf("bytes             %zu\n", mf.size());
    std::printf("messages          %llu\n", (unsigned long long)total);
    std::printf("book-affecting    %llu  (%.1f%%)\n", (unsigned long long)book_msgs,
                total ? 100.0 * (double)book_msgs / (double)total : 0.0);
    std::printf("unknown type      %llu\n", (unsigned long long)rd.unknown());
    std::printf("length mismatch   %llu\n", (unsigned long long)rd.mismatch());
    std::printf("end-of-session    %s\n", saw_end ? "yes" : "no  (truncated or malformed)");
    std::printf("first ts          %llu ns\n", (unsigned long long)first_ts);
    std::printf("last ts           %llu ns\n", (unsigned long long)last_ts);
    std::printf("\nper type:\n");
    for (std::size_t t = 0; t < counts.size(); ++t) {
        if (counts[t]) std::printf("  %c  %12llu\n", static_cast<int>(t), (unsigned long long)counts[t]);
    }

    // Not a benchmark. This is wall-clock time on an unpinned core with the page
    // cache in an arbitrary state; it confirms the run completed and nothing
    // more. Published latency figures come from the benchmark harness on an
    // isolated core, per docs/benchmarks.md.
    std::printf("\n[not a benchmark] %.2fs wall, %.1f M msg/s\n",
                secs, secs > 0 ? (double)total / secs / 1e6 : 0.0);
    return 0;
}
