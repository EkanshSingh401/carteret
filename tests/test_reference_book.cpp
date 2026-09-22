// test_reference_book -- one test per semantic rule the reference book implements.
//
// The reference book is the differential oracle (docs/design.md record 014),
// so an error here is an error in the standard every later implementation is
// measured against. Each test drives a hand-written event script through the
// parser and asserts on the resulting book, rather than calling the book's
// methods directly, so the wiring between dispatch and book is covered too.

#include "carteret/parser.hpp"
#include "carteret/reference_book.hpp"

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

// Builds a session out of message bodies and replays it into a book.
struct Script {
    std::vector<unsigned char> buf;
    std::uint64_t ts = 34200ULL * 1000000000ULL;

    std::uint64_t next_ts() { return ts += 1000; }

    void add(Ref ref, unsigned char side, Price price, Shares shares,
             std::string_view stock = "AAPL") {
        Bytes m = carteret::test::header('A', kLoc, next_ts());
        m.u64(ref);
        m.u8(side);
        m.u32(shares);
        m.alpha(stock, 8);
        m.u32(price);
        carteret::test::frame(buf, m);
    }

    void add_mpid(Ref ref, unsigned char side, Price price, Shares shares,
                  std::string_view mpid) {
        Bytes m = carteret::test::header('F', kLoc, next_ts());
        m.u64(ref);
        m.u8(side);
        m.u32(shares);
        m.alpha("AAPL", 8);
        m.u32(price);
        m.alpha(mpid, 4);
        carteret::test::frame(buf, m);
    }

    void execute(Ref ref, Shares qty) {
        Bytes m = carteret::test::header('E', kLoc, next_ts());
        m.u64(ref);
        m.u32(qty);
        m.u64(1);
        carteret::test::frame(buf, m);
    }

    void execute_price(Ref ref, Shares qty, Price price, unsigned char printable) {
        Bytes m = carteret::test::header('C', kLoc, next_ts());
        m.u64(ref);
        m.u32(qty);
        m.u64(1);
        m.u8(printable);
        m.u32(price);
        carteret::test::frame(buf, m);
    }

    void cancel(Ref ref, Shares qty) {
        Bytes m = carteret::test::header('X', kLoc, next_ts());
        m.u64(ref);
        m.u32(qty);
        carteret::test::frame(buf, m);
    }

    void del(Ref ref) {
        Bytes m = carteret::test::header('D', kLoc, next_ts());
        m.u64(ref);
        carteret::test::frame(buf, m);
    }

    void replace(Ref old_ref, Ref new_ref, Price price, Shares shares) {
        Bytes m = carteret::test::header('U', kLoc, next_ts());
        m.u64(old_ref);
        m.u64(new_ref);
        m.u32(shares);
        m.u32(price);
        carteret::test::frame(buf, m);
    }

    void trade(Price price, Shares shares) {
        Bytes m = carteret::test::header('P', kLoc, next_ts());
        m.u64(0);
        m.u8('B');
        m.u32(shares);
        m.alpha("AAPL", 8);
        m.u32(price);
        m.u64(2);
        carteret::test::frame(buf, m);
    }

    void cross(Shares shares, Price price, unsigned char type) {
        Bytes m = carteret::test::header('Q', kLoc, next_ts());
        m.u64(shares);
        m.alpha("AAPL", 8);
        m.u32(price);
        m.u64(3);
        m.u8(type);
        carteret::test::frame(buf, m);
    }

    void broken() {
        Bytes m = carteret::test::header('B', kLoc, next_ts());
        m.u64(3);
        carteret::test::frame(buf, m);
    }

    void system_event(unsigned char code) {
        Bytes m = carteret::test::header('S', 0, next_ts());
        m.u8(code);
        carteret::test::frame(buf, m);
    }

    ParseStats replay(ReferenceBook& book) {
        carteret::test::frame_end(buf);
        Parser<ReferenceBook> parser(book);
        return parser.run({buf.data(), buf.size()});
    }
};

// --- record 008: cumulative deductions; removal at zero without a 'D' ------

void test_execute_deducts_cumulatively() {
    Script s;
    s.add(1, kBuy, 1500000, 500);
    s.execute(1, 100);
    s.execute(1, 150);
    ReferenceBook b;
    s.replay(b);

    const RefOrder* o = b.order(1);
    CHECK(o != nullptr);
    CHECK(o && o->shares == 250); // 500 - 100 - 150, not "set to 150"
    const LevelSnapshot lv = b.level(kLoc, kBuy, 1500000);
    CHECK(lv.present);
    CHECK(lv.shares == 250);
    CHECK(lv.orders == 1);
    CHECK(b.check_invariants() == 0);
}

void test_order_removed_at_zero_without_delete() {
    Script s;
    s.add(1, kBuy, 1500000, 200);
    s.execute(1, 200);
    ReferenceBook b;
    s.replay(b);

    CHECK(b.order(1) == nullptr);
    CHECK(b.live_orders() == 0);
    CHECK(!b.level(kLoc, kBuy, 1500000).present); // the empty level is dropped
    CHECK(b.counters().removed_at_zero == 1);
    CHECK(b.counters().orphan_delete == 0);
    CHECK(b.check_invariants() == 0);
}

void test_cancel_is_partial() {
    Script s;
    s.add(1, kSell, 1500000, 400);
    s.cancel(1, 100);
    ReferenceBook b;
    s.replay(b);

    const RefOrder* o = b.order(1);
    CHECK(o && o->shares == 300);
    CHECK(b.level(kLoc, kSell, 1500000).shares == 300);
    CHECK(b.counters().removed_at_zero == 0);
    CHECK(b.check_invariants() == 0);
}

void test_cancel_to_zero_removes() {
    Script s;
    s.add(1, kSell, 1500000, 400);
    s.cancel(1, 400);
    ReferenceBook b;
    s.replay(b);
    CHECK(b.live_orders() == 0);
    CHECK(b.counters().removed_at_zero == 1);
}

// A 'D' after the order has already reached zero names a reference that no
// longer exists. That is an orphan, counted and not an error (record 007).
void test_delete_after_zero_is_an_orphan() {
    Script s;
    s.add(1, kBuy, 1500000, 100);
    s.execute(1, 100);
    s.del(1);
    ReferenceBook b;
    s.replay(b);
    CHECK(b.counters().orphan_delete == 1);
    CHECK(b.live_orders() == 0);
}

// --- record 010: 'E' executes at the resting price; 'C' carries its own ----

// The execution price on a 'C' does not move the order: the resting order
// stays at the price it was added at, and only its size changes.
void test_execute_with_price_does_not_move_the_order() {
    Script s;
    s.add(1, kBuy, 1500000, 500);
    s.execute_price(1, 200, 1499000, 'N'); // a different price, not printable
    ReferenceBook b;
    s.replay(b);

    const RefOrder* o = b.order(1);
    CHECK(o && o->price == 1500000);
    CHECK(o && o->shares == 300);
    CHECK(b.level(kLoc, kBuy, 1500000).shares == 300);
    CHECK(!b.level(kLoc, kBuy, 1499000).present);
    CHECK(b.check_invariants() == 0);
}

// Printable 'N' keeps the execution off the consolidated tape. The book effect
// is identical, so the two must produce the same state.
void test_printable_flag_does_not_change_the_book() {
    Script yes;
    yes.add(1, kBuy, 1500000, 500);
    yes.execute_price(1, 200, 1500000, 'Y');
    ReferenceBook by;
    yes.replay(by);

    Script no;
    no.add(1, kBuy, 1500000, 500);
    no.execute_price(1, 200, 1500000, 'N');
    ReferenceBook bn;
    no.replay(bn);

    CHECK(by.level(kLoc, kBuy, 1500000).shares == bn.level(kLoc, kBuy, 1500000).shares);
    CHECK(by.live_orders() == bn.live_orders());
}

// --- record 009: 'U' retains fields and loses queue priority ---------------

void test_replace_retains_side_stock_and_attribution() {
    Script s;
    s.add_mpid(1, kSell, 1500000, 300, "NITE");
    s.replace(1, 2, 1510000, 250);
    ReferenceBook b;
    s.replay(b);

    CHECK(b.order(1) == nullptr); // the old reference is retired
    const RefOrder* o = b.order(2);
    CHECK(o != nullptr);
    CHECK(o && o->side == kSell);                                     // 'U' carries no side
    CHECK(o && std::string_view(o->stock.data(), 4) == "AAPL");       // nor stock
    CHECK(o && std::string_view(o->attribution.data(), 4) == "NITE"); // nor attribution
    CHECK(o && o->price == 1510000);
    CHECK(o && o->shares == 250); // a new total, not a decrement
    CHECK(!b.level(kLoc, kSell, 1500000).present);
    CHECK(b.level(kLoc, kSell, 1510000).shares == 250);
    CHECK(b.check_invariants() == 0);
}

// The replaced order goes to the back of the queue at its new price even when
// the price is unchanged. Assuming otherwise inflates every simulated fill
// rate, so this is the test that matters most for the queue study.
void test_replace_at_the_same_price_loses_priority() {
    Script s;
    s.add(1, kBuy, 1500000, 100); // first in queue
    s.add(2, kBuy, 1500000, 100);
    s.add(3, kBuy, 1500000, 100);
    s.replace(1, 4, 1500000, 100); // same price
    ReferenceBook b;
    s.replay(b);

    const LevelSnapshot lv = b.level(kLoc, kBuy, 1500000);
    CHECK(lv.orders == 3);
    CHECK(lv.fifo.size() == 3);
    CHECK(lv.fifo.size() == 3 && lv.fifo[0] == 2);
    CHECK(lv.fifo.size() == 3 && lv.fifo[1] == 3);
    CHECK(lv.fifo.size() == 3 && lv.fifo[2] == 4); // the replacement is last
    CHECK(b.check_invariants() == 0);
}

// A replace performs two book operations -- remove the old order, insert the
// new one -- but it is one message, and the message counter has to say so or
// every per-message rate derived from it is wrong.
void test_replace_counts_as_one_message() {
    Script s;
    s.add(1, kBuy, 1500000, 100);
    s.replace(1, 2, 1500000, 100);
    ReferenceBook b;
    s.replay(b);
    CHECK(b.counters().book_messages == 2); // one add, one replace
}

void test_replace_of_an_unknown_reference_is_an_orphan() {
    Script s;
    s.replace(99, 100, 1500000, 100);
    ReferenceBook b;
    s.replay(b);
    CHECK(b.counters().orphan_replace == 1);
    CHECK(b.live_orders() == 0); // no order is invented for the new reference
}

// --- FIFO ordering --------------------------------------------------------

void test_fifo_order_is_arrival_order() {
    Script s;
    s.add(10, kBuy, 1500000, 100);
    s.add(20, kBuy, 1500000, 200);
    s.add(30, kBuy, 1500000, 300);
    s.del(20); // removing from the middle must not disturb the rest
    ReferenceBook b;
    s.replay(b);

    const LevelSnapshot lv = b.level(kLoc, kBuy, 1500000);
    CHECK(lv.fifo.size() == 2);
    CHECK(lv.fifo.size() == 2 && lv.fifo[0] == 10);
    CHECK(lv.fifo.size() == 2 && lv.fifo[1] == 30);
    CHECK(lv.shares == 400);
    CHECK(b.check_invariants() == 0);
}

// --- record 011: 'P', 'Q' and 'B' have no book effect ----------------------

void test_trade_cross_and_broken_have_no_book_effect() {
    Script s;
    s.add(1, kBuy, 1500000, 500);
    s.trade(1500000, 300);
    s.cross(1000, 1500000, 'O');
    s.broken();
    ReferenceBook b;
    s.replay(b);

    CHECK(b.level(kLoc, kBuy, 1500000).shares == 500); // untouched
    CHECK(b.live_orders() == 1);
    CHECK(b.check_invariants() == 0);
}

// A cross trade reporting zero shares is valid, not corrupt (record 007).
void test_zero_share_cross_is_counted_not_rejected() {
    Script s;
    s.add(1, kBuy, 1500000, 500);
    s.cross(0, 1500000, 'O');
    ReferenceBook b;
    const ParseStats st = s.replay(b);

    CHECK(st.clean()); // not a framing error
    CHECK(b.counters().zero_share_cross == 1);
    CHECK(b.level(kLoc, kBuy, 1500000).shares == 500);
}

// --- record 007: crossed and locked books are counted, not repaired --------

void test_crossed_and_locked_books_are_counted() {
    Script s;
    s.add(1, kBuy, 1500000, 100);
    s.add(2, kSell, 1500000, 100); // locked: bid == ask
    s.add(3, kSell, 1490000, 100); // crossed: bid > ask
    ReferenceBook b;
    s.replay(b);

    const RefSymbol* sym = b.symbol(kLoc);
    CHECK(sym != nullptr);
    CHECK(sym && sym->best_bid() == 1500000);
    CHECK(sym && sym->best_ask() == 1490000); // left crossed, not repaired
    CHECK(b.counters().locked_observations >= 1);
    CHECK(b.counters().crossed_observations >= 1);
    CHECK(b.check_invariants() == 0);
}

// 'B' and 'D' can arrive after the end-of-system-hours event, so the book must
// keep applying them rather than stopping at 'E'.
void test_messages_after_end_of_system_hours_are_applied() {
    Script s;
    s.add(1, kBuy, 1500000, 100);
    s.system_event('E');
    s.broken();
    s.del(1);
    ReferenceBook b;
    s.replay(b);

    CHECK(b.live_orders() == 0); // the post-'E' delete took effect
    CHECK(b.counters().after_end_of_system_hours == 2);
    CHECK(b.counters().orphan_delete == 0);
}

void test_orphan_modifies_are_counted() {
    Script s;
    s.execute(404, 100);
    s.execute_price(405, 100, 1500000, 'Y');
    s.cancel(406, 100);
    s.del(407);
    ReferenceBook b;
    const ParseStats st = s.replay(b);

    CHECK(st.clean());
    CHECK(b.counters().orphan_execute == 1);
    CHECK(b.counters().orphan_execute_price == 1);
    CHECK(b.counters().orphan_cancel == 1);
    CHECK(b.counters().orphan_delete == 1);
    CHECK(b.counters().orphans() == 4);
    CHECK(b.live_orders() == 0);
}

// An execution larger than the resting size cannot occur in a correct feed.
// It is counted and the order removed, rather than wrapping the unsigned
// subtraction into a very large share count.
void test_over_execution_is_counted_and_does_not_wrap() {
    Script s;
    s.add(1, kBuy, 1500000, 100);
    s.execute(1, 500);
    ReferenceBook b;
    s.replay(b);

    CHECK(b.counters().over_execute == 1);
    CHECK(b.live_orders() == 0);
    CHECK(!b.level(kLoc, kBuy, 1500000).present);
    CHECK(b.check_invariants() == 0);
}

// --- both sides -----------------------------------------------------------

void test_sides_are_independent() {
    Script s;
    s.add(1, kBuy, 1490000, 100);
    s.add(2, kBuy, 1495000, 200);
    s.add(3, kSell, 1505000, 300);
    s.add(4, kSell, 1510000, 400);
    ReferenceBook b;
    s.replay(b);

    const RefSymbol* sym = b.symbol(kLoc);
    CHECK(sym && sym->best_bid() == 1495000); // highest bid
    CHECK(sym && sym->best_ask() == 1505000); // lowest ask
    CHECK(sym && sym->bids.size() == 2);
    CHECK(sym && sym->asks.size() == 2);
    CHECK(b.counters().crossed_observations == 0);
    CHECK(b.counters().locked_observations == 0);
    CHECK(b.check_invariants() == 0);
}

// A longer script, to confirm the invariants hold across mixed flow rather
// than only in the single-message cases above.
void test_invariants_hold_over_mixed_flow() {
    Script s;
    Ref ref = 1;
    for (int i = 0; i < 40; ++i) {
        const Price px = 1500000 + static_cast<Price>((i % 7) * 100);
        s.add(ref, (i % 2) ? kBuy : kSell, px, static_cast<Shares>(100 * (1 + i % 5)));
        ++ref;
    }
    for (Ref r = 1; r <= 40; r += 3) s.execute(r, 100);
    for (Ref r = 2; r <= 40; r += 5) s.cancel(r, 100);
    for (Ref r = 3; r <= 40; r += 7) {
        s.replace(r, ref, 1500500, 150);
        ++ref;
    }
    for (Ref r = 4; r <= 40; r += 4) s.del(r);

    ReferenceBook b;
    const ParseStats st = s.replay(b);
    CHECK(st.clean());
    CHECK(b.check_invariants() == 0);
    CHECK(b.counters().book_messages > 0);
}

} // namespace

int main() {
    test_execute_deducts_cumulatively();
    test_order_removed_at_zero_without_delete();
    test_cancel_is_partial();
    test_cancel_to_zero_removes();
    test_delete_after_zero_is_an_orphan();
    test_execute_with_price_does_not_move_the_order();
    test_printable_flag_does_not_change_the_book();
    test_replace_retains_side_stock_and_attribution();
    test_replace_at_the_same_price_loses_priority();
    test_replace_counts_as_one_message();
    test_replace_of_an_unknown_reference_is_an_orphan();
    test_fifo_order_is_arrival_order();
    test_trade_cross_and_broken_have_no_book_effect();
    test_zero_share_cross_is_counted_not_rejected();
    test_crossed_and_locked_books_are_counted();
    test_messages_after_end_of_system_hours_are_applied();
    test_orphan_modifies_are_counted();
    test_over_execution_is_counted_and_does_not_wrap();
    test_sides_are_independent();
    test_invariants_hold_over_mixed_flow();

    if (failures == 0) {
        std::printf("all reference book tests passed\n");
    } else {
        std::printf("%d failure(s)\n", failures);
    }
    return failures ? 1 : 0;
}
