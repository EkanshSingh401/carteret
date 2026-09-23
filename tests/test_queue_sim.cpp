// test_queue_sim -- one test per fill rule, from hand-written event scripts.
//
// The fill rules are docs/design.md record 020, and the pre-registration cites
// that record by number. A change to them after the registration commit has to
// be reported in the study writeup, so they need tests that fail loudly rather
// than a simulator that quietly does something reasonable.
//
// Each test drives a scripted session through the simulator with placement
// forced to a known instant and a known price, so the queue position at entry
// is exact and the expected outcome can be written down in advance.

#include "carteret/parser.hpp"
#include "carteret/queue_sim.hpp"

#include "itch_builder.hpp"

#include <cstdio>
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

constexpr std::uint16_t kLoc = 1;
constexpr std::uint64_t kT0 = 34200ULL * 1000000000ULL; // 09:30:00
constexpr Price kBid = 1500000;                         // $150.00
constexpr Price kAsk = 1500100;                         // $150.01

// Builds a session. Placement is made deterministic by setting the mean
// interarrival to a value that puts exactly one placement at a known time.
struct Script {
    std::vector<unsigned char> buf;
    std::uint64_t ts = kT0;

    std::uint64_t at(std::uint64_t offset_ns) { return kT0 + offset_ns; }

    void add(Ref ref, unsigned char side, Price price, Shares shares, std::uint64_t t) {
        Bytes m = carteret::test::header('A', kLoc, t);
        m.u64(ref);
        m.u8(side);
        m.u32(shares);
        m.alpha("TEST", 8);
        m.u32(price);
        carteret::test::frame(buf, m);
    }
    void execute(Ref ref, Shares qty, std::uint64_t t) {
        Bytes m = carteret::test::header('E', kLoc, t);
        m.u64(ref);
        m.u32(qty);
        m.u64(1);
        carteret::test::frame(buf, m);
    }
    void cancel(Ref ref, Shares qty, std::uint64_t t) {
        Bytes m = carteret::test::header('X', kLoc, t);
        m.u64(ref);
        m.u32(qty);
        carteret::test::frame(buf, m);
    }
    void del(Ref ref, std::uint64_t t) {
        Bytes m = carteret::test::header('D', kLoc, t);
        m.u64(ref);
        carteret::test::frame(buf, m);
    }
    void trade(Price price, Shares shares, std::uint64_t t) {
        Bytes m = carteret::test::header('P', kLoc, t);
        m.u64(0);
        m.u8('B');
        m.u32(shares);
        m.alpha("TEST", 8);
        m.u32(price);
        m.u64(2);
        carteret::test::frame(buf, m);
    }

    QueueSimulator run(QueueSimConfig cfg) {
        carteret::test::frame_end(buf);
        QueueSimulator sim(cfg);
        Parser<QueueSimulator> parser(sim);
        parser.run({buf.data(), buf.size()});
        sim.finish(ts + 1);
        return sim;
    }
};

// One placement, at a known instant, on a book that is already two-sided.
QueueSimConfig one_placement(std::uint64_t placement_offset_ns, bool rule4 = false) {
    QueueSimConfig cfg;
    cfg.start_ns = kT0 + placement_offset_ns;
    cfg.end_ns = cfg.start_ns + 1; // the window admits exactly one placement
    cfg.mean_interarrival_ns = 1'000'000'000ULL;
    cfg.max_life_ns = 300'000'000'000ULL; // long enough not to interfere
    cfg.rule4_non_displayed_fills = rule4;
    cfg.order_size = 100;
    // Every script below is written for a synthetic BID. Fixing the side here
    // keeps the tests independent of how many values the sampler draws.
    cfg.force_side = kBuy;
    return cfg;
}

bool filled(const QueueSimulator& s, QueueModel m) {
    return s.results()[static_cast<std::size_t>(m)].filled > 0;
}
std::uint64_t reason_count(const QueueSimulator& s, QueueModel m, FillReason r) {
    return s.results()[static_cast<std::size_t>(m)].reasons[static_cast<std::size_t>(r)];
}

// Seeds a two-sided book, then emits a clock message at the placement instant.
//
// A placement fires on the first message at or after its scheduled time,
// because that is the first moment the book state is defined. Without a
// message at that instant the order would be placed on whatever message comes
// next -- which in these scripts is the order that is supposed to arrive
// BEHIND it, and would instead be counted ahead.
//
// The clock is a non-cross Trade at a price far from both sides: it has no
// book effect (record 011), and it cannot trigger rule 4 because that rule
// only applies at the synthetic order's own price.
void clock(Script& s, std::uint64_t t) {
    s.trade(kBid - 50000, 1, t);
}

void seed(Script& s, Shares ahead_shares) {
    s.add(1, kBuy, kBid, ahead_shares, s.at(0));
    s.add(2, kSell, kAsk, 500, s.at(1));
    clock(s, s.at(500));
}

// --- rule 1: execution of a real order ahead deducts, and does not fill ----

void test_rule1_execution_ahead_deducts_without_filling() {
    Script s;
    seed(s, 300);
    // Placement lands here, joining behind order 1 (300 shares ahead).
    s.add(3, kBuy, kBid, 100, s.at(1000)); // another order, behind the synthetic one
    s.execute(1, 100, s.at(2000));         // partial execution of the order ahead

    QueueSimulator sim = s.run(one_placement(500));
    CHECK(sim.placements() == 1);
    // 200 shares still ahead: no model may fill.
    for (std::size_t m = 0; m < kModelCount; ++m) {
        CHECK(!filled(sim, static_cast<QueueModel>(m)));
    }
}

// --- rule 2: execution of a real order behind fills --------------------------

void test_rule2_execution_behind_fills_exact_immediately() {
    Script s;
    seed(s, 300);
    s.add(3, kBuy, kBid, 100, s.at(1000)); // joins BEHIND the synthetic order
    // Order 3 executes while 300 shares are still ahead. Under exact
    // market-by-order that is impossible unless the aggressor consumed
    // everything in front, which includes the synthetic order: it fills.
    s.execute(3, 100, s.at(2000));

    QueueSimulator sim = s.run(one_placement(500));
    CHECK(sim.placements() == 1);
    CHECK(filled(sim, QueueModel::Exact));
    CHECK(reason_count(sim, QueueModel::Exact, FillReason::ExecutionBehind) == 1);
    // The approximations cannot see that the executed order was behind. They
    // still have 300 shares ahead, so they deduct and do not fill. This is the
    // bias the study measures, in its simplest form.
    CHECK(!filled(sim, QueueModel::Conservative));
    CHECK(!filled(sim, QueueModel::Optimistic));
    CHECK(!filled(sim, QueueModel::Proportional));
}

void test_rule2_execution_at_price_once_ahead_is_zero_fills() {
    Script s;
    seed(s, 200);
    s.add(3, kBuy, kBid, 100, s.at(1000));
    s.execute(1, 200, s.at(2000)); // clears everything ahead, does not fill
    s.execute(3, 100, s.at(3000)); // next execution at the price fills

    QueueSimulator sim = s.run(one_placement(500));
    for (std::size_t m = 0; m < kModelCount; ++m) {
        const QueueModel qm = static_cast<QueueModel>(m);
        CHECK(filled(sim, qm));
        CHECK(reason_count(sim, qm, FillReason::ExecutionBehind) == 1);
    }
}

// Clearing the queue exactly is not a fill: the aggressor's size was exactly
// the shares ahead, so it stopped at the synthetic order rather than taking it.
void test_clearing_the_queue_exactly_does_not_fill() {
    Script s;
    seed(s, 200);
    s.execute(1, 200, s.at(2000));

    QueueSimulator sim = s.run(one_placement(500));
    for (std::size_t m = 0; m < kModelCount; ++m) {
        CHECK(!filled(sim, static_cast<QueueModel>(m)));
    }
}

// --- rule 3: a trade-through fills ------------------------------------------

void test_rule3_trade_through_fills_every_model() {
    Script s;
    seed(s, 5000); // a deep queue, so no model is close to the front
    s.add(3, kBuy, kBid - 100, 100, s.at(1000)); // a bid one tick BELOW
    s.execute(3, 100, s.at(2000)); // executing it means the aggressor walked through

    QueueSimulator sim = s.run(one_placement(500));
    CHECK(sim.placements() == 1);
    for (std::size_t m = 0; m < kModelCount; ++m) {
        const QueueModel qm = static_cast<QueueModel>(m);
        CHECK(filled(sim, qm));
        CHECK(reason_count(sim, qm, FillReason::TradeThrough) == 1);
    }
}

// An execution on the OTHER side, however aggressive, is not a trade-through
// for this order: it is someone else's queue.
void test_execution_on_the_other_side_does_not_fill() {
    Script s;
    seed(s, 300);
    s.execute(2, 500, s.at(2000)); // the resting ask executes
    QueueSimulator sim = s.run(one_placement(500));
    for (std::size_t m = 0; m < kModelCount; ++m) {
        CHECK(!filled(sim, static_cast<QueueModel>(m)));
    }
}

// --- rule 4: the non-displayed print, reported both ways --------------------

void test_rule4_off_by_default() {
    Script s;
    seed(s, 300);
    s.trade(kBid, 500, s.at(2000)); // a print at exactly the synthetic price

    QueueSimulator sim = s.run(one_placement(500, /*rule4=*/false));
    for (std::size_t m = 0; m < kModelCount; ++m) {
        CHECK(!filled(sim, static_cast<QueueModel>(m)));
    }
}

void test_rule4_on_fills_every_model() {
    Script s;
    seed(s, 300);
    s.trade(kBid, 500, s.at(2000));

    QueueSimulator sim = s.run(one_placement(500, /*rule4=*/true));
    for (std::size_t m = 0; m < kModelCount; ++m) {
        const QueueModel qm = static_cast<QueueModel>(m);
        CHECK(filled(sim, qm));
        CHECK(reason_count(sim, qm, FillReason::NonDisplayedPrint) == 1);
    }
}

// A print at a different price never fills, whether the rule is on or off.
void test_rule4_only_applies_at_the_order_price() {
    Script s;
    seed(s, 300);
    s.trade(kBid - 500, 500, s.at(2000));
    QueueSimulator sim = s.run(one_placement(500, /*rule4=*/true));
    for (std::size_t m = 0; m < kModelCount; ++m) {
        CHECK(!filled(sim, static_cast<QueueModel>(m)));
    }
}

// --- cancel attribution: the whole source of the bias -----------------------

// A cancel of an order AHEAD. Exact and optimistic move the full amount,
// proportional moves part of it, conservative does not move at all.
void test_cancel_ahead_splits_the_models() {
    Script s;
    s.add(1, kBuy, kBid, 400, s.at(0)); // ahead
    s.add(2, kSell, kAsk, 500, s.at(1));
    clock(s, s.at(500));                   // the synthetic order enters here, 400 ahead
    s.add(3, kBuy, kBid, 600, s.at(1000)); // behind
    s.cancel(1, 400, s.at(2000));          // the order ahead cancels entirely
    // Now exact has 0 ahead. An execution at the price fills exact; the others
    // still believe there is depth in front.
    s.execute(3, 100, s.at(3000));

    QueueSimulator sim = s.run(one_placement(500));
    CHECK(sim.placements() == 1);
    // Exact knows reference 1 was ahead, so its ahead-count is zero and the
    // execution of the order behind fills it.
    CHECK(filled(sim, QueueModel::Exact));
    // Optimistic assumes every cancel came from ahead, removes the whole 400,
    // and reaches the same place by a wrong argument.
    CHECK(filled(sim, QueueModel::Optimistic));
    // Conservative assumes the cancel came from behind, so it still believes
    // 400 shares are ahead and the 100-share execution only reduces that.
    CHECK(!filled(sim, QueueModel::Conservative));
    // Proportional sees a level of 1,000 (400 ahead, 600 behind) and a cancel
    // of 400, so it attributes 400 * 400/1000 = 160 to the front and still
    // believes 240 are ahead. It sits between the other two approximations,
    // which is the behaviour that makes it worth measuring separately.
    CHECK(!filled(sim, QueueModel::Proportional));
}

// A cancel of an order BEHIND. Exact does not move; optimistic wrongly moves
// the full amount; proportional moves a fraction.
void test_cancel_behind_splits_the_models() {
    Script s;
    s.add(1, kBuy, kBid, 400, s.at(0));
    s.add(2, kSell, kAsk, 500, s.at(1));
    clock(s, s.at(500));                   // the synthetic order enters here, 400 ahead
    s.add(3, kBuy, kBid, 600, s.at(1000)); // behind; level depth now 1000
    s.cancel(3, 600, s.at(2000));          // the order BEHIND cancels
    s.execute(1, 400, s.at(3000));         // clears the real queue ahead
    s.add(4, kBuy, kBid, 100, s.at(4000));
    s.execute(4, 100, s.at(5000)); // fills anything at the front

    QueueSimulator sim = s.run(one_placement(500));
    CHECK(sim.placements() == 1);
    // Every model ends up filled here; what differs is when, which the
    // aggregate time-to-fill captures. The point of this test is that the
    // exact model's ahead-count was untouched by a cancel behind it.
    for (std::size_t m = 0; m < kModelCount; ++m) {
        CHECK(filled(sim, static_cast<QueueModel>(m)));
    }
}

// A replace removes an order from the level. To a market-by-price observer it
// is indistinguishable from a cancel, which is exactly why the models differ
// on it; the exact model attributes it correctly.
void test_replace_is_treated_as_a_removal() {
    Script s;
    s.add(1, kBuy, kBid, 400, s.at(0));
    s.add(2, kSell, kAsk, 500, s.at(1));
    clock(s, s.at(500)); // the synthetic order enters here, 400 ahead
    Bytes m = carteret::test::header('U', kLoc, s.at(2000));
    m.u64(1);    // old reference: the order ahead
    m.u64(10);   // new reference
    m.u32(400);  // same size
    m.u32(kBid); // same price: it goes to the back of the queue
    carteret::test::frame(s.buf, m);
    s.add(3, kBuy, kBid, 100, s.at(3000));
    s.execute(3, 100, s.at(4000));

    QueueSimulator sim = s.run(one_placement(500));
    CHECK(sim.placements() == 1);
    // The replaced order left the queue ahead, so exact is at the front and
    // the execution fills it.
    CHECK(filled(sim, QueueModel::Exact));
    // Conservative treats the removal as behind and still sees 400 ahead.
    CHECK(!filled(sim, QueueModel::Conservative));
}

// --- the Bernoulli-proportional model ----------------------------------------

// The model's whole purpose is to have proportional's MEAN and exact's SHAPE
// (docs/design.md record 035a), so both halves are asserted here.
//
// The script: 400 shares ahead, 600 join behind, then the 600 behind cancels.
// A market-by-price observer sees a level of 1,000 lose 600, so the assumed
// ahead-share is 400/1000 = 0.4. The probe is a PARTIAL EXECUTION OF THE ORDER
// AHEAD -- 100 of its 400 shares. That probe is deliberate: an execution of an
// order behind would fill the exact model outright by rule 2 and settle
// nothing, whereas executing the order ahead leaves every model to answer from
// its own ahead-count, and only a model that believes nothing is ahead fills.
//
//   exact         the cancel was behind: 400 - 100 = 300 ahead, never fills.
//   conservative  assumes behind: 400 - 100 = 300 ahead, never fills.
//   optimistic    assumes ahead: 400 - 600 -> 0, so the execution fills it.
//   proportional  removes 600 * 0.4 = 240, then 100: 60 ahead, never fills.
//   bernoulli     removes 600 or nothing: 0 or 400 ahead, so it fills in
//                 exactly those runs where the coin came up ahead.
//
// SHAPE is therefore the fact that this model's outcome is ever either of the
// two extremes, which no fraction can produce. MEAN is the rate at which it
// takes the filling one, checked across 400 seeds.
void test_bernoulli_proportional_is_all_or_nothing_at_the_stated_rate() {
    const int kRuns = 400;
    int bern_fills = 0;
    for (int r = 0; r < kRuns; ++r) {
        Script s;
        s.add(1, kBuy, kBid, 400, s.at(0));
        s.add(2, kSell, kAsk, 500, s.at(1));
        clock(s, s.at(500));                   // synthetic order enters, 400 ahead
        s.add(3, kBuy, kBid, 600, s.at(1000)); // behind; level depth now 1,000
        s.cancel(3, 600, s.at(2000));          // the order behind cancels
        s.execute(1, 100, s.at(3000));         // partial execution of the order AHEAD

        QueueSimConfig cfg = one_placement(500);
        cfg.seed = 1000u + static_cast<std::uint64_t>(r);
        QueueSimulator sim = s.run(cfg);
        CHECK(sim.placements() == 1);

        // The other four are deterministic and must not vary with the seed.
        CHECK(!filled(sim, QueueModel::Exact));
        CHECK(!filled(sim, QueueModel::Conservative));
        CHECK(!filled(sim, QueueModel::Proportional));
        CHECK(filled(sim, QueueModel::Optimistic));

        if (filled(sim, QueueModel::BernoulliProportional)) ++bern_fills;
    }

    // 400 draws at p = 0.4: mean 160, standard deviation 9.8. The bounds are
    // five standard deviations either side, wide enough that a change to the
    // generator cannot fail this by chance and narrow enough to catch a wrong
    // numerator, a wrong denominator, or a draw that never happens.
    CHECK(bern_fills > 111);
    CHECK(bern_fills < 209);
    if (bern_fills <= 111 || bern_fills >= 209) {
        std::fprintf(stderr, "  bernoulli fills %d of %d, expected about 160\n", bern_fills,
                     kRuns);
    }
}

// A certainty must not consume a draw, and must not be taken as a coin: when
// the whole level is ahead, the assumed share is 1 and the model must remove
// the full quantity every time, for every seed.
void test_bernoulli_proportional_is_certain_when_the_level_is_all_ahead() {
    for (std::uint64_t seed :
         {std::uint64_t{1}, std::uint64_t{2}, std::uint64_t{99}, std::uint64_t{20190130}}) {
        Script s;
        s.add(1, kBuy, kBid, 400, s.at(0));
        s.add(2, kSell, kAsk, 500, s.at(1));
        clock(s, s.at(500));          // synthetic order enters, 400 ahead, nothing behind
        s.cancel(1, 400, s.at(1000)); // the entire queue ahead cancels
        s.add(3, kBuy, kBid, 100, s.at(2000));
        s.execute(3, 100, s.at(3000));

        QueueSimConfig cfg = one_placement(500);
        cfg.seed = seed;
        QueueSimulator sim = s.run(cfg);
        CHECK(sim.placements() == 1);
        CHECK(filled(sim, QueueModel::BernoulliProportional));
        // Conservative still believes 400 are ahead, which is what makes the
        // check above a statement about attribution rather than about the
        // execution.
        CHECK(!filled(sim, QueueModel::Conservative));
    }
}

// --- expiry and accounting ---------------------------------------------------

void test_unfilled_orders_expire_and_are_worth_nothing() {
    Script s;
    seed(s, 10000);
    // Nothing happens at the price for a long time.
    for (int i = 1; i <= 20; ++i)
        s.add(100 + static_cast<Ref>(i), kSell, kAsk + 100, 100,
              s.at(static_cast<std::uint64_t>(i) * 10'000'000'000ULL));

    QueueSimConfig cfg = one_placement(500);
    cfg.max_life_ns = 60'000'000'000ULL;
    QueueSimulator sim = s.run(cfg);
    CHECK(sim.placements() == 1);
    for (std::size_t m = 0; m < kModelCount; ++m) {
        const QueueModel qm = static_cast<QueueModel>(m);
        CHECK(!filled(sim, qm));
        CHECK(reason_count(sim, qm, FillReason::Expired) == 1);
    }
}

// Every model sees the same placements, so a difference in fill rate is a
// difference in the model and not in the sample.
void test_all_models_see_the_same_placements() {
    Script s;
    s.add(1, kBuy, kBid, 300, s.at(0));
    s.add(2, kSell, kAsk, 300, s.at(1));
    for (int i = 0; i < 200; ++i) {
        const auto t = s.at(static_cast<std::uint64_t>(i + 2) * 1'000'000ULL);
        s.add(100 + static_cast<Ref>(i), (i % 2) ? kBuy : kSell, (i % 2) ? kBid : kAsk, 100, t);
    }
    QueueSimConfig cfg;
    cfg.start_ns = kT0;
    cfg.end_ns = kT0 + 200'000'000ULL;
    cfg.mean_interarrival_ns = 10'000'000ULL;
    cfg.max_life_ns = 300'000'000'000ULL;
    cfg.force_side = kBuy;

    QueueSimulator sim = s.run(cfg);
    CHECK(sim.placements() > 5);
    const auto& r = sim.results();
    for (std::size_t m = 1; m < kModelCount; ++m) {
        CHECK(r[m].placed == r[0].placed);
        for (std::size_t d = 0; d < kDepthBucketEdges.size(); ++d) {
            CHECK(r[m].placed_by_depth[d] == r[0].placed_by_depth[d]);
        }
    }
    CHECK(r[0].placed == sim.placements());
}

} // namespace

int main() {
    test_rule1_execution_ahead_deducts_without_filling();
    test_rule2_execution_behind_fills_exact_immediately();
    test_rule2_execution_at_price_once_ahead_is_zero_fills();
    test_clearing_the_queue_exactly_does_not_fill();
    test_rule3_trade_through_fills_every_model();
    test_execution_on_the_other_side_does_not_fill();
    test_rule4_off_by_default();
    test_rule4_on_fills_every_model();
    test_rule4_only_applies_at_the_order_price();
    test_cancel_ahead_splits_the_models();
    test_cancel_behind_splits_the_models();
    test_replace_is_treated_as_a_removal();
    test_bernoulli_proportional_is_all_or_nothing_at_the_stated_rate();
    test_bernoulli_proportional_is_certain_when_the_level_is_all_ahead();
    test_unfilled_orders_expire_and_are_worth_nothing();
    test_all_models_see_the_same_placements();

    if (failures == 0) {
        std::printf("all queue simulator tests passed\n");
    } else {
        std::printf("%d failure(s)\n", failures);
    }
    return failures ? 1 : 0;
}
