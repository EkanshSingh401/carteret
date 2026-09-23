// replay -- runs the differential harness over a session file.
//
// Correctness layer 4 against real data. Drives the reference book and the
// fast book from the same message stream and compares them after every
// message, then compares the entire book at end of session.
//
//   usage: replay [--hash identity|multiply-shift|std] [--full-every N]
//                 [--invariant-every N] [--max-orders N] [--max-symbols N]
//                 <session-file>
//
// Structural invariants are checked every --invariant-every messages, which
// defaults to a small interval in debug builds and a large one in release,
// because the check walks every level of every symbol and cannot run per
// message on a full session.
//
// Exit status is nonzero on any divergence, any invariant violation, or any
// capacity exhaustion, so the tool is usable as a gate.

#include "carteret/differential.hpp"
#include "carteret/mapped_file.hpp"
#include "carteret/parser.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace carteret;

namespace {

struct Options {
    std::string path;
    std::string hash = "multiply-shift";
    std::uint64_t full_every = 1u << 22;
    std::uint64_t invariant_every = Differential<>::kDefaultInvariantEvery;
    FastBookConfig cfg;
};

void print_counters(const ReferenceBook& ref, const FastCounters& f) {
    const RefCounters& r = ref.counters();
    std::printf("\nfeed conditions (counted, never repaired -- docs/design.md record 007):\n");
    std::printf("  %-28s %14s %14s\n", "", "reference", "fast");
    auto row = [](const char* name, std::uint64_t a, std::uint64_t b) {
        std::printf("  %-28s %14llu %14llu%s\n", name, (unsigned long long)a,
                    (unsigned long long)b, a == b ? "" : "   <-- DIFFERS");
    };
    row("orphan execute", r.orphan_execute, f.orphan_execute);
    row("orphan execute-with-price", r.orphan_execute_price, f.orphan_execute_price);
    row("orphan cancel", r.orphan_cancel, f.orphan_cancel);
    row("orphan delete", r.orphan_delete, f.orphan_delete);
    row("orphan replace", r.orphan_replace, f.orphan_replace);
    row("duplicate add", r.duplicate_add, f.duplicate_add);
    row("zero-share cross trade", r.zero_share_cross, f.zero_share_cross);
    row("removed at zero shares", r.removed_at_zero, f.removed_at_zero);
    row("over-execution", r.over_execute, f.over_execute);
    row("crossed observations", r.crossed_observations, f.crossed_observations);
    row("locked observations", r.locked_observations, f.locked_observations);
    row("after end of system hours", r.after_end_of_system_hours, f.after_end_of_system_hours);
    row("book messages", r.book_messages, f.book_messages);

    std::printf("\nfast book structure:\n");
    std::printf("  window ticks               %14zu\n", kWindowTicks);
    std::printf("  order record bytes         %14zu\n", sizeof(FastOrder));
    std::printf("  overflow hits              %14llu\n", (unsigned long long)f.overflow_hits);
    std::printf("  window recenters           %14llu\n", (unsigned long long)f.recenters);
    std::printf("  levels moved by recenters  %14llu\n", (unsigned long long)f.levels_moved);
    std::printf("  sub-cent prices            %14llu\n", (unsigned long long)f.sub_cent_prices);
    std::printf("  pool exhausted             %14llu\n", (unsigned long long)f.pool_exhausted);
    std::printf("  symbol table overflow      %14llu\n", (unsigned long long)f.symbol_overflow);
    std::printf("  index insert failures      %14llu\n", (unsigned long long)f.index_failures);
}

template<class Policy>
int run(const Options& opt) {
    MappedFile mf(opt.path);
    Differential<Policy> d(opt.cfg, opt.full_every, opt.invariant_every);
    Parser<Differential<Policy>> parser(d);

    const auto t0 = std::chrono::steady_clock::now();
    const ParseStats st = parser.run(mf.bytes());
    const bool full_ok = d.compare_everything();
    const auto t1 = std::chrono::steady_clock::now();
    const double secs = std::chrono::duration<double>(t1 - t0).count();

    std::printf("file              %s\n", opt.path.c_str());
    std::printf("hash policy       %s\n", Policy::name);
    std::printf("framing           %s\n",
                st.framing == Framing::LengthPrefixed ? "length-prefixed" : "zero-prefixed");
    std::printf("messages parsed   %llu\n", (unsigned long long)st.dispatched);
    std::printf("messages compared %llu\n", (unsigned long long)d.stats().messages);
    std::printf("level compares    %llu\n", (unsigned long long)d.stats().level_comparisons);
    std::printf("BBO compares      %llu\n", (unsigned long long)d.stats().bbo_comparisons);
    std::printf("full compares     %llu\n", (unsigned long long)d.stats().full_comparisons);
    std::printf("invariant checks  %llu  (every %llu messages)\n",
                (unsigned long long)d.stats().invariant_checks,
                (unsigned long long)opt.invariant_every);
    std::printf("unknown type      %llu\n", (unsigned long long)st.unknown);
    std::printf("length mismatch   %llu\n", (unsigned long long)st.mismatch);
    std::printf("input ended       %s\n",
                st.end == ParseEnd::ZeroLengthPrefix ? "zero-length prefix"
                : st.end == ParseEnd::Exhausted      ? "buffer exhausted at a message boundary"
                                                     : "TRUNCATED mid-message");
    std::printf("trailing bytes    %zu\n", st.trailing);
    std::printf("live orders       ref %zu / fast %zu\n", d.reference().live_orders(),
                d.fast().live_orders());
    std::printf("index load factor %.3f, mean probe %.3f\n", d.fast().index().load_factor(),
                d.fast().index().mean_probe_length());

    print_counters(d.reference(), d.fast().counters());

    std::printf("\ninvariants:\n");
    const std::size_t ref_bad = d.reference().check_invariants();
    const std::size_t fast_bad = d.fast().check_invariants();
    std::printf("  reference violations       %14zu\n", ref_bad);
    std::printf("  fast violations            %14zu\n", fast_bad);

    std::printf("\n");
    d.report(stdout);

    // Not a benchmark: an unpinned core with the page cache in an arbitrary
    // state, and both books running. See docs/benchmarks.md.
    std::printf("\n[not a benchmark] %.1fs wall for the differential replay\n", secs);

    // A single venue's own displayed book cannot lock or cross itself during
    // continuous trading: an incoming order that would cross executes against
    // the resting side instead of resting. Locked and crossed markets are an
    // inter-venue phenomenon in the consolidated quote, not a property of one
    // book. So a nonzero count here is a RECONSTRUCTION ERROR -- a missed
    // removal, a misapplied replace, a stale level -- and it fails the gate.
    // See docs/design.md record 027.
    const RefCounters& rc = d.reference().counters();
    const FastCounters& fc = d.fast().counters();
    // The gate (record 036). A crossed or locked book is a reconstruction
    // error only where the venue was matching the symbol. Two states excuse
    // it, and both are read from the feed rather than assumed:
    //   - the symbol is not in trading state 'T' -- halted, paused, or in a
    //     quotation-only period, so nothing executes against the resting book;
    //   - the symbol has resumed but its reopening cross has not yet run, so
    //     the accumulated book has not been matched.
    // Anything else fails. The fast book carries no trading state, so it is
    // held to agreement with the reference book's totals instead, which is
    // what keeps it inside the check.
    const bool books_agree = rc.crossed_observations == fc.crossed_observations &&
                             rc.locked_observations == fc.locked_observations;
    const bool no_crossing =
        rc.crossed_unexplained == 0 && rc.locked_unexplained == 0 && books_agree;

    const bool ok = !d.divergence().found && full_ok && ref_bad == 0 && fast_bad == 0 &&
                    no_crossing && st.end != ParseEnd::Truncated && st.trailing == 0 &&
                    fc.pool_exhausted == 0 && fc.index_failures == 0 && fc.symbol_overflow == 0;

    // Reported whenever they occur, not only on failure: a rate that changes
    // between sessions is worth seeing even when every observation is excused.
    if (rc.crossed_observations || rc.locked_observations) {
        const auto hms = [](std::uint64_t ns, char* buf, std::size_t n) {
            const std::uint64_t s_ = ns / 1'000'000'000ULL;
            std::snprintf(buf, n, "%02llu:%02llu:%02llu", (unsigned long long)(s_ / 3600),
                          (unsigned long long)(s_ / 60 % 60), (unsigned long long)(s_ % 60));
        };
        char c0[16], c1[16], l0[16], l1[16];
        hms(rc.crossed_first_ts, c0, sizeof c0);
        hms(rc.crossed_last_ts, c1, sizeof c1);
        hms(rc.locked_first_ts, l0, sizeof l0);
        hms(rc.locked_last_ts, l1, sizeof l1);
        std::printf("\ncrossed and locked observations (docs/design.md record 036):\n");
        std::printf("  %-26s %12s %12s\n", "", "crossed", "locked");
        std::printf("  %-26s %12llu %12llu\n", "total",
                    (unsigned long long)rc.crossed_observations,
                    (unsigned long long)rc.locked_observations);
        std::printf("  %-26s %12llu %12llu\n", "  symbol not trading",
                    (unsigned long long)rc.crossed_while_not_trading,
                    (unsigned long long)rc.locked_while_not_trading);
        std::printf("  %-26s %12llu %12llu\n", "  awaiting reopening cross",
                    (unsigned long long)rc.crossed_awaiting_reopen,
                    (unsigned long long)rc.locked_awaiting_reopen);
        std::printf("  %-26s %12llu %12llu   <- fails the gate\n", "  unexplained",
                    (unsigned long long)rc.crossed_unexplained,
                    (unsigned long long)rc.locked_unexplained);
        std::printf("  %-26s %12llu %12llu\n", "  in continuous trading",
                    (unsigned long long)rc.crossed_continuous,
                    (unsigned long long)rc.locked_continuous);
        std::printf("  %-26s %12s %12s\n", "  first", c0, l0);
        std::printf("  %-26s %12s %12s\n", "  last", c1, l1);
        std::printf("  %-26s %12llu %12llu\n", "  distinct symbols",
                    (unsigned long long)rc.crossed_symbols.size(),
                    (unsigned long long)rc.locked_symbols.size());
        std::printf("  deepest crossing          %12llu ticks\n",
                    (unsigned long long)rc.crossed_worst_ticks);
        std::printf("  reopening windows         %12llu, longest %.3f s\n",
                    (unsigned long long)rc.reopen_windows,
                    static_cast<double>(rc.reopen_window_max_ns) / 1e9);
    }
    if (!no_crossing) {
        std::fprintf(stderr,
                     "\nLOCKED OR CROSSED BOOK: %llu crossed and %llu locked observations\n"
                     "that the feed does not excuse, across %llu symbols.\n"
                     "A venue that is matching a symbol cannot let its own displayed book\n"
                     "cross, so this is a reconstruction error. See docs/design.md record\n"
                     "036. Books agree on the totals: %s.\n",
                     (unsigned long long)rc.crossed_unexplained,
                     (unsigned long long)rc.locked_unexplained,
                     (unsigned long long)rc.unexplained_symbols.size(),
                     books_agree ? "yes"
                                 : "NO -- the two books disagree, which is its own bug");
    }
    std::printf("%s\n", ok ? "RESULT: identical" : "RESULT: FAILED");
    return ok ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    Options opt;
    for (int i = 1; i < argc; ++i) {
        const auto next = [&](const char* flag) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s needs a value\n", flag);
                std::exit(2);
            }
            return argv[++i];
        };
        if (std::strcmp(argv[i], "--hash") == 0)
            opt.hash = next("--hash");
        else if (std::strcmp(argv[i], "--full-every") == 0)
            opt.full_every = std::strtoull(next("--full-every"), nullptr, 10);
        else if (std::strcmp(argv[i], "--invariant-every") == 0)
            opt.invariant_every = std::strtoull(next("--invariant-every"), nullptr, 10);
        else if (std::strcmp(argv[i], "--max-orders") == 0)
            opt.cfg.max_orders = std::strtoull(next("--max-orders"), nullptr, 10);
        else if (std::strcmp(argv[i], "--max-symbols") == 0)
            opt.cfg.max_symbols = std::strtoull(next("--max-symbols"), nullptr, 10);
        else
            opt.path = argv[i];
    }
    if (opt.path.empty()) {
        std::fprintf(stderr,
                     "usage: %s [--hash identity|multiply-shift|std] [--full-every N]\n"
                     "          [--invariant-every N] [--max-orders N] [--max-symbols N]\n"
                     "          <session-file>\n",
                     argv[0]);
        return 2;
    }

    if (opt.hash == "identity") return run<IdentityHash>(opt);
    if (opt.hash == "multiply-shift") return run<MultiplyShiftHash>(opt);
    if (opt.hash == "std") return run<StdHash>(opt);
    std::fprintf(stderr, "unknown hash policy: %s\n", opt.hash.c_str());
    return 2;
}
