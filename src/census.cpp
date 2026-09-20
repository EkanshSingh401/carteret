// census -- per-type message census over a session file.
//
// This is Week 1's deliverable and your first correctness gate. Run it against
// 20170130.BX_ITCH_50 and require an EXACT match on every type against a
// published third party (the RITCH R package publishes counts; regenerate them
// yourself rather than trusting a number you read somewhere).
//
// If the totals match to the message, your framing and your length table are
// right, and everything downstream can stand on that. If they do not, stop.
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
        if (st != FrameStatus::Ok) continue;   // skipped and counted in rd

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
    std::printf("end-of-session    %s\n", saw_end ? "yes" : "NO  <-- truncated or malformed");
    std::printf("first ts          %llu ns\n", (unsigned long long)first_ts);
    std::printf("last ts           %llu ns\n", (unsigned long long)last_ts);
    std::printf("\nper type:\n");
    for (std::size_t t = 0; t < counts.size(); ++t) {
        if (counts[t]) std::printf("  %c  %12llu\n", static_cast<int>(t), (unsigned long long)counts[t]);
    }

    // NOT a benchmark. Wall-clock over an unpinned core with the page cache in
    // whatever state it happens to be in. It tells you the run finished; it
    // does not tell you how fast anything is. The real numbers come from the
    // benchmark harness on an isolated core. Do not put this figure on a resume.
    std::printf("\n[not a benchmark] %.2fs wall, %.1f M msg/s\n",
                secs, secs > 0 ? (double)total / secs / 1e6 : 0.0);
    return 0;
}
