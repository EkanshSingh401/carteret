// bench_book -- book-update latency, and where its misses come from.
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
//   attribute    no timing. Each message is bracketed by rdpmc reads of the
//                demand-fill counters validated in docs/design.md record 040,
//                and the fills are filed by message type, by whether the
//                message triggered a window recenter, and by the age of the
//                order it names. See attribution.hpp.
//   spsc         the replay split across two cores through a ring: one parses,
//                the other applies. Batch and per-message, as above, measured
//                on the applying core. See spsc.hpp.
//
// --verify replays the session through the differential harness with the
// selected policy before anything is timed, so a variant is known to produce
// the reference book's state on this exact session before its speed means
// anything. --region-map writes the address range of every book structure,
// for bench/perf_mem_attribute.py to resolve perf mem's sampled addresses.
//
// Nothing here is published unless the clock is an invariant TSC read through
// fenced rdtsc/rdtscp. On any other host the harness runs and labels its
// output a smoke test, which is what it is: evidence the code executes, not a
// measurement. See docs/benchmarks.md.
//
//   usage: bench_book [--mode batch|per-message|both|attribute|spsc] [--runs N]
//                     [--warmup N] [--hash POLICY] [--verify] [--no-age]
//                     [--core N] [--producer-core N] [--region-map FILE]
//                     <session-file>

#include "alloc_map.hpp"
#include "attribution.hpp"
#include "chained_index.hpp"
#include "pmc.hpp"
#include "spsc.hpp"

#include "carteret/bench/timer.hpp"
#include "carteret/differential.hpp"
#include "carteret/fast_book.hpp"
#include "carteret/mapped_file.hpp"
#include "carteret/parser.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#include <hdr/hdr_histogram.h>

#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#endif
#include <sys/resource.h>

using namespace carteret;
using namespace carteret::bench;

namespace {

struct Options {
    std::string path;
    std::string mode = "both";
    std::string hash = "multiply-shift";
    std::string region_map;
    int runs = 5;
    std::uint64_t warmup = 1000000;
    bool verify = false;
    bool no_age = false;
    int core = -1;
    int producer_core = -1;
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

// Pins the calling thread. Pinning is a request the kernel honours on an
// isolated core; the entry records what was asked for either way.
bool pin_to(int core) {
#if defined(__linux__)
    if (core < 0) return true;
    cpu_set_t s;
    CPU_ZERO(&s);
    CPU_SET(core, &s);
    return pthread_setaffinity_np(pthread_self(), sizeof s, &s) == 0;
#else
    (void)core;
    return false;
#endif
}

// Page faults taken inside timed regions. The session is mapped, so a page not
// yet resident is a fault inside the measurement; the untimed structures pass
// touches every page first, and this confirms it rather than assuming it.
struct Faults {
    long major = 0;
    long minor = 0;
};
Faults faults_now() {
    rusage ru{};
    getrusage(RUSAGE_SELF, &ru);
    return {ru.ru_majflt, ru.ru_minflt};
}

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

// --- the SPSC split ---------------------------------------------------------

using Ring = SpscRing<1024>;

// Producer side: parses and publishes a pointer to every message the book
// handles. A null pointer marks the end of the session.
struct Publisher {
    Ring* ring;
    template<class V>
    [[gnu::always_inline]] void pub(V v) noexcept {
        ring->push(v.p);
    }
    void on(SystemEvent v) { pub(v); }
    void on(AddOrder v) { pub(v); }
    void on(AddOrderMpid v) { pub(v); }
    void on(OrderExecuted v) { pub(v); }
    void on(OrderExecutedPrice v) { pub(v); }
    void on(OrderCancel v) { pub(v); }
    void on(OrderDelete v) { pub(v); }
    void on(OrderReplace v) { pub(v); }
    void on(Trade v) { pub(v); }
    void on(CrossTrade v) { pub(v); }
    void on(BrokenTrade v) { pub(v); }
};

// Consumer side: the same dispatch the parser performs, from the type byte of
// a message the producer has already framed and validated.
template<class H>
[[gnu::always_inline]] inline void apply(H& h, const unsigned char* p) {
    using carteret::detail::view;
    switch (p[off::kType]) {
    case 'S': h.on(view<SystemEvent>(p)); break;
    case 'A': h.on(view<AddOrder>(p)); break;
    case 'F': h.on(view<AddOrderMpid>(p)); break;
    case 'E': h.on(view<OrderExecuted>(p)); break;
    case 'C': h.on(view<OrderExecutedPrice>(p)); break;
    case 'X': h.on(view<OrderCancel>(p)); break;
    case 'D': h.on(view<OrderDelete>(p)); break;
    case 'U': h.on(view<OrderReplace>(p)); break;
    case 'P': h.on(view<Trade>(p)); break;
    case 'Q': h.on(view<CrossTrade>(p)); break;
    case 'B': h.on(view<BrokenTrade>(p)); break;
    default: break;
    }
}

// Runs one session through the ring. The calling thread is the consumer and
// is pinned to consumer_core; the producer thread is pinned to producer_core.
// Returns the ticks from before the producer starts to after the consumer
// applies the last message.
template<class H>
std::uint64_t run_spsc(H& h, const MappedFile& mf, int consumer_core, int producer_core) {
    auto ring = std::make_unique<Ring>();
    pin_to(consumer_core);
    const std::uint64_t t0 = tick_begin();
    std::thread producer([&] {
        pin_to(producer_core);
        Publisher pub{ring.get()};
        Parser<Publisher> parser(pub);
        parser.run(mf.bytes());
        ring->push(nullptr);
    });
    for (;;) {
        const unsigned char* p = ring->pop();
        if (!p) break;
        apply(h, p);
    }
    const std::uint64_t t1 = tick_end();
    producer.join();
    return t1 - t0;
}

// --- reporting --------------------------------------------------------------

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

// Prints the per-type and recenter-separated distributions. Returns the p50
// across all types in nanoseconds.
double print_per_message(const char* title, const TypeHistograms& merged, const ClockInfo& ci,
                         int runs, std::uint64_t recorded) {
    // Nanoseconds on a host that can publish one; otherwise the clock's own
    // resolution quantum, because that is the finest thing it can say.
    const double quantum = static_cast<double>(ci.resolution_ticks ? ci.resolution_ticks : 1);
    const double scale = ci.publishable() ? ci.ns_per_tick : 1.0 / quantum;
    const char* unit = ci.publishable() ? "ns" : "ticks";

    std::printf("%s\n", title);
    if (!ci.publishable()) {
        std::printf("  RESOLUTION-LIMITED, REPORTED IN TICKS OF THIS CLOCK.\n");
        std::printf("  One tick is %llu of the units this clock reports in (smallest step\n",
                    (unsigned long long)ci.resolution_ticks_max);
        std::printf("  seen %llu; the reported unit is finer than the counter behind it, so\n",
                    (unsigned long long)ci.resolution_ticks);
        std::printf("  the step alternates). Every sample is a multiple of that quantum.\n");
        std::printf("  p50 of 1 means \"at or below the clock's resolution\", and two\n");
        std::printf("  operations differing by less than one tick print identically.\n");
        std::printf("  Nothing here is a latency measurement. See docs/benchmarks.md.\n");
    }
    std::printf("  every sample from every run is recorded; %d runs, %llu samples\n", runs,
                (unsigned long long)recorded);
    std::printf("  %-4s %13s %9s %9s %9s %9s %9s %9s   (%s)\n", "type", "samples", "p50", "p90",
                "p99", "p99.9", "p99.99", "max", unit);

    double p50_ns = 0;
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
        p50_ns = static_cast<double>(hdr_value_at_percentile(all, 50.0)) * ci.ns_per_tick;
    }
    if (all) hdr_close(all);
    std::printf("\n");

    // The same samples, split by whether the message triggered a window
    // recenter. Every timed message appears in exactly one of these two rows,
    // and their sample counts sum to the "all" row above. A recenter rebuilds
    // one side of one symbol from its occupancy bitmap, so the question this
    // answers is whether the rebuilds are cheap on average or merely rare --
    // and whether they own the tail that the per-type rows report but cannot
    // explain.
    const hdr_histogram* rc = merged.h[kSlotRecenter];
    const hdr_histogram* ord = merged.h[kSlotOrdinary];
    const long long rc_n = rc ? static_cast<long long>(rc->total_count) : 0;
    const long long ord_n = ord ? static_cast<long long>(ord->total_count) : 0;
    std::printf("  window recenters, separated (docs/design.md record 033)\n");
    std::printf("  %-4s %13s %9s %9s %9s %9s %9s %9s\n", "kind", "samples", "p50", "p90", "p99",
                "p99.9", "p99.99", "max");
    print_hist_row("rest", ord, scale);
    print_hist_row("rcnt", rc, scale);
    if (rc_n + ord_n > 0) {
        std::printf("  recenter share  %.4f%% of timed messages\n",
                    100.0 * static_cast<double>(rc_n) / static_cast<double>(rc_n + ord_n));
    }
    if (rc_n == 0) std::printf("  no message in this run triggered a recenter\n");
    std::printf("\n");
    return p50_ns;
}

void print_batch(const char* title, std::vector<double>& ns_per_msg, std::uint64_t messages) {
    std::sort(ns_per_msg.begin(), ns_per_msg.end());
    std::printf("%s\n", title);
    std::printf("  book messages   %llu\n", (unsigned long long)messages);
    std::printf("  ns per message  median %.2f   min %.2f   max %.2f   (%d runs)\n",
                ns_per_msg[ns_per_msg.size() / 2], ns_per_msg.front(), ns_per_msg.back(),
                static_cast<int>(ns_per_msg.size()));
    std::printf("\n");
}

void print_fill_header(const char* first) {
    std::printf("  %-10s %13s", first, "messages");
    for (int e = 0; e < kFillEventCount; ++e) std::printf(" %8s", kFillEvents[e].name);
    std::printf(" %8s %8s\n", ">=1 dram", "dram shr");
}

void print_fill_row(const char* label, const FillTotals& f, std::uint64_t total_dram) {
    if (f.messages == 0) return;
    std::printf("  %-10s %13llu", label, (unsigned long long)f.messages);
    for (int e = 0; e < kFillEventCount; ++e) std::printf(" %8.4f", f.per_message(e));
    const double any_dram =
        1.0 - static_cast<double>(f.dram_hist[0]) / static_cast<double>(f.messages);
    std::printf(" %7.2f%% %7.2f%%\n", 100.0 * any_dram,
                total_dram
                    ? 100.0 * static_cast<double>(f.fills[0]) / static_cast<double>(total_dram)
                    : 0.0);
}

template<class Book>
void print_attribution(const AttributedHandler<Book>& h, bool with_age) {
    const std::uint64_t total_dram = h.all.fills[0];
    std::printf("  fills per message by source; \">=1 dram\" is the share of messages taking\n"
                "  at least one DRAM fill, \"dram shr\" the share of all DRAM fills\n");
    print_fill_header("type");
    for (const char* t = kTypeOrder; *t; ++t) {
        const char label[2] = {*t, 0};
        print_fill_row(label, h.by_type[static_cast<unsigned char>(*t)], total_dram);
    }
    print_fill_row("all", h.all, total_dram);
    print_fill_row("recenter", h.recenter, total_dram);
    print_fill_row("rest", h.ordinary, total_dram);
    std::printf(
        "  DRAM fills per message, distribution: 0 %.2f%%  1 %.2f%%  2 %.2f%%  3 "
        "%.2f%%  4+ %.2f%%\n",
        100.0 * static_cast<double>(h.all.dram_hist[0]) / static_cast<double>(h.all.messages),
        100.0 * static_cast<double>(h.all.dram_hist[1]) / static_cast<double>(h.all.messages),
        100.0 * static_cast<double>(h.all.dram_hist[2]) / static_cast<double>(h.all.messages),
        100.0 * static_cast<double>(h.all.dram_hist[3]) / static_cast<double>(h.all.messages),
        100.0 * static_cast<double>(h.all.dram_hist[4]) / static_cast<double>(h.all.messages));
    std::printf("  peak live orders %zu\n\n", h.peak_live);
    if (!with_age) return;

    // By the age of the order the message names. Only the types that name a
    // resting order appear.
    std::printf("  by age of the referenced order (record 015's buckets)\n");
    print_fill_header("type/age");
    for (const char t : {'E', 'C', 'X', 'D', 'U'}) {
        for (int a = 0; a < kAgeBuckets; ++a) {
            char label[16];
            std::snprintf(label, sizeof label, "%c %s", t, kAgeLabels[a]);
            print_fill_row(
                label,
                h.by_type_age[static_cast<unsigned char>(t)][static_cast<std::size_t>(a)],
                total_dram);
        }
    }
    FillTotals pooled[kAgeBuckets];
    for (const char t : {'E', 'C', 'X', 'D', 'U'}) {
        for (int a = 0; a < kAgeBuckets; ++a) {
            const FillTotals& f =
                h.by_type_age[static_cast<unsigned char>(t)][static_cast<std::size_t>(a)];
            pooled[a].messages += f.messages;
            for (std::size_t e = 0; e < static_cast<std::size_t>(kFillEventCount); ++e)
                pooled[a].fills[e] += f.fills[e];
            for (std::size_t k = 0; k < 5; ++k) pooled[a].dram_hist[k] += f.dram_hist[k];
        }
    }
    for (int a = 0; a < kAgeBuckets; ++a) {
        char label[16];
        std::snprintf(label, sizeof label, "* %s", kAgeLabels[a]);
        print_fill_row(label, pooled[a], total_dram);
    }
    std::printf("\n");
}

// Names the book's large allocations in the order its constructor makes them.
template<class Policy>
std::vector<const char*> structure_labels() {
    if constexpr (std::is_same_v<Policy, ChainedMultiplyShift>) {
        return {"index-heads", "index-nodes", "pool", "refs", "symbols"};
    } else {
        return {"index", "pool", "refs", "symbols"};
    }
}

// The size of one overflow map node, learned from the allocator rather than
// from the standard library's internals.
std::size_t overflow_node_bytes() {
    std::map<Price, FastLevel> probe;
    alloc_map_learn();
    probe[0] = FastLevel{};
    return alloc_map_learned();
}

// Reports the reserved size of every structure, from the allocations the
// constructor and the pass actually made, and optionally writes them as a
// region map.
template<class Policy>
void report_structures(const MappedFile& mf, const std::string& region_map,
                       std::size_t node_bytes) {
    const std::size_t n = alloc_map_count();
    const AllocRecord* rec = alloc_map_records();
    const auto labels = structure_labels<Policy>();
    constexpr std::size_t window_bytes = kWindowTicks * sizeof(FastLevel);

    // Large allocations are named in the order they were made. The small ones
    // are deduplicated by address, because a freed overflow node is handed
    // back out by the allocator and would otherwise be counted again.
    std::vector<std::pair<const char*, AllocRecord>> named;
    std::vector<AllocRecord> small;
    std::size_t big = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if (rec[i].bytes == window_bytes || rec[i].bytes == node_bytes) {
            small.push_back(rec[i]);
            continue;
        }
        named.emplace_back(big < labels.size() ? labels[big] : "other-large", rec[i]);
        ++big;
    }
    std::sort(small.begin(), small.end(),
              [](const AllocRecord& a, const AllocRecord& b) { return a.addr < b.addr; });
    small.erase(std::unique(small.begin(), small.end(),
                            [](const AllocRecord& a, const AllocRecord& b) {
                                return a.addr == b.addr;
                            }),
                small.end());
    std::size_t windows = 0, nodes = 0;
    for (const AllocRecord& r : small) (r.bytes == window_bytes ? windows : nodes)++;

    std::printf("structures (reserved; the L3 of one CCD is 32 MB)\n");
    std::size_t total = 0;
    for (const auto& [label, r] : named) {
        std::printf("  %-12s %10.1f MB\n", label, static_cast<double>(r.bytes) / 1048576.0);
        total += r.bytes;
    }
    std::printf("  %-12s %10.1f MB  (%zu windows of %zu bytes)\n", "levels",
                static_cast<double>(windows * window_bytes) / 1048576.0, windows, window_bytes);
    std::printf("  %-12s %10.1f MB  (%zu distinct nodes of %zu bytes)\n", "overflow",
                static_cast<double>(nodes * node_bytes) / 1048576.0, nodes, node_bytes);
    total += windows * window_bytes + nodes * node_bytes;
    std::printf("  %-12s %10.1f MB\n", "total", static_cast<double>(total) / 1048576.0);
    if (alloc_map_overflowed())
        std::printf("  (allocation log filled; small allocations undercounted)\n");
    std::printf("\n");

    if (region_map.empty()) return;
    std::FILE* f = std::fopen(region_map.c_str(), "w");
    if (!f) {
        std::fprintf(stderr, "cannot write %s\n", region_map.c_str());
        return;
    }
    // Where the occupancy bitmap sits inside a symbol, so a sample in the
    // symbol table can be split into bitmap and everything else.
    FastSide side;
    const auto bitmap_off = static_cast<std::size_t>(
        reinterpret_cast<const char*>(&side.occupied) - reinterpret_cast<const char*>(&side));
    std::fprintf(f, "layout symbol_bytes %zu side_bytes %zu bitmap_off %zu bitmap_bytes %zu\n",
                 sizeof(FastSymbol), sizeof(FastSide), bitmap_off, sizeof(LevelBitmap));
    std::fprintf(f, "region input %#zx %#zx\n",
                 reinterpret_cast<std::size_t>(mf.bytes().data()),
                 reinterpret_cast<std::size_t>(mf.bytes().data()) + mf.size());
    for (const auto& [label, r] : named) {
        std::fprintf(f, "region %s %#zx %#zx\n", label, static_cast<std::size_t>(r.addr),
                     static_cast<std::size_t>(r.addr + r.bytes));
    }
    for (const AllocRecord& r : small) {
        std::fprintf(
            f, "region %s %#zx %#zx\n", r.bytes == window_bytes ? "levels" : "overflow",
            static_cast<std::size_t>(r.addr), static_cast<std::size_t>(r.addr + r.bytes));
    }
    std::fclose(f);
    std::printf("region map written to %s\n\n", region_map.c_str());
}

template<class Policy>
bool verify(const MappedFile& mf, FastBookConfig cfg) {
    auto d = std::make_unique<Differential<Policy>>(cfg, 1u << 20);
    Parser<Differential<Policy>> parser(*d);
    parser.run(mf.bytes());
    const bool ok = !d->divergence().found && d->compare_everything();
    if (d->divergence().found) d->report(stderr);
    std::printf("verify            %s against the reference book, %llu messages compared, "
                "index failures %llu, pool exhausted %llu\n\n",
                ok ? "RESULT: identical" : "RESULT: FAILED",
                (unsigned long long)d->stats().messages,
                (unsigned long long)d->fast().counters().index_failures,
                (unsigned long long)d->fast().counters().pool_exhausted);
    return ok && d->fast().counters().index_failures == 0 &&
           d->fast().counters().pool_exhausted == 0;
}

template<class Policy>
int run(const Options& opt) {
    if (opt.core >= 0 && !pin_to(opt.core)) {
        std::fprintf(stderr, "could not pin to core %d\n", opt.core);
        return 1;
    }
    const ClockInfo ci = probe_clock();
    MappedFile mf(opt.path);

    FastBookConfig cfg;
    cfg.max_orders = 8u << 20;
    cfg.max_symbols = 1u << 14;
    cfg.index_hint = 8u << 20;

    std::printf("file              %s\n", opt.path.c_str());
    std::printf("bytes             %zu\n", mf.size());
    std::printf("mode              %s\n", opt.mode.c_str());
    std::printf("hash policy       %s\n", Policy::name);
    std::printf("order bytes       %zu\n", sizeof(FastOrder));
    std::printf("window ticks      %zu\n", kWindowTicks);
    std::printf("runs              %d\n", opt.runs);
    std::printf("warmup messages   %llu\n", (unsigned long long)opt.warmup);
    if (opt.core >= 0) std::printf("core              %d\n", opt.core);
    if (opt.mode == "spsc") std::printf("producer core     %d\n", opt.producer_core);
    std::printf("\n");
    print_clock(ci);

    if (opt.verify && !verify<Policy>(mf, cfg)) {
        std::printf("verification failed; nothing is timed against a book that disagrees\n");
        return 1;
    }

    // One untimed pass with the allocation log on, to report the structures'
    // sizes -- and, with --region-map, their addresses -- as this build lays
    // them out. The windows are allocated as symbols first trade, so the log
    // stays on for the whole pass.
    {
        const std::size_t node_bytes = overflow_node_bytes();
        alloc_map_start(kWindowTicks * sizeof(FastLevel), node_bytes);
        auto h = std::make_unique<PlainHandler<FastBook<Policy>>>(cfg);
        Parser<PlainHandler<FastBook<Policy>>> parser(*h);
        parser.run(mf.bytes());
        alloc_map_stop();
        report_structures<Policy>(mf, opt.region_map, node_bytes);
        const auto& c = h->book.counters();
        std::printf("index             load factor %.3f at end, mean probe length %.3f\n",
                    h->book.index().load_factor(), h->book.index().mean_probe_length());
        std::printf("book counters     overflow %.2f%%, recenters %llu, orphans %llu, index "
                    "failures %llu, pool exhausted %llu\n\n",
                    c.book_messages ? 100.0 * static_cast<double>(c.overflow_hits) /
                                          static_cast<double>(c.book_messages)
                                    : 0.0,
                    (unsigned long long)c.recenters, (unsigned long long)c.orphans(),
                    (unsigned long long)c.index_failures, (unsigned long long)c.pool_exhausted);
        if (!opt.region_map.empty() && opt.mode == "structures") return 0;
    }

    if (opt.mode == "attribute") {
        PmcSet pmc;
        std::string why;
        if (!pmc.open(kFillEvents, kFillEventCount, why)) {
            std::printf("attribution unavailable: %s\n", why.c_str());
            return 1;
        }
        // Ages first, in a pass that is not counted, so the table that
        // computes them is not competing with the book for the cache.
        std::vector<std::uint8_t> ages;
        if (!opt.no_age) {
            auto lab = std::make_unique<AgeLabeller>(cfg.max_orders);
            lab->out.reserve(mf.size() / 20);
            Parser<AgeLabeller> parser(*lab);
            parser.run(mf.bytes());
            ages = std::move(lab->out);
        }
        // With the age stream, then without it: the difference between the
        // two is what reading the stream costs the book, measured.
        for (const bool with_age : {!opt.no_age, false}) {
            auto h = std::make_unique<AttributedHandler<FastBook<Policy>>>(cfg, opt.warmup);
            h->pmc = &pmc;
            h->ages = with_age ? ages.data() : nullptr;
            Parser<AttributedHandler<FastBook<Policy>>> parser(*h);
            parser.run(mf.bytes());
            std::printf("attribution, %s (demand fills into L1D, user mode, per message)\n",
                        with_age ? "with the order-age stream" : "without the age stream");
            print_attribution(*h, with_age);
            if (!with_age) break;
        }
        return 0;
    }

    const bool spsc = opt.mode == "spsc";
    const bool want_batch = (opt.mode == "batch" || opt.mode == "both" || spsc);
    const bool want_per = (opt.mode == "per-message" || opt.mode == "both" || spsc);

    // --- batch mode --------------------------------------------------------
    std::vector<double> batch_ns_per_msg;
    std::uint64_t batch_messages = 0;
    Faults timed_faults;
    if (want_batch) {
        for (int r = 0; r < opt.runs; ++r) {
            auto h = std::make_unique<PlainHandler<FastBook<Policy>>>(cfg);
            std::uint64_t ticks = 0;
            const Faults f0 = faults_now();
            if (spsc) {
                ticks = run_spsc(*h, mf, opt.core, opt.producer_core);
            } else {
                Parser<PlainHandler<FastBook<Policy>>> parser(*h);
                const std::uint64_t t0 = tick_begin();
                parser.run(mf.bytes());
                const std::uint64_t t1 = tick_end();
                ticks = t1 - t0;
            }
            const Faults f1 = faults_now();
            timed_faults.major += f1.major - f0.major;
            timed_faults.minor += f1.minor - f0.minor;
            batch_messages = h->book.counters().book_messages;
            const double per = batch_messages ? static_cast<double>(ticks) /
                                                    static_cast<double>(batch_messages)
                                              : 0.0;
            batch_ns_per_msg.push_back(per * ci.ns_per_tick);
        }
        print_batch(spsc ? "batch-timed, SPSC  (parse on the producer core, book on this one)"
                         : "batch-timed  (one region around the whole session)",
                    batch_ns_per_msg, batch_messages);
    }

    // --- per-message mode --------------------------------------------------
    double per_median_ns = 0;
    if (want_per) {
        // One set of histograms across all runs.
        TypeHistograms merged;
        std::uint64_t recorded = 0;
        // Each run's all-types distribution as well, so the spread across runs
        // is reported beside the pooled one: the method asks for the median of
        // at least five runs with the minimum and maximum, and a pooled
        // histogram alone would hide one bad run inside four good ones.
        constexpr double kPcts[] = {50.0, 90.0, 99.0, 99.9, 99.99};
        std::vector<std::array<double, 6>> per_run;
        for (int r = 0; r < opt.runs; ++r) {
            auto h = std::make_unique<TimedHandler<FastBook<Policy>>>(cfg, opt.warmup);
            TypeHistograms mine;
            h->overhead = ci.overhead_ticks;
            h->hist = &mine;
            const Faults f0 = faults_now();
            if (spsc) {
                run_spsc(*h, mf, opt.core, opt.producer_core);
            } else {
                Parser<TimedHandler<FastBook<Policy>>> parser(*h);
                parser.run(mf.bytes());
            }
            const Faults f1 = faults_now();
            timed_faults.major += f1.major - f0.major;
            timed_faults.minor += f1.minor - f0.minor;
            recorded += h->recorded;
            const hdr_histogram* run_all = mine.h[kSlotOrdinary];
            std::array<double, 6> row{};
            hdr_histogram* both = nullptr;
            hdr_init(1, 10000000, 3, &both);
            if (both) {
                hdr_add(both, run_all);
                hdr_add(both, mine.h[kSlotRecenter]);
                for (std::size_t k = 0; k < 5; ++k)
                    row[k] = static_cast<double>(hdr_value_at_percentile(both, kPcts[k])) *
                             ci.ns_per_tick;
                row[5] = static_cast<double>(hdr_max(both)) * ci.ns_per_tick;
                hdr_close(both);
            }
            per_run.push_back(row);
            for (std::size_t t = 0; t < 256; ++t) {
                if (mine.h[t] && merged.h[t]) hdr_add(merged.h[t], mine.h[t]);
            }
        }
        per_median_ns = print_per_message(
            spsc ? "per-message-timed, SPSC  (the book call on the consumer core, fenced)"
                 : "per-message-timed  (fenced region per message, instrument cost subtracted)",
            merged, ci, opt.runs, recorded);
        if (ci.publishable() && per_run.size() > 1) {
            std::printf("  all types, run by run: median [min, max] across %zu runs (ns)\n",
                        per_run.size());
            const char* names[] = {"p50", "p90", "p99", "p99.9", "p99.99", "max"};
            for (std::size_t k = 0; k < 6; ++k) {
                std::vector<double> col;
                for (const auto& row : per_run) col.push_back(row[k]);
                std::sort(col.begin(), col.end());
                std::printf("    %-7s %9.1f  [%9.1f, %9.1f]\n", names[k], col[col.size() / 2],
                            col.front(), col.back());
            }
            std::printf("\n");
        }
    }
    if (want_batch || want_per) {
        std::printf("page faults inside timed regions, all runs: major %ld, minor %ld\n\n",
                    timed_faults.major, timed_faults.minor);
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
    return 0; // a smoke run is not a failure
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
        else if (std::strcmp(argv[i], "--core") == 0)
            opt.core = std::atoi(next("--core"));
        else if (std::strcmp(argv[i], "--producer-core") == 0)
            opt.producer_core = std::atoi(next("--producer-core"));
        else if (std::strcmp(argv[i], "--region-map") == 0)
            opt.region_map = next("--region-map");
        else if (std::strcmp(argv[i], "--verify") == 0)
            opt.verify = true;
        else if (std::strcmp(argv[i], "--no-age") == 0)
            opt.no_age = true;
        else
            opt.path = argv[i];
    }
    const bool known_mode = opt.mode == "batch" || opt.mode == "per-message" ||
                            opt.mode == "both" || opt.mode == "attribute" ||
                            opt.mode == "spsc" || opt.mode == "structures";
    if (opt.path.empty() || opt.runs < 1 || !known_mode) {
        std::fprintf(stderr,
                     "usage: %s [--mode batch|per-message|both|attribute|spsc|structures]\n"
                     "          [--runs N] [--warmup N] [--verify] [--no-age]\n"
                     "          [--hash identity|multiply-shift|std|chained]\n"
                     "          [--core N] [--producer-core N] [--region-map FILE] "
                     "<session-file>\n",
                     argv[0]);
        return 2;
    }
    if (opt.mode == "spsc" && (opt.core < 0 || opt.producer_core < 0)) {
        std::fprintf(stderr, "--mode spsc needs --core and --producer-core\n");
        return 2;
    }
    if (opt.hash == "identity") return run<IdentityHash>(opt);
    if (opt.hash == "multiply-shift") return run<MultiplyShiftHash>(opt);
    if (opt.hash == "std") return run<StdHash>(opt);
    if (opt.hash == "chained") return run<ChainedMultiplyShift>(opt);
    std::fprintf(stderr, "unknown hash policy: %s\n", opt.hash.c_str());
    return 2;
}
