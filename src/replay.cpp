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
    std::printf("end-of-session    %s\n",
                st.end == ParseEnd::EndOfSession ? "yes" : "no  (truncated or malformed)");
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

    const bool ok =
        !d.divergence().found && full_ok && ref_bad == 0 && fast_bad == 0 &&
        st.end == ParseEnd::EndOfSession && d.fast().counters().pool_exhausted == 0 &&
        d.fast().counters().index_failures == 0 && d.fast().counters().symbol_overflow == 0;
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
