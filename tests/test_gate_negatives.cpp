// test_gate_negatives -- one test per correctness gate, proving it can fail.
//
// Every gate in this project is a claim that something would have been caught.
// A gate that has only ever been seen to pass supports no such claim: it may
// be checking nothing. Each test below constructs the defect the gate exists
// to catch and asserts that it is caught, and where the distinction matters a
// matching positive control asserts that the clean case still passes.
//
// This is not hypothetical. The reopening-window cap added for record 036
// failed `20190130.BX_ITCH_50` on its first run -- a session with zero crossed
// and zero locked observations -- because it charged the cap against windows
// that were excusing nothing. A gate that fails on clean data is as broken as
// one that passes on dirty data, and only running it against both finds that
// out. The list of gates and their tests is in docs/correctness.md.

#include "carteret/determinism.hpp"
#include "carteret/differential.hpp"
#include "carteret/parser.hpp"
#include "carteret/reference_book.hpp"

#include "itch_builder.hpp"

#include <cstdio>
#include <string>
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

// --- stream construction -----------------------------------------------------

void add(std::vector<unsigned char>& buf, Ref ref, unsigned char side, Price price,
         Shares shares, std::uint64_t t) {
    Bytes m = carteret::test::header('A', kLoc, t);
    m.u64(ref);
    m.u8(side);
    m.u32(shares);
    m.alpha("TEST", 8);
    m.u32(price);
    carteret::test::frame(buf, m);
}

void del(std::vector<unsigned char>& buf, Ref ref, std::uint64_t t) {
    Bytes m = carteret::test::header('D', kLoc, t);
    m.u64(ref);
    carteret::test::frame(buf, m);
}

void trading_action(std::vector<unsigned char>& buf, unsigned char state, std::uint64_t t) {
    Bytes m = carteret::test::header('H', kLoc, t);
    m.alpha("TEST", 8);
    m.u8(state);
    m.u8(' ');
    m.alpha("", 4);
    carteret::test::frame(buf, m);
}

void operational_halt(std::vector<unsigned char>& buf, unsigned char market,
                      unsigned char action, std::uint64_t t) {
    Bytes m = carteret::test::header('h', kLoc, t);
    m.alpha("TEST", 8);
    m.u8(market);
    m.u8(action);
    carteret::test::frame(buf, m);
}

void cross_trade(std::vector<unsigned char>& buf, std::uint64_t t) {
    Bytes m = carteret::test::header('Q', kLoc, t);
    m.u64(100);
    m.alpha("TEST", 8);
    m.u32(kBid);
    m.u64(1);
    m.u8('H'); // halt cross
    carteret::test::frame(buf, m);
}

void system_event(std::vector<unsigned char>& buf, unsigned char code, std::uint64_t t) {
    Bytes m = carteret::test::header('S', 0, t);
    m.u8(code);
    carteret::test::frame(buf, m);
}

ReferenceBook replay_into_reference(const std::vector<unsigned char>& buf) {
    ReferenceBook b;
    b.set_venue_market_code('Q');
    Parser<ReferenceBook> p(b);
    p.run({buf.data(), buf.size()});
    return b;
}

// A book whose bid crosses its offer: a buy at $150.02 resting against a sell
// at $150.01. On a venue that is matching the symbol this cannot happen, and
// that is precisely what the gate asserts.
void build_crossing(std::vector<unsigned char>& buf, std::uint64_t t) {
    add(buf, 1, kSell, kAsk, 100, t);
    add(buf, 2, kBuy, kAsk + 100, 100, t + 1000);
}

// --- GATE: crossed or locked book while the venue is matching ---------------

void test_crossing_while_trading_is_caught() {
    std::vector<unsigned char> buf;
    trading_action(buf, 'T', kT0);
    build_crossing(buf, kT0 + 1'000'000);

    const ReferenceBook b = replay_into_reference(buf);
    const RefCounters& c = b.counters();
    CHECK(c.crossed_observations > 0);
    CHECK(c.crossed_unexplained > 0); // the gate fires
    CHECK(c.crossed_while_not_trading == 0);
    CHECK(c.crossed_before_first_action == 0);
}

// Positive control: the same crossing while the symbol is halted must NOT
// fail, or the gate would reject every halted book on the tape.
void test_crossing_while_halted_is_excused() {
    std::vector<unsigned char> buf;
    trading_action(buf, 'T', kT0);
    trading_action(buf, 'H', kT0 + 1000);
    build_crossing(buf, kT0 + 1'000'000);

    const ReferenceBook book = replay_into_reference(buf);
    const RefCounters& c = book.counters();
    CHECK(c.crossed_observations > 0);
    CHECK(c.crossed_while_not_trading == c.crossed_observations);
    CHECK(c.crossed_unexplained == 0);
}

// Specification 1.2.2: a security absent from the pre-opening Trading Action
// spin is treated as halted. A crossing before any 'H' is therefore excused,
// and counted separately so that reliance on the default stays visible.
void test_crossing_before_any_trading_action_is_excused_and_counted_apart() {
    std::vector<unsigned char> buf;
    build_crossing(buf, kT0);

    const ReferenceBook book = replay_into_reference(buf);
    const RefCounters& c = book.counters();
    CHECK(c.crossed_observations > 0);
    CHECK(c.crossed_before_first_action == c.crossed_observations);
    CHECK(c.crossed_while_not_trading == 0);
    CHECK(c.crossed_unexplained == 0);
}

// --- GATE: the reopening window, and its hard cap ---------------------------

// Inside the window, before the reopening cross: excused.
void test_crossing_inside_the_reopening_window_is_excused() {
    std::vector<unsigned char> buf;
    trading_action(buf, 'T', kT0);
    trading_action(buf, 'H', kT0 + 1000);
    trading_action(buf, 'T', kT0 + 2000); // resumption opens the window
    build_crossing(buf, kT0 + 3000);      // 1 us later, far inside the cap

    const ReferenceBook book = replay_into_reference(buf);
    const RefCounters& c = book.counters();
    CHECK(c.crossed_observations > 0);
    CHECK(c.crossed_awaiting_reopen == c.crossed_observations);
    CHECK(c.crossed_unexplained == 0);
    CHECK(c.reopen_windows_capped == 0);
}

// Past the cap with no reopening cross: the window closes by timing out, which
// fails. The excuse is only as good as the event that ends it, and here that
// event never arrived.
void test_reopening_window_past_the_cap_fails() {
    std::vector<unsigned char> buf;
    trading_action(buf, 'T', kT0);
    trading_action(buf, 'H', kT0 + 1000);
    trading_action(buf, 'T', kT0 + 2000);
    // 200 ms later, twice the 100 ms cap.
    build_crossing(buf, kT0 + 2000 + 200'000'000ULL);

    const ReferenceBook book = replay_into_reference(buf);
    const RefCounters& c = book.counters();
    CHECK(c.reopen_windows_capped > 0);
    CHECK(c.crossed_unexplained > 0);
    CHECK(c.crossed_awaiting_reopen == 0);
}

// The reopening cross closes the window, so a crossing AFTER it is no longer
// excused even though it is well inside the cap.
void test_crossing_after_the_reopening_cross_is_not_excused() {
    std::vector<unsigned char> buf;
    trading_action(buf, 'T', kT0);
    trading_action(buf, 'H', kT0 + 1000);
    trading_action(buf, 'T', kT0 + 2000);
    cross_trade(buf, kT0 + 3000); // window closes here
    build_crossing(buf, kT0 + 4000);

    const ReferenceBook book = replay_into_reference(buf);
    const RefCounters& c = book.counters();
    CHECK(c.crossed_observations > 0);
    CHECK(c.crossed_unexplained > 0);
    CHECK(c.crossed_awaiting_reopen == 0);
}

// Regression for the defect described at the top of this file: a window left
// open on a symbol whose book never crosses must not be charged against the
// cap, however long it stays open.
void test_an_open_window_on_a_clean_book_does_not_trip_the_cap() {
    std::vector<unsigned char> buf;
    trading_action(buf, 'T', kT0);
    trading_action(buf, 'H', kT0 + 1000);
    trading_action(buf, 'T', kT0 + 2000); // window opens and is never closed by a cross
    // A clean two-sided book, an hour later.
    add(buf, 1, kSell, kAsk, 100, kT0 + 3600'000'000'000ULL);
    add(buf, 2, kBuy, kBid, 100, kT0 + 3600'000'000'001ULL);

    const ReferenceBook book = replay_into_reference(buf);
    const RefCounters& c = book.counters();
    CHECK(c.crossed_observations == 0);
    CHECK(c.locked_observations == 0);
    CHECK(c.reopen_windows_capped == 0);
}

// --- GATE: operational halt ('h') -------------------------------------------

void test_operational_halt_for_this_market_excuses_a_crossing() {
    std::vector<unsigned char> buf;
    trading_action(buf, 'T', kT0);
    operational_halt(buf, 'Q', 'H', kT0 + 1000); // NASDAQ, halted
    build_crossing(buf, kT0 + 2000);

    const ReferenceBook book = replay_into_reference(buf);
    const RefCounters& c = book.counters();
    CHECK(c.operational_halts == 1);
    CHECK(c.crossed_observations > 0);
    CHECK(c.crossed_operational_halt == c.crossed_observations);
    CHECK(c.crossed_unexplained == 0);
}

// An operational halt on a DIFFERENT market says nothing about this one, so it
// must not excuse anything. Getting this wrong would silently disable the gate
// for any symbol halted anywhere.
void test_operational_halt_for_another_market_excuses_nothing() {
    std::vector<unsigned char> buf;
    trading_action(buf, 'T', kT0);
    operational_halt(buf, 'B', 'H', kT0 + 1000); // BX, while replaying NASDAQ
    build_crossing(buf, kT0 + 2000);

    const ReferenceBook book = replay_into_reference(buf);
    const RefCounters& c = book.counters();
    CHECK(c.operational_halts == 0);
    CHECK(c.crossed_unexplained > 0);
}

void test_operational_halt_release_restores_the_gate() {
    std::vector<unsigned char> buf;
    trading_action(buf, 'T', kT0);
    operational_halt(buf, 'Q', 'H', kT0 + 1000);
    operational_halt(buf, 'Q', 'T', kT0 + 2000); // released
    build_crossing(buf, kT0 + 3000);

    const ReferenceBook book = replay_into_reference(buf);
    const RefCounters& c = book.counters();
    CHECK(c.operational_halt_releases == 1);
    CHECK(c.crossed_unexplained > 0);
}

// --- GATE: trading-state tracking is actually reaching the book -------------

// The tracker is what every excuse above depends on. If 'H' never arrives --
// because a harness drops it, as Differential did before record 036 -- every
// observation is excused by the specification's halted default and the gate
// silently stops testing anything. Zero observed state changes on a stream
// that contains them is therefore an ERROR, not a quiet pass.
void test_stripping_trading_actions_is_an_error_not_a_quiet_pass() {
    std::vector<unsigned char> with_actions;
    trading_action(with_actions, 'T', kT0);
    build_crossing(with_actions, kT0 + 1'000'000);

    const ReferenceBook with_book = replay_into_reference(with_actions);
    const RefCounters& present = with_book.counters();
    CHECK(present.trading_state_changes == 1);
    CHECK(present.crossed_unexplained > 0); // the gate is live

    // The same stream with every 'H' removed.
    std::vector<unsigned char> stripped;
    build_crossing(stripped, kT0 + 1'000'000);

    const ReferenceBook without_book = replay_into_reference(stripped);
    const RefCounters& absent = without_book.counters();
    CHECK(absent.trading_state_changes == 0);
    CHECK(absent.crossed_observations == present.crossed_observations);
    // Every observation is now excused by the default, which is exactly the
    // silent failure. The count of state changes is what exposes it.
    CHECK(absent.crossed_unexplained == 0);
    CHECK(absent.crossed_before_first_action == absent.crossed_observations);
}

// --- GATE: the differential comparison itself -------------------------------

// A fast book that drops one removal. The comparison must notice; if it does
// not, every "RESULT: identical" in this project means nothing.
template<class HashPolicy>
struct BookThatDropsOneDelete {
    FastBook<HashPolicy> inner;
    int deletes_seen = 0;

    explicit BookThatDropsOneDelete(FastBookConfig cfg) : inner(cfg) {}

    void on(SystemEvent v) { inner.on(v); }
    void on(AddOrder v) { inner.on(v); }
    void on(AddOrderMpid v) { inner.on(v); }
    void on(OrderExecuted v) { inner.on(v); }
    void on(OrderExecutedPrice v) { inner.on(v); }
    void on(OrderCancel v) { inner.on(v); }
    void on(OrderDelete v) {
        if (++deletes_seen == 2) return; // the defect
        inner.on(v);
    }
    void on(OrderReplace v) { inner.on(v); }
    void on(Trade v) { inner.on(v); }
    void on(CrossTrade v) { inner.on(v); }
    void on(BrokenTrade v) { inner.on(v); }

    [[nodiscard]] auto level(std::uint16_t l, unsigned char s, Price p) const {
        return inner.level(l, s, p);
    }
    [[nodiscard]] bool has_bid(std::uint16_t l) const { return inner.has_bid(l); }
    [[nodiscard]] bool has_ask(std::uint16_t l) const { return inner.has_ask(l); }
    [[nodiscard]] Price best_bid(std::uint16_t l) const { return inner.best_bid(l); }
    [[nodiscard]] Price best_ask(std::uint16_t l) const { return inner.best_ask(l); }
    [[nodiscard]] std::uint64_t live_orders() const { return inner.live_orders(); }
    [[nodiscard]] std::size_t check_invariants() const { return inner.check_invariants(); }
    [[nodiscard]] const FastCounters& counters() const { return inner.counters(); }
};

void test_the_differential_detects_a_book_that_is_wrong() {
    std::vector<unsigned char> buf;
    for (Ref r = 1; r <= 6; ++r) {
        add(buf, r, (r % 2) ? kBuy : kSell, (r % 2) ? kBid : kAsk, 100, kT0 + r * 1'000'000ULL);
    }
    for (Ref r = 1; r <= 6; ++r) del(buf, r, kT0 + (10 + r) * 1'000'000ULL);

    // Control: the real book agrees.
    {
        Differential<MultiplyShiftHash> d({}, 1u << 22, 1u << 20);
        d.set_venue_market_code('Q');
        Parser<Differential<MultiplyShiftHash>> p(d);
        p.run({buf.data(), buf.size()});
        const bool full_ok = d.compare_everything();
        CHECK(!d.divergence().found);
        CHECK(full_ok);
    }
    // The same stream against a book that drops one delete.
    {
        using Broken =
            Differential<MultiplyShiftHash, BookThatDropsOneDelete<MultiplyShiftHash>>;
        Broken d({}, 1u << 22, 1u << 20);
        d.set_venue_market_code('Q');
        Parser<Broken> p(d);
        p.run({buf.data(), buf.size()});
        const bool full_ok = d.compare_everything();
        // Either the per-message comparison caught it or the end-of-session
        // full compare did. Both are the gate; neither may stay silent.
        CHECK(d.divergence().found || !full_ok);
    }
}

// --- GATE: census ------------------------------------------------------------

ParseStats parse_stats(const std::vector<unsigned char>& buf) {
    struct Null {
    } n;
    Parser<Null> p(n);
    return p.run({buf.data(), buf.size()});
}

void test_census_rejects_an_unknown_message_type() {
    std::vector<unsigned char> buf;
    add(buf, 1, kBuy, kBid, 100, kT0);
    Bytes m = carteret::test::header('~', kLoc, kT0 + 1000); // not an ITCH 5.0 type
    m.u64(0);
    carteret::test::frame(buf, m);

    const ParseStats st = parse_stats(buf);
    CHECK(st.unknown > 0);
    CHECK(!st.clean());
}

void test_census_rejects_a_length_mismatch() {
    std::vector<unsigned char> buf;
    add(buf, 1, kBuy, kBid, 100, kT0);
    // An 'A' body is 35 bytes; declare 34 and supply 34.
    Bytes m = carteret::test::header('A', kLoc, kT0 + 1000);
    m.u64(2);
    m.u8(kBuy);
    m.u32(100);
    m.alpha("TEST", 8);
    m.u16(0); // one short
    buf.push_back(static_cast<unsigned char>(m.size() >> 8));
    buf.push_back(static_cast<unsigned char>(m.size()));
    buf.insert(buf.end(), m.b.begin(), m.b.end());

    const ParseStats st = parse_stats(buf);
    CHECK(st.mismatch > 0);
    CHECK(!st.clean());
}

void test_census_rejects_a_truncated_body() {
    std::vector<unsigned char> buf;
    add(buf, 1, kBuy, kBid, 100, kT0);
    add(buf, 2, kBuy, kBid, 100, kT0 + 1000);
    buf.resize(buf.size() - 10); // cut the last message in half

    const ParseStats st = parse_stats(buf);
    CHECK(st.end == ParseEnd::Truncated);
    CHECK(!st.clean());
}

void test_census_rejects_trailing_bytes() {
    std::vector<unsigned char> buf;
    add(buf, 1, kBuy, kBid, 100, kT0);
    buf.push_back(0x00); // one byte too few for even a length prefix

    const ParseStats st = parse_stats(buf);
    CHECK(!st.clean());
}

// The only check that can tell a truncated download from a complete session,
// since without a zero-length terminator a truncation is a well-formed prefix
// of a valid file (record 032).
void test_a_session_without_the_final_c_is_a_well_formed_prefix() {
    std::vector<unsigned char> complete;
    system_event(complete, 'O', kT0);
    add(complete, 1, kBuy, kBid, 100, kT0 + 1000);
    system_event(complete, 'C', kT0 + 2000);

    std::vector<unsigned char> truncated;
    system_event(truncated, 'O', kT0);
    add(truncated, 1, kBuy, kBid, 100, kT0 + 1000);

    // Both parse cleanly. Framing alone cannot separate them, which is the
    // whole reason the census gates on content.
    CHECK(parse_stats(complete).clean());
    CHECK(parse_stats(truncated).clean());

    const auto last_type = [](const std::vector<unsigned char>& b) {
        struct Last {
            unsigned char type = 0;
            unsigned char code = 0;
            void on(SystemEvent v) {
                type = v.type();
                code = v.event_code();
            }
            void on(AddOrder v) { type = v.type(); }
        } l;
        Parser<Last> p(l);
        p.run({b.data(), b.size()});
        return std::pair<unsigned char, unsigned char>{l.type, l.code};
    };
    CHECK(last_type(complete) == (std::pair<unsigned char, unsigned char>{'S', 'C'}));
    CHECK(last_type(truncated).first == 'A');
}

// --- GATE: determinism -------------------------------------------------------

void test_determinism_hash_separates_two_different_sessions() {
    std::vector<unsigned char> a;
    add(a, 1, kBuy, kBid, 100, kT0);
    add(a, 2, kSell, kAsk, 100, kT0 + 1000);

    std::vector<unsigned char> b;
    add(b, 1, kBuy, kBid, 100, kT0);
    add(b, 2, kSell, kAsk, 101, kT0 + 1000); // one share different

    const auto digests = [](const std::vector<unsigned char>& buf) {
        DeterministicReplay r;
        Parser<DeterministicReplay> p(r);
        p.run({buf.data(), buf.size()});
        return std::pair<std::string, std::string>{r.event_hash(), r.book_hash()};
    };

    const auto [ea, ba] = digests(a);
    const auto [eb, bb] = digests(b);
    CHECK(ea != eb); // the event stream differs
    CHECK(ba != bb); // and so does the final book
    CHECK(ea.size() == 64);

    // And the same input twice gives the same digests, or the checks above
    // would be satisfied by a hash that is merely unstable.
    const auto [ea2, ba2] = digests(a);
    CHECK(ea2 == ea);
    CHECK(ba2 == ba);
}

} // namespace

int main() {
    test_crossing_while_trading_is_caught();
    test_crossing_while_halted_is_excused();
    test_crossing_before_any_trading_action_is_excused_and_counted_apart();
    test_crossing_inside_the_reopening_window_is_excused();
    test_reopening_window_past_the_cap_fails();
    test_crossing_after_the_reopening_cross_is_not_excused();
    test_an_open_window_on_a_clean_book_does_not_trip_the_cap();
    test_operational_halt_for_this_market_excuses_a_crossing();
    test_operational_halt_for_another_market_excuses_nothing();
    test_operational_halt_release_restores_the_gate();
    test_stripping_trading_actions_is_an_error_not_a_quiet_pass();
    test_the_differential_detects_a_book_that_is_wrong();
    test_census_rejects_an_unknown_message_type();
    test_census_rejects_a_length_mismatch();
    test_census_rejects_a_truncated_body();
    test_census_rejects_trailing_bytes();
    test_a_session_without_the_final_c_is_a_well_formed_prefix();
    test_determinism_hash_separates_two_different_sessions();

    if (failures == 0) {
        std::printf("all gate negative tests passed\n");
    } else {
        std::printf("%d failure(s)\n", failures);
    }
    return failures ? 1 : 0;
}
