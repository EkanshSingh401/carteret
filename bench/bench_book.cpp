// bench_book -- book-update latency, in two timing modes.
//
// Reports batch-timed and per-message-timed results separately and states the
// gap between them, because they answer different questions and the second one
// perturbs what it measures. A single blended figure would hide both facts.
//
//   batch        one timed region around N messages. Amortises the instrument
//                to nothing, and yields throughput rather than a distribution.
//   per-message  one fenced region per message, with the calibrated instrument
//                cost subtracted. Yields a distribution, at the cost of adding
//                a serialising pair of instructions around a few hundred
//                cycles of work.
//
// Nothing here is published unless the clock is an invariant TSC read through
// fenced rdtsc/rdtscp. On any other host the harness runs and labels its
// output a smoke test, which is what it is: evidence the code executes, not a
// measurement. See docs/benchmarks.md.
//
//   usage: bench_book [--mode batch|per-message|both] [--runs N]
//                     [--warmup N] [--limit N] [--hash POLICY] <session-file>

#include "carteret/bench/timer.hpp"
#include "carteret/fast_book.hpp"
#include "carteret/mapped_file.hpp"
#include "carteret/parser.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <hdr/hdr_histogram.h>

using namespace carteret;
using namespace carteret::bench;

namespace {

struct Options {
    std::string path;
    std::string mode = "both";
    std::string hash = "multiply-shift";
    int runs = 5;
    std::uint64_t warmup = 1000000;
    std::uint64_t limit = 0; // 0 means the whole session
};

constexpr char kTypeOrder[] = "AFECXDUPQB";

// Two synthetic labels alongside the message types. A window recenter rebuilds
// one side of one symbol, so it is rare and its cost is proportional to what
// that side holds rather than to anything about the message that triggered it.
// Pooled into the type histograms it would be invisible at the median and
// would own the tail with no way to say so; separated, the tail can be
// attributed. See docs/design.md record 033.
constexpr unsigned char kSlotRecenter = 0xFEu; // messages that triggered a recenter
constexpr unsigned char kSlotOrdinary = 0xFFu; // every other timed message

// A histogram per message type, recording every sample. hdr_histogram is used
// rather than a vector of raw samples because a session produces tens of
// millions of samples per type: capping a vector would keep only the start of
// the session, which is exactly the part where the book is still filling and
// every structure is unrepresentatively small.
struct TypeHistograms {
    std::array<hdr_histogram*, 256> h{};

    // Range and precision: 1 to 10^7 ticks covers a few milliseconds on any
    // plausible TSC, and 3 significant figures puts the bucket error below
    // 0.1%, finer than the run-to-run spread the method requires be reported.
    TypeHistograms() {
        for (const char* t = kTypeOrder; *t; ++t) {
            hdr_init(1, 10000000, 3, &h[static_cast<unsigned char>(*t)]);
        }
        hdr_init(1, 10000000, 3, &h[kSlotRecenter]);
        hdr_init(1, 10000000, 3, &h[kSlotOrdinary]);
    }
    ~TypeHistograms() {
        for (hdr_histogram* p : h) {
            if (p) hdr_close(p);
        }
    }
    TypeHistograms(const TypeHistograms&) = delete;
    TypeHistograms& operator=(const TypeHistograms&) = delete;

    [[gnu::always_inline]] void record(unsigned char type, std::uint64_t ticks) noexcept {
        hdr_histogram* p = h[type];
        // hdr_histogram cannot record zero in a range starting at one. A
        // sample at or below the instrument's own cost is recorded at the
        // floor rather than discarded, so the sample count stays equal to the
        // message count.
        if (p) hdr_record_value(p, static_cast<std::int64_t>(ticks ? ticks : 1));
    }
};

// Per-message timing wrapper. Opens a fenced region, applies the message to
// the book, closes the region, and files the sample under the message type.
//
// The book is a member rather than a base so that the handler's on() overloads
// are the ones dispatch finds; forwarding explicitly also keeps the timed
// region to exactly the book call.
template<class Book>
struct TimedHandler {
    Book book;
    std::uint64_t overhead = 0;
    TypeHistograms* hist = nullptr;
    std::uint64_t warmup_left = 0;
    std::uint64_t recorded = 0;

    explicit TimedHandler(FastBookConfig cfg, std::uint64_t warmup_msgs)
        : book(cfg), warmup_left(warmup_msgs) {}

    template<class V>
    [[gnu::always_inline]] void timed(V v) {
        if (warmup_left) {
            --warmup_left;
            book.on(v);
            return;
        }
        // The recenter counter is read OUTSIDE the fenced region on both
        // sides, so the two loads are not in what is being measured. Reading
        // it is how a message is classified after the fact: a recenter is
        // triggered from inside the book and there is nothing about the
        // message itself that predicts one.
        const std::uint64_t r0 = book.counters().recenters;
        const std::uint64_t t0 = tick_begin();
        book.on(v);
        const std::uint64_t t1 = tick_end();
        const std::uint64_t r1 = book.counters().recenters;
        const std::uint64_t raw = t1 - t0;
        // The instrument's own cost is subtracted, and a sample at or below it
        // is recorded at the floor rather than wrapping.
        const std::uint64_t net = raw > overhead ? raw - overhead : 0;
        hist->record(v.type(), net);
        hist->record(r1 != r0 ? kSlotRecenter : kSlotOrdinary, net);
        ++recorded;
    }

    void on(SystemEvent v) { book.on(v); }
    void on(AddOrder v) { timed(v); }
    void on(AddOrderMpid v) { timed(v); }
    void on(OrderExecuted v) { timed(v); }
    void on(OrderExecutedPrice v) { timed(v); }
    void on(OrderCancel v) { timed(v); }
    void on(OrderDelete v) { timed(v); }
    void on(OrderReplace v) { timed(v); }
    void on(Trade v) { timed(v); }
    void on(CrossTrade v) { timed(v); }
    void on(BrokenTrade v) { timed(v); }
};

// Batch mode: the book alone, with no per-message instrumentation at all.
template<class Book>
struct PlainHandler {
    Book book;
    explicit PlainHandler(FastBookConfig cfg) : book(cfg) {}
    void on(SystemEvent v) { book.on(v); }
    void on(AddOrder v) { book.on(v); }
    void on(AddOrderMpid v) { book.on(v); }
    void on(OrderExecuted v) { book.on(v); }
    void on(OrderExecutedPrice v) { book.on(v); }
    void on(OrderCancel v) { book.on(v); }
    void on(OrderDelete v) { book.on(v); }
    void on(OrderReplace v) { book.on(v); }
    void on(Trade v) { book.on(v); }
    void on(CrossTrade v) { book.on(v); }
    void on(BrokenTrade v) { book.on(v); }
};

// On a host that cannot produce a publishable number the rows are printed in
// TICKS, not nanoseconds. Multiplying a tick count by ns_per_tick on such a
// host manufactures precision the clock does not have: the development Mac's
// counter runs at 24 MHz, so a tick is 41.667 ns and every sample is an
// integer multiple of it. A p50 printed as "42.0 ns" is one tick and nothing
// more -- it is a resolution floor being reported as a measurement, and two
// operations differing by 30 ns would print identically. Ticks make that
// visible: a p50 of 1 says the median is at or below the clock's resolution.
void print_hist_row(const char* label, const hdr_histogram* h, double scale) {
    if (!h || h->total_count == 0) return;
    const auto q = [&](double pct) {
        return static_cast<double>(hdr_value_at_percentile(h, pct)) * scale;
    };
    std::printf("  %-4s %13lld %9.1f %9.1f %9.1f %9.1f %9.1f %9.1f\n", label,
                static_cast<long long>(h->total_count), q(50.0), q(90.0), q(99.0), q(99.9),
                q(99.99), static_cast<double>(hdr_max(h)) * scale);
}

void print_clock(const ClockInfo& ci) {
    std::printf("clock             %s\n", clock_name(ci.kind));
    std::printf("  constant_tsc    %s\n", ci.constant_tsc ? "yes" : "no");
    std::printf("  nonstop_tsc     %s\n", ci.nonstop_tsc ? "yes" : "no");
    std::printf("  rdtscp          %s\n", ci.rdtscp ? "yes" : "no");
    std::printf("  ns per tick     %.6f  (spread across rounds %.3f%%)\n", ci.ns_per_tick,
                100.0 * ci.calibration_error);
    std::printf("  instrument cost %llu ticks (median of a begin/end pair)\n",
                (unsigned long long)ci.overhead_ticks);
    if (!ci.note.empty()) std::printf("  note            %s\n", ci.note.c_str());
    std::printf("\n");
    if (!ci.publishable()) {
        std::printf("*** SMOKE TEST ONLY ***\n");
        std::printf("This host cannot produce a publishable latency number: it lacks an\n");
        std::printf("invariant TSC read through fenced rdtsc/rdtscp. The numbers below show\n");
        std::printf("that the harness runs. They are not a measurement and do not belong in\n");
        std::printf("docs/benchmarks.md. Run bench/run_linux.sh on the benchmark host.\n\n");
    }
}

template<class Policy>
int run(const Options& opt) {
    const ClockInfo ci = probe_clock();
    MappedFile mf(opt.path);

    FastBookConfig cfg;
    cfg.max_orders = 8u << 20;
    cfg.max_symbols = 1u << 14;
    cfg.index_hint = 8u << 20;

    std::printf("file              %s\n", opt.path.c_str());
    std::printf("bytes             %zu\n", mf.size());
    std::printf("hash policy       %s\n", Policy::name);
    std::printf("order bytes       %zu\n", sizeof(FastOrder));
    std::printf("window ticks      %zu\n", kWindowTicks);
    std::printf("runs              %d\n", opt.runs);
    std::printf("warmup messages   %llu\n", (unsigned long long)opt.warmup);
    std::printf("\n");
    print_clock(ci);

    const bool want_batch = (opt.mode == "batch" || opt.mode == "both");
    const bool want_per = (opt.mode == "per-message" || opt.mode == "both");

    // --- batch mode --------------------------------------------------------
    std::vector<double> batch_ns_per_msg;
    std::uint64_t batch_messages = 0;
    if (want_batch) {
        for (int r = 0; r < opt.runs; ++r) {
            PlainHandler<FastBook<Policy>> h(cfg);
            Parser<PlainHandler<FastBook<Policy>>> parser(h);
            const std::uint64_t t0 = tick_begin();
            const ParseStats st = parser.run(mf.bytes());
            const std::uint64_t t1 = tick_end();
            batch_messages = h.book.counters().book_messages;
            const double ticks = static_cast<double>(t1 - t0);
            const double per =
                batch_messages ? ticks / static_cast<double>(batch_messages) : 0.0;
            batch_ns_per_msg.push_back(per * ci.ns_per_tick);
            (void)st;
        }
        std::sort(batch_ns_per_msg.begin(), batch_ns_per_msg.end());
        std::printf("batch-timed  (one region around the whole session)\n");
        std::printf("  book messages   %llu\n", (unsigned long long)batch_messages);
        std::printf("  ns per message  median %.2f   min %.2f   max %.2f   (%d runs)\n",
                    batch_ns_per_msg[batch_ns_per_msg.size() / 2], batch_ns_per_msg.front(),
                    batch_ns_per_msg.back(), static_cast<int>(batch_ns_per_msg.size()));
        std::printf("\n");
    }

    // --- per-message mode --------------------------------------------------
    double per_median_ns = 0;
    if (want_per) {
        // One set of histograms across all runs, plus an "all types" histogram
        // that the per-type ones are merged into at the end.
        TypeHistograms merged;
        std::uint64_t recorded = 0;
        for (int r = 0; r < opt.runs; ++r) {
            TimedHandler<FastBook<Policy>> h(cfg, opt.warmup);
            h.overhead = ci.overhead_ticks;
            h.hist = &merged;
            Parser<TimedHandler<FastBook<Policy>>> parser(h);
            parser.run(mf.bytes());
            recorded += h.recorded;
        }

        // Nanoseconds on a host that can publish one; otherwise the clock's
        // own resolution quantum, because that is the finest thing it can say.
        const double quantum =
            static_cast<double>(ci.resolution_ticks ? ci.resolution_ticks : 1);
        const double scale = ci.publishable() ? ci.ns_per_tick : 1.0 / quantum;
        const char* unit = ci.publishable() ? "ns" : "ticks";

        std::printf("per-message-timed  (fenced region per message, instrument cost "
                    "subtracted)\n");
        if (!ci.publishable()) {
            std::printf("  RESOLUTION-LIMITED, REPORTED IN TICKS OF THIS CLOCK.\n");
            std::printf(
                "  One tick is %llu of the units this clock reports in (smallest step\n",
                (unsigned long long)ci.resolution_ticks_max);
            std::printf(
                "  seen %llu; the reported unit is finer than the counter behind it, so\n",
                (unsigned long long)ci.resolution_ticks);
            std::printf(
                "  the step alternates). Every sample is a multiple of that quantum.\n");
            std::printf("  p50 of 1 means \"at or below the clock's resolution\", and two\n");
            std::printf("  operations differing by less than one tick print identically.\n");
            std::printf("  Nothing here is a latency measurement. See docs/benchmarks.md.\n");
        }
        std::printf("  every sample from every run is recorded; %d runs, %llu samples\n",
                    opt.runs, (unsigned long long)recorded);
        std::printf("  %-4s %13s %9s %9s %9s %9s %9s %9s   (%s)\n", "type", "samples", "p50",
                    "p90", "p99", "p99.9", "p99.99", "max", unit);

        hdr_histogram* all = nullptr;
        hdr_init(1, 10000000, 3, &all);
        for (const char* t = kTypeOrder; *t; ++t) {
            const hdr_histogram* h = merged.h[static_cast<unsigned char>(*t)];
            if (!h || h->total_count == 0) continue;
            const char label[2] = {*t, 0};
            print_hist_row(label, h, scale);
            if (all) hdr_add(all, h);
        }
        if (all && all->total_count > 0) {
            print_hist_row("all", all, scale);
            per_median_ns =
                static_cast<double>(hdr_value_at_percentile(all, 50.0)) * ci.ns_per_tick;
        }
        if (all) hdr_close(all);
        std::printf("\n");

        // The same samples, split by whether the message triggered a window
        // recenter. Every timed message appears in exactly one of these two
        // rows, and their sample counts sum to the "all" row above. A recenter
        // rebuilds one side of one symbol from its occupancy bitmap, so the
        // question this answers is whether the rebuilds are cheap on average
        // or merely rare -- and whether they own the tail that the per-type
        // rows report but cannot explain.
        const hdr_histogram* rc = merged.h[kSlotRecenter];
        const hdr_histogram* ord = merged.h[kSlotOrdinary];
        const long long rc_n = rc ? static_cast<long long>(rc->total_count) : 0;
        const long long ord_n = ord ? static_cast<long long>(ord->total_count) : 0;
        std::printf("  window recenters, separated (docs/design.md record 033)\n");
        std::printf("  %-4s %13s %9s %9s %9s %9s %9s %9s\n", "kind", "samples", "p50", "p90",
                    "p99", "p99.9", "p99.99", "max");
        print_hist_row("rest", ord, scale);
        print_hist_row("rcnt", rc, scale);
        if (rc_n + ord_n > 0) {
            std::printf("  recenter share  %.4f%% of timed messages\n",
                        100.0 * static_cast<double>(rc_n) / static_cast<double>(rc_n + ord_n));
        }
        if (rc_n == 0) {
            std::printf("  no message in this run triggered a recenter\n");
        }
        std::printf("\n");
    }

    // The gap between the two modes is the instrument's effect, and naming it
    // is worth more than most of the optimisations it is used to evaluate.
    if (want_batch && want_per && !batch_ns_per_msg.empty() && per_median_ns > 0) {
        const double batch = batch_ns_per_msg[batch_ns_per_msg.size() / 2];
        std::printf("mode gap          batch %.2f ns vs per-message p50 %.2f ns "
                    "(%+.1f%%)\n",
                    batch, per_median_ns, 100.0 * (per_median_ns - batch) / batch);
        std::printf("  instrument      %llu ticks = %.2f ns, subtracted from every "
                    "per-message sample\n",
                    (unsigned long long)ci.overhead_ticks,
                    static_cast<double>(ci.overhead_ticks) * ci.ns_per_tick);
        std::printf("  Batch amortises the instrument to nothing but yields no "
                    "distribution;\n"
                    "  per-message yields a distribution and serialises around every "
                    "update.\n");
    }

    std::printf("\n%s\n", ci.publishable()
                              ? "Numbers above are from an invariant TSC and may be published."
                              : "SMOKE TEST: nothing above is publishable.");
    return ci.publishable() ? 0 : 0; // a smoke run is not a failure
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
        if (std::strcmp(argv[i], "--mode") == 0)
            opt.mode = next("--mode");
        else if (std::strcmp(argv[i], "--hash") == 0)
            opt.hash = next("--hash");
        else if (std::strcmp(argv[i], "--runs") == 0)
            opt.runs = std::atoi(next("--runs"));
        else if (std::strcmp(argv[i], "--warmup") == 0)
            opt.warmup = std::strtoull(next("--warmup"), nullptr, 10);
        else if (std::strcmp(argv[i], "--limit") == 0)
            opt.limit = std::strtoull(next("--limit"), nullptr, 10);
        else
            opt.path = argv[i];
    }
    if (opt.path.empty() || opt.runs < 1) {
        std::fprintf(stderr,
                     "usage: %s [--mode batch|per-message|both] [--runs N] [--warmup N]\n"
                     "          [--limit N] [--hash identity|multiply-shift|std] "
                     "<session-file>\n",
                     argv[0]);
        return 2;
    }
    if (opt.hash == "identity") return run<IdentityHash>(opt);
    if (opt.hash == "multiply-shift") return run<MultiplyShiftHash>(opt);
    if (opt.hash == "std") return run<StdHash>(opt);
    std::fprintf(stderr, "unknown hash policy: %s\n", opt.hash.c_str());
    return 2;
}
