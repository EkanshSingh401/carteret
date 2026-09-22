// census -- per-type message census over a session file.
//
// Correctness layer 2. An exact per-type match against an independently
// implemented counter establishes that the framing loop, the length table and
// the dispatch switch walk the file the same way a second implementation does.
// It says nothing about field decode, which layer 1 covers.
// tools/census_vs_ritch.sh runs the comparison against the RITCH R package.
//
// The count is taken through Parser, not through FrameReader directly, so a
// message the frame reader accepts but the dispatch switch drops would show up
// as a mismatch rather than passing silently.
//
//   usage: census [--tsv] [--sha256] <session-file>
//
// --tsv writes one "<type>\t<count>" line per known message type, including
// types with a zero count, for machine comparison.
//
// --sha256 digests the whole file with the in-tree implementation, for the
// record kept in docs/data.md. It is off by default because it reads the file
// a second time.
//
// The framing form is detected and reported, because one file in circulation
// -- the RITCH package's bundled test fixture -- carries zeroed length
// prefixes. See wire.hpp.
//
// TERMINATION. The ITCH 5.0 specification guarantees one thing about the end
// of a session: System Event 'C', End of Messages, is the last message of the
// day. It says nothing about a zero-length length-prefix; that is a property
// of how a file is packaged, described by third parties, and NASDAQ's
// published sessions do not write one. So the terminator that matters is the
// 'C' message, and its absence is the signal that a file is truncated. The
// checks below report the final message, any zero-length prefix, and any
// bytes left over, separately, because a file can fail each in a different
// way.

#include "carteret/mapped_file.hpp"
#include "carteret/parser.hpp"
#include "carteret/spec.hpp"

#include "carteret/sha256.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>

using namespace carteret;

namespace {

// Counts every type and tracks the session's time span. Deliberately does no
// book work: this is a framing and dispatch gate.
struct Census {
    std::array<std::uint64_t, 256> counts{};
    std::uint64_t first_ts = 0;
    std::uint64_t last_ts = 0;
    bool saw_first = false;
    bool saw_end_of_system_hours = false;
    unsigned char last_type = 0;       // the final message's type byte
    unsigned char last_event_code = 0; // if that message was a System Event
    std::uint64_t after_end_of_system_hours = 0;

    [[gnu::always_inline]] void record(unsigned char type, std::uint64_t ts) {
        ++counts[type];
        last_type = type;
        if (type != 'S') last_event_code = 0;
        if (!saw_first) {
            first_ts = ts;
            saw_first = true;
        }
        last_ts = ts;
        // 'B' and 'D' legitimately arrive after the 'E' system event; see
        // docs/design.md record 007. Counted so the claim is checkable
        // against real data rather than asserted from the specification.
        if (saw_end_of_system_hours) ++after_end_of_system_hours;
    }

    void on(SystemEvent v) {
        record(v.type(), v.ts());
        last_event_code = v.event_code();
        if (v.event_code() == 'E') saw_end_of_system_hours = true;
    }
    void on(StockDirectory v) { record(v.type(), v.ts()); }
    void on(StockTradingAction v) { record(v.type(), v.ts()); }
    void on(RegSHO v) { record(v.type(), v.ts()); }
    void on(MarketParticipant v) { record(v.type(), v.ts()); }
    void on(MwcbDeclineLevel v) { record(v.type(), v.ts()); }
    void on(MwcbStatus v) { record(v.type(), v.ts()); }
    void on(IpoQuotingPeriod v) { record(v.type(), v.ts()); }
    void on(LuldAuctionCollar v) { record(v.type(), v.ts()); }
    void on(OperationalHalt v) { record(v.type(), v.ts()); }
    void on(AddOrder v) { record(v.type(), v.ts()); }
    void on(AddOrderMpid v) { record(v.type(), v.ts()); }
    void on(OrderExecuted v) { record(v.type(), v.ts()); }
    void on(OrderExecutedPrice v) { record(v.type(), v.ts()); }
    void on(OrderCancel v) { record(v.type(), v.ts()); }
    void on(OrderDelete v) { record(v.type(), v.ts()); }
    void on(OrderReplace v) { record(v.type(), v.ts()); }
    void on(Trade v) { record(v.type(), v.ts()); }
    void on(CrossTrade v) { record(v.type(), v.ts()); }
    void on(BrokenTrade v) { record(v.type(), v.ts()); }
    void on(Noii v) { record(v.type(), v.ts()); }
    void on(Rpii v) { record(v.type(), v.ts()); }
    void on(DirectListingCapRaise v) { record(v.type(), v.ts()); }
};

// The specification's type bytes in table order, so the TSV output has a
// stable, documented row order independent of the byte values.
constexpr char kTypeOrder[] = "SRHYLVWKJhAFECXDUPQBINO";

} // namespace

int main(int argc, char** argv) {
    bool tsv = false;
    bool want_sha = false;
    const char* path = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--tsv") == 0) {
            tsv = true;
        } else if (std::strcmp(argv[i], "--sha256") == 0) {
            want_sha = true;
        } else {
            path = argv[i];
        }
    }
    if (!path) {
        std::fprintf(stderr, "usage: %s [--tsv] [--sha256] <session-file>\n", argv[0]);
        return 2;
    }

    MappedFile mf(path);
    Census c;
    Parser<Census> parser(c);

    const auto t0 = std::chrono::steady_clock::now();
    const ParseStats st = parser.run(mf.bytes());
    const auto t1 = std::chrono::steady_clock::now();
    const double secs = std::chrono::duration<double>(t1 - t0).count();

    const bool ends_on_c_early = c.last_type == 'S' && c.last_event_code == 'C';

    if (tsv) {
        for (const char* t = kTypeOrder; *t; ++t) {
            std::printf("%c\t%llu\n", *t,
                        (unsigned long long)c.counts[static_cast<unsigned char>(*t)]);
        }
        return (st.clean() && ends_on_c_early) ? 0 : 1;
    }

    std::uint64_t book_msgs = 0;
    for (std::size_t t = 0; t < c.counts.size(); ++t) {
        if (touches_book(static_cast<unsigned char>(t))) book_msgs += c.counts[t];
    }

    std::printf("file              %s\n", path);
    std::printf("bytes             %zu\n", mf.size());
    if (want_sha) {
        // Digested with the in-tree implementation. Cross-checking it against
        // the system's shasum, as tools/fetch_data.sh does, is two
        // independent implementations agreeing on the same bytes.
        Sha256 h;
        const auto bytes = mf.bytes();
        constexpr std::size_t kChunk = 1u << 22;
        for (std::size_t off = 0; off < bytes.size(); off += kChunk) {
            h.update(bytes.data() + off, std::min(kChunk, bytes.size() - off));
        }
        std::printf("sha256            %s\n", h.hex().c_str());
    }
    std::printf("framing           %s\n", st.framing == Framing::LengthPrefixed
                                              ? "length-prefixed"
                                              : "zero-prefixed (length from type byte)");
    std::printf("messages          %llu\n", (unsigned long long)st.dispatched);
    std::printf("book-affecting    %llu  (%.1f%%)\n", (unsigned long long)book_msgs,
                st.dispatched ? 100.0 * (double)book_msgs / (double)st.dispatched : 0.0);
    std::printf("unknown type      %llu\n", (unsigned long long)st.unknown);
    std::printf("length mismatch   %llu\n", (unsigned long long)st.mismatch);
    // The three end conditions the specification and the file format actually
    // distinguish, reported separately. See the TERMINATION note above.
    const char* how_it_ended = st.end == ParseEnd::ZeroLengthPrefix ? "zero-length prefix"
                               : st.end == ParseEnd::Exhausted      ? "buffer exhausted at a "
                                                                      "message boundary"
                                                                    : "TRUNCATED mid-message";
    const bool ends_on_c = c.last_type == 'S' && c.last_event_code == 'C';
    std::printf("input ended       %s\n", how_it_ended);
    std::printf("trailing bytes    %zu%s\n", st.trailing, st.trailing ? "   <-- UNREAD" : "");
    std::printf("final message     ");
    if (c.last_type == 'S') {
        std::printf("'S' System Event, code '%c'%s\n", c.last_event_code,
                    ends_on_c ? "  (End of Messages)" : "");
    } else if (c.last_type) {
        std::printf("'%c'\n", c.last_type);
    } else {
        std::printf("none\n");
    }
    std::printf("complete session  %s\n",
                ends_on_c ? "yes  (ends on System Event 'C')"
                          : "NO   <-- the last message is not End of Messages");
    std::printf("first ts          %llu ns\n", (unsigned long long)c.first_ts);
    std::printf("last ts           %llu ns\n", (unsigned long long)c.last_ts);
    std::printf("after 'E' event   %llu\n", (unsigned long long)c.after_end_of_system_hours);
    std::printf("\nper type:\n");
    for (const char* t = kTypeOrder; *t; ++t) {
        const std::uint64_t n = c.counts[static_cast<unsigned char>(*t)];
        if (n) std::printf("  %c  %12llu\n", *t, (unsigned long long)n);
    }

    // Not a benchmark. This is wall-clock time on an unpinned core with the
    // page cache in an arbitrary state; it confirms the run completed and
    // nothing more. Published latency figures come from the benchmark harness
    // on an isolated core, per docs/benchmarks.md.
    std::printf("\n[not a benchmark] %.2fs wall, %.1f M msg/s\n", secs,
                secs > 0 ? (double)st.dispatched / secs / 1e6 : 0.0);

    // Exit status is the gate. A session is acceptable only if the framing
    // understood every byte AND the last message is End of Messages. Either
    // failing alone means the file cannot be relied on.
    const bool ok = st.clean() && ends_on_c;
    std::printf("\nRESULT: %s\n",
                ok ? "session complete and fully parsed" : "SESSION FAILED VERIFICATION");
    return ok ? 0 : 1;
}
