// test_differential -- the fast book against the reference book on synthetic flow.
//
// Correctness layer 4, run in CI on generated flow. The full-session runs
// against real data are the real gate; this catches regressions without a
// multi-gigabyte download.
//
// The harness is also tested against itself: a deliberately corrupted fast
// book must be reported as a divergence. A differential harness that never
// fails is worth nothing, and its silence is indistinguishable from agreement.

#include "carteret/differential.hpp"
#include "carteret/parser.hpp"

#include "itch_builder.hpp"

#include <cstdio>
#include <random>
#include <vector>

using namespace carteret;
using carteret::test::Bytes;

namespace {

int failures = 0;

#define CHECK(cond)                                                                            \
    do {                                                                                       \
        if (!(cond)) {                                                                         \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);               \
            ++failures;                                                                        \
        }                                                                                      \
    } while (0)

// Generates flow that reaches the structures a simple script would not: prices
// outside the flat window, sub-cent prices, replaces at the same price,
// executions to exactly zero, and orphaned modifies.
struct FlowGenerator {
    std::vector<unsigned char> buf;
    std::mt19937_64 rng;
    std::uint64_t ts = 34200ULL * 1000000000ULL;
    std::uint64_t next_ref = 1;
    std::vector<std::uint64_t> live;
    std::uint16_t symbols;

    explicit FlowGenerator(std::uint64_t seed, std::uint16_t n_symbols = 4)
        : rng(seed), symbols(n_symbols) {}

    std::uint64_t next_ts() { return ts += 1 + rng() % 1000; }

    // Prices straddle the flat window deliberately: the window is centred on
    // the first price a symbol sees, so a spread this wide forces overflow.
    Price random_price() {
        const std::uint32_t roll = static_cast<std::uint32_t>(rng() % 100);
        if (roll < 70) return static_cast<Price>(1500000 + (rng() % 200) * 100);  // inside
        if (roll < 90) return static_cast<Price>(1400000 + (rng() % 4000) * 100); // wide
        if (roll < 95) return static_cast<Price>(10000 + (rng() % 100) * 100);    // far away
        return static_cast<Price>(1500000 + (rng() % 200) * 100 + 50);            // half cent
    }

    void add(std::uint16_t locate, Ref ref, unsigned char side, Price price, Shares shares) {
        Bytes m = carteret::test::header('A', locate, next_ts());
        m.u64(ref);
        m.u8(side);
        m.u32(shares);
        m.alpha("SYM", 8);
        m.u32(price);
        carteret::test::frame(buf, m);
    }

    void add_mpid(std::uint16_t locate, Ref ref, unsigned char side, Price price,
                  Shares shares) {
        Bytes m = carteret::test::header('F', locate, next_ts());
        m.u64(ref);
        m.u8(side);
        m.u32(shares);
        m.alpha("SYM", 8);
        m.u32(price);
        m.alpha("MPID", 4);
        carteret::test::frame(buf, m);
    }

    void simple(char type, std::uint16_t locate, Ref ref, std::uint32_t qty) {
        Bytes m = carteret::test::header(type, locate, next_ts());
        m.u64(ref);
        if (type == 'E') {
            m.u32(qty);
            m.u64(1);
        } else if (type == 'X') {
            m.u32(qty);
        }
        carteret::test::frame(buf, m);
    }

    void exec_price(std::uint16_t locate, Ref ref, std::uint32_t qty, Price price) {
        Bytes m = carteret::test::header('C', locate, next_ts());
        m.u64(ref);
        m.u32(qty);
        m.u64(2);
        m.u8((rng() % 2) ? 'Y' : 'N');
        m.u32(price);
        carteret::test::frame(buf, m);
    }

    void replace(std::uint16_t locate, Ref old_ref, Ref new_ref, Price price, Shares shares) {
        Bytes m = carteret::test::header('U', locate, next_ts());
        m.u64(old_ref);
        m.u64(new_ref);
        m.u32(shares);
        m.u32(price);
        carteret::test::frame(buf, m);
    }

    void generate(int n) {
        for (int i = 0; i < n; ++i) {
            const auto locate = static_cast<std::uint16_t>(1 + rng() % symbols);
            const std::uint32_t roll = static_cast<std::uint32_t>(rng() % 100);

            if (roll < 40 || live.empty()) {
                const Ref ref = next_ref++;
                const unsigned char side = (rng() % 2) ? kBuy : kSell;
                const auto shares = static_cast<Shares>(100 * (1 + rng() % 10));
                if (rng() % 5 == 0)
                    add_mpid(locate, ref, side, random_price(), shares);
                else
                    add(locate, ref, side, random_price(), shares);
                live.push_back(ref);
                continue;
            }

            const std::size_t k = static_cast<std::size_t>(rng()) % live.size();
            const Ref ref = live[k];

            if (roll < 55) {
                simple('E', locate, ref, 100);
            } else if (roll < 62) {
                // Execute enough to reach zero, so removal-without-delete is
                // reached rather than only partial deductions.
                simple('E', locate, ref, 100000);
                live[k] = live.back();
                live.pop_back();
            } else if (roll < 70) {
                exec_price(locate, ref, 100, random_price());
            } else if (roll < 78) {
                simple('X', locate, ref, 100);
            } else if (roll < 88) {
                simple('D', locate, ref, 0);
                live[k] = live.back();
                live.pop_back();
            } else if (roll < 96) {
                const Ref nref = next_ref++;
                // Half the replaces keep the price, which is the case that
                // must still lose queue priority.
                const Price px = (rng() % 2) ? random_price() : 1500000;
                replace(locate, ref, nref, px, static_cast<Shares>(100 * (1 + rng() % 5)));
                live[k] = nref;
            } else {
                // Orphans: a reference that was never added.
                simple('D', locate, 900000000 + rng() % 1000, 0);
            }
        }
        carteret::test::frame_end(buf);
    }
};

template<class Policy>
void run_policy(const char* label, std::uint64_t seed, int n) {
    FlowGenerator g(seed);
    g.generate(n);

    FastBookConfig cfg;
    cfg.max_orders = 1u << 16;
    cfg.max_symbols = 64;
    cfg.index_hint = 1u << 16;

    Differential<Policy> d(cfg, 4096);
    Parser<Differential<Policy>> parser(d);
    const ParseStats st = parser.run({g.buf.data(), g.buf.size()});

    CHECK(st.clean());
    if (d.divergence().found) d.report(stderr);
    CHECK(!d.divergence().found);
    CHECK(d.compare_everything());
    CHECK(d.reference().check_invariants() == 0);
    CHECK(d.fast().check_invariants() == 0);
    CHECK(d.fast().counters().pool_exhausted == 0);
    CHECK(d.fast().counters().index_failures == 0);

    // The generated flow must actually reach the structures under test, or a
    // clean run means only that nothing interesting happened.
    CHECK(d.fast().counters().overflow_hits > 0);
    CHECK(d.fast().counters().sub_cent_prices > 0);
    CHECK(d.fast().counters().removed_at_zero > 0);
    CHECK(d.reference().counters().orphans() > 0);
    CHECK(d.stats().messages > static_cast<std::uint64_t>(n) / 2);

    std::printf("  %-16s %llu messages, %llu level and %llu BBO comparisons, "
                "%llu overflow hits\n",
                label, (unsigned long long)d.stats().messages,
                (unsigned long long)d.stats().level_comparisons,
                (unsigned long long)d.stats().bbo_comparisons,
                (unsigned long long)d.fast().counters().overflow_hits);
}

// A harness that cannot fail proves nothing. This drives both books with
// different flow and requires the comparison to report it.
void test_harness_detects_a_planted_difference() {
    FlowGenerator a(7);
    a.add(1, 1, kBuy, 1500000, 100);
    a.add(1, 2, kBuy, 1500000, 200);
    carteret::test::frame_end(a.buf);

    FastBookConfig cfg;
    cfg.max_orders = 1024;
    cfg.max_symbols = 16;
    cfg.index_hint = 1024;

    // Feed the reference book one extra order that the fast book never sees,
    // by driving them separately rather than through the harness's own apply.
    ReferenceBook ref;
    FastBook<MultiplyShiftHash> fast(cfg);
    {
        Parser<ReferenceBook> p(ref);
        p.run({a.buf.data(), a.buf.size()});
    }
    {
        FlowGenerator b(7);
        b.add(1, 1, kBuy, 1500000, 100); // second order omitted
        carteret::test::frame_end(b.buf);
        Parser<FastBook<MultiplyShiftHash>> p(fast);
        p.run({b.buf.data(), b.buf.size()});
    }

    const LevelSnapshot r = ref.level(1, kBuy, 1500000);
    const FastLevelSnapshot f = fast.level(1, kBuy, 1500000);
    CHECK(r.present && f.present);
    CHECK(r.shares != f.shares); // the comparison the harness performs
    CHECK(r.fifo != f.fifo);     // and the queue-order comparison
}

// A symbol whose FIRST order carries a sub-cent price leaves the flat window
// unallocated, because the window origin is taken from the first whole-cent
// price. The order still rests, in the overflow map, and still sets the
// inside. Found by the differential harness on 20190130.BX_ITCH_50 at message
// 8753: the fast book reported no bid for a symbol the reference book had one
// for. The synthetic flow reached sub-cent prices but never as a symbol's
// first order, which is why this case needs naming rather than generating.
void test_first_order_at_a_sub_cent_price() {
    FlowGenerator g(13);
    g.add(1, 1, kBuy, 1500050, 100);  // half a cent: no window can index it
    g.add(1, 2, kSell, 1500150, 200); // likewise on the other side
    carteret::test::frame_end(g.buf);

    FastBookConfig cfg;
    cfg.max_orders = 1024;
    cfg.max_symbols = 16;
    cfg.index_hint = 1024;

    Differential<> d(cfg, 0);
    Parser<Differential<>> parser(d);
    parser.run({g.buf.data(), g.buf.size()});

    if (d.divergence().found) d.report(stderr);
    CHECK(!d.divergence().found);
    CHECK(d.compare_everything());
    CHECK(d.fast().has_bid(1));
    CHECK(d.fast().best_bid(1) == 1500050);
    CHECK(d.fast().has_ask(1));
    CHECK(d.fast().best_ask(1) == 1500150);
    CHECK(d.fast().counters().sub_cent_prices == 2);
    CHECK(d.fast().check_invariants() == 0);

    // And the window still works once a whole-cent price arrives afterwards.
    FlowGenerator g2(17);
    g2.add(1, 1, kBuy, 1500050, 100);
    g2.add(1, 2, kBuy, 1510000, 300);
    carteret::test::frame_end(g2.buf);
    Differential<> d2(cfg, 0);
    Parser<Differential<>> p2(d2);
    p2.run({g2.buf.data(), g2.buf.size()});
    if (d2.divergence().found) d2.report(stderr);
    CHECK(!d2.divergence().found);
    CHECK(d2.compare_everything());
    CHECK(d2.fast().best_bid(1) == 1510000);
}

// A price that trends far beyond one window width, which forces the window to
// move repeatedly. Recentering rebuilds a side's level array and bitmap and
// moves levels between the flat array and the overflow map; nothing else in
// the book changes, so a defect here shows up as a level in the wrong place
// or an occupancy bit that disagrees with its level.
//
// The fixed-origin design this replaced never moved, so no amount of trending
// flow could have caught a bug in it -- and it put a third of all orders on a
// real session into the overflow map. See docs/design.md record 033.
void test_trending_price_forces_recentering() {
    FlowGenerator g(23);
    Ref ref = 1;
    // Walk the inside up by 4,000 ticks, about sixteen window widths at the
    // default width, leaving resting depth behind at every level.
    for (int step = 0; step < 4000; ++step) {
        const Price bid = static_cast<Price>(1000000 + step * 100);
        const Price ask = bid + 100;
        g.add(1, ref++, kBuy, bid, 100);
        g.add(1, ref++, kSell, ask, 100);
        if (step % 3 == 0 && ref > 4) {
            // Remove some depth left behind, so levels empty as well as fill.
            g.simple('D', 1, ref - 4, 0);
        }
    }
    // And back down again, so the window moves in both directions.
    for (int step = 4000; step-- > 0;) {
        const Price bid = static_cast<Price>(1000000 + step * 100);
        g.add(1, ref++, kBuy, bid, 100);
    }
    carteret::test::frame_end(g.buf);

    FastBookConfig cfg;
    cfg.max_orders = 1u << 16;
    cfg.max_symbols = 64;
    cfg.index_hint = 1u << 16;

    Differential<> d(cfg, 1024);
    Parser<Differential<>> parser(d);
    const ParseStats st = parser.run({g.buf.data(), g.buf.size()});

    CHECK(st.clean());
    if (d.divergence().found) d.report(stderr);
    CHECK(!d.divergence().found);
    CHECK(d.compare_everything());
    CHECK(d.reference().check_invariants() == 0);
    CHECK(d.fast().check_invariants() == 0);
    // The point of the test: the window really did move, many times.
    CHECK(d.fast().counters().recenters > 20);
    CHECK(d.fast().counters().levels_moved > 0);

    std::printf("  %-16s %llu recenters, %llu levels moved, %llu overflow hits\n",
                "recentering", (unsigned long long)d.fast().counters().recenters,
                (unsigned long long)d.fast().counters().levels_moved,
                (unsigned long long)d.fast().counters().overflow_hits);
}

// The same flow through the harness must be reported as clean, which is the
// other half of the check above.
void test_harness_reports_agreement() {
    FlowGenerator g(11);
    g.add(1, 1, kBuy, 1500000, 100);
    g.add(1, 2, kBuy, 1500000, 200);
    g.simple('E', 1, 1, 100);
    carteret::test::frame_end(g.buf);

    FastBookConfig cfg;
    cfg.max_orders = 1024;
    cfg.max_symbols = 16;
    cfg.index_hint = 1024;

    Differential<> d(cfg, 0);
    Parser<Differential<>> parser(d);
    parser.run({g.buf.data(), g.buf.size()});
    CHECK(!d.divergence().found);
    CHECK(d.compare_everything());
    CHECK(d.stats().level_comparisons > 0);
}

} // namespace

int main() {
    test_harness_detects_a_planted_difference();
    test_harness_reports_agreement();
    test_first_order_at_a_sub_cent_price();
    test_trending_price_forces_recentering();

    run_policy<IdentityHash>(IdentityHash::name, 1, 40000);
    run_policy<MultiplyShiftHash>(MultiplyShiftHash::name, 2, 40000);
    run_policy<StdHash>(StdHash::name, 3, 40000);

    if (failures == 0) {
        std::printf("all differential tests passed\n");
    } else {
        std::printf("%d failure(s)\n", failures);
    }
    return failures ? 1 : 0;
}
