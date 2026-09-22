// test_messages -- a byte-level fixture for every ITCH 5.0 message type.
//
// Correctness layer 1. Each fixture is written out byte by byte from the
// specification tables rather than captured from a session file, then decoded
// through the typed view and checked field by field. A transcription error in
// an offset fails here instead of propagating into the book as a plausible
// wrong value.
//
// Every fixture carries a NONZERO tracking number. The tracking number shares
// its 8-byte load with the timestamp (docs/design.md record 002), so a zero
// there would hide a masking error in either direction.
//
// Field values are chosen to be distinguishable: no two numeric fields in a
// message share a value, and no value is a small integer that a neighbouring
// field could plausibly produce, so a swapped pair of offsets fails rather
// than coincidentally passing.

#include "carteret/messages.hpp"
#include "carteret/spec.hpp"

#include "itch_builder.hpp"

#include <cstdio>
#include <string_view>
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

// Header values shared by every fixture. The locate and tracking number are
// distinct and both nonzero.
constexpr std::uint16_t kLocate = 0x2A5B;   // 10843
constexpr std::uint16_t kTracking = 0xBEEF; // 48879
constexpr std::uint64_t kTs = 0x00001F2B3C4DULL;

Bytes header(char type) {
    return carteret::test::header(type, kLocate, kTs, kTracking);
}

// Every fixture runs this first, so a header regression fails once per type
// rather than silently in one.
template<class View>
void check_header(const View& v, char type) {
    CHECK(v.type() == static_cast<unsigned char>(type));
    CHECK(v.locate() == kLocate);
    CHECK(v.track() == kTracking);
    CHECK(v.ts() == kTs);
}

// Builds the view, asserts the fixture is exactly the specification length,
// and checks the header.
template<class View>
View bind(const Bytes& m, char type) {
    CHECK(m.size() == kMsgLen[static_cast<unsigned char>(type)]);
    View v{};
    v.p = m.data();
    check_header(v, type);
    return v;
}

// --- 'S' System Event (12) -------------------------------------------------
void test_system_event() {
    Bytes m = header('S');
    m.u8('Q'); // start of market hours
    auto v = bind<SystemEvent>(m, 'S');
    CHECK(v.event_code() == 'Q');
}

// --- 'R' Stock Directory (39) ----------------------------------------------
void test_stock_directory() {
    Bytes m = header('R');
    m.alpha("AAPL", 8);
    m.u8('Q'); // market category: NASDAQ Global Select
    m.u8('N'); // financial status: not deficient
    m.u32(100);
    m.u8('N'); // round lots only
    m.u8('C'); // issue classification: common stock
    m.alpha("Z", 2);
    m.u8('P'); // authenticity: live
    m.u8('N'); // short sale threshold
    m.u8('Y'); // IPO flag
    m.u8('1'); // LULD reference price tier
    m.u8('N'); // ETP flag
    m.u32(3);  // ETP leverage factor
    m.u8('N'); // inverse indicator
    auto v = bind<StockDirectory>(m, 'R');
    CHECK(v.stock() == "AAPL");
    CHECK(v.market_category() == 'Q');
    CHECK(v.financial_status() == 'N');
    CHECK(v.round_lot_size() == 100);
    CHECK(v.round_lots_only() == 'N');
    CHECK(v.issue_classification() == 'C');
    CHECK(v.issue_sub_type() == "Z");
    CHECK(v.authenticity() == 'P');
    CHECK(v.short_sale_threshold() == 'N');
    CHECK(v.ipo_flag() == 'Y');
    CHECK(v.luld_ref_price_tier() == '1');
    CHECK(v.etp_flag() == 'N');
    CHECK(v.etp_leverage_factor() == 3);
    CHECK(v.inverse_indicator() == 'N');
}

// --- 'H' Stock Trading Action (25) -----------------------------------------
void test_stock_trading_action() {
    Bytes m = header('H');
    m.alpha("MSFT", 8);
    m.u8('H'); // halted
    m.u8(' '); // reserved
    m.alpha("T1", 4);
    auto v = bind<StockTradingAction>(m, 'H');
    CHECK(v.stock() == "MSFT");
    CHECK(v.trading_state() == 'H');
    CHECK(v.reserved() == ' ');
    CHECK(v.reason() == "T1");
}

// --- 'Y' Reg SHO (20) ------------------------------------------------------
void test_reg_sho() {
    Bytes m = header('Y');
    m.alpha("INTC", 8);
    m.u8('1'); // intraday price drop
    auto v = bind<RegSHO>(m, 'Y');
    CHECK(v.stock() == "INTC");
    CHECK(v.action() == '1');
}

// --- 'L' Market Participant Position (26) ----------------------------------
void test_market_participant() {
    Bytes m = header('L');
    m.alpha("NITE", 4);
    m.alpha("TSLA", 8);
    m.u8('Y'); // primary market maker
    m.u8('N'); // market maker mode: normal
    m.u8('A'); // participant state: active
    auto v = bind<MarketParticipant>(m, 'L');
    CHECK(v.mpid() == "NITE");
    CHECK(v.stock() == "TSLA");
    CHECK(v.primary_market_maker() == 'Y');
    CHECK(v.market_maker_mode() == 'N');
    CHECK(v.participant_state() == 'A');
}

// --- 'V' MWCB Decline Level (35) -------------------------------------------
// The three levels are Price(8). Values exceed 32 bits so that a Price(4)
// decode cannot produce them.
void test_mwcb_decline_level() {
    Bytes m = header('V');
    m.u64(0x0000'0001'1111'1111ULL);
    m.u64(0x0000'0002'2222'2222ULL);
    m.u64(0x0000'0003'3333'3333ULL);
    auto v = bind<MwcbDeclineLevel>(m, 'V');
    CHECK(v.level1() == 0x0000000111111111ULL);
    CHECK(v.level2() == 0x0000000222222222ULL);
    CHECK(v.level3() == 0x0000000333333333ULL);
    CHECK(v.level1() > 0xFFFFFFFFULL); // would be unrepresentable as Price(4)
}

// --- 'W' MWCB Status (12) --------------------------------------------------
void test_mwcb_status() {
    Bytes m = header('W');
    m.u8('2');
    auto v = bind<MwcbStatus>(m, 'W');
    CHECK(v.breached_level() == '2');
}

// --- 'K' IPO Quoting Period Update (28) ------------------------------------
// ReleaseTime is SECONDS since midnight. 34200 is 09:30:00; the same instant
// in nanoseconds would not fit in the 4-byte field, which is the check.
void test_ipo_quoting_period() {
    Bytes m = header('K');
    m.alpha("XYZQ", 8);
    m.u32(34200);
    m.u8('A');      // anticipated quotation release time
    m.u32(1750000); // $175.0000 as binary Price(4)
    auto v = bind<IpoQuotingPeriod>(m, 'K');
    CHECK(v.stock() == "XYZQ");
    CHECK(v.release_time() == 34200);
    CHECK(v.release_time() * 1000000000ULL == 34200ULL * 1000000000ULL);
    CHECK(v.release_qualifier() == 'A');
    CHECK(v.ipo_price() == 1750000);
    CHECK(v.ipo_price() <= kMaxPrice4);
}

// --- 'J' LULD Auction Collar (35) ------------------------------------------
void test_luld_auction_collar() {
    Bytes m = header('J');
    m.alpha("AMZN", 8);
    m.u32(15000000); // reference  $1500.0000
    m.u32(15750000); // upper      $1575.0000
    m.u32(14250000); // lower      $1425.0000
    m.u32(2);        // collar extension count
    auto v = bind<LuldAuctionCollar>(m, 'J');
    CHECK(v.stock() == "AMZN");
    CHECK(v.ref_price() == 15000000);
    CHECK(v.upper_collar() == 15750000);
    CHECK(v.lower_collar() == 14250000);
    CHECK(v.collar_extension() == 2);
    CHECK(v.lower_collar() < v.ref_price());
    CHECK(v.ref_price() < v.upper_collar());
}

// --- 'h' Operational Halt (21) ---------------------------------------------
void test_operational_halt() {
    Bytes m = header('h');
    m.alpha("GOOG", 8);
    m.u8('B'); // BX
    m.u8('H'); // halted
    auto v = bind<OperationalHalt>(m, 'h');
    CHECK(v.stock() == "GOOG");
    CHECK(v.market_code() == 'B');
    CHECK(v.halt_action() == 'H');
    CHECK(v.type() != 'H'); // lowercase 'h', distinct from Stock Trading Action
}

// --- 'A' Add Order (36) ----------------------------------------------------
void test_add_order() {
    Bytes m = header('A');
    m.u64(0x0102030405060708ULL); // every byte distinct: catches a bad swap
    m.u8('B');
    m.u32(500);
    m.alpha("AAPL", 8);
    m.u32(1500000); // $150.0000
    auto v = bind<AddOrder>(m, 'A');
    CHECK(v.order_ref() == 0x0102030405060708ULL);
    CHECK(v.side() == 'B');
    CHECK(v.shares() == 500);
    CHECK(v.stock() == "AAPL");
    CHECK(v.price() == 1500000);
}

// --- 'F' Add Order with MPID (40) ------------------------------------------
// Identical to 'A' through offset 36. Checked against the 'A' offsets
// explicitly, because the book relies on that shared prefix.
void test_add_order_mpid() {
    Bytes m = header('F');
    m.u64(0x0102030405060708ULL);
    m.u8('S');
    m.u32(700);
    m.alpha("MSFT", 8);
    m.u32(2500000);
    m.alpha("NITE", 4);
    auto v = bind<AddOrderMpid>(m, 'F');
    CHECK(v.order_ref() == 0x0102030405060708ULL);
    CHECK(v.side() == 'S');
    CHECK(v.shares() == 700);
    CHECK(v.stock() == "MSFT");
    CHECK(v.price() == 2500000);
    CHECK(v.attribution() == "NITE");

    static_assert(off::add_mpid::kOrderRef == off::add::kOrderRef);
    static_assert(off::add_mpid::kSide == off::add::kSide);
    static_assert(off::add_mpid::kShares == off::add::kShares);
    static_assert(off::add_mpid::kStock == off::add::kStock);
    static_assert(off::add_mpid::kPrice == off::add::kPrice);
}

// --- 'E' Order Executed (31) -----------------------------------------------
void test_order_executed() {
    Bytes m = header('E');
    m.u64(0x0102030405060708ULL);
    m.u32(300);
    m.u64(0x1122334455667788ULL);
    auto v = bind<OrderExecuted>(m, 'E');
    CHECK(v.order_ref() == 0x0102030405060708ULL);
    CHECK(v.executed_shares() == 300);
    CHECK(v.match_number() == 0x1122334455667788ULL);
    static_assert(kMsgLen['E'] == 31); // no price field; the resting price applies
}

// --- 'C' Order Executed With Price (36) ------------------------------------
void test_order_executed_price() {
    Bytes m = header('C');
    m.u64(0x0102030405060708ULL);
    m.u32(150);
    m.u64(0x1122334455667788ULL);
    m.u8('N'); // not printed to the tape; book effect is unchanged
    m.u32(1499900);
    auto v = bind<OrderExecutedPrice>(m, 'C');
    CHECK(v.order_ref() == 0x0102030405060708ULL);
    CHECK(v.executed_shares() == 150);
    CHECK(v.match_number() == 0x1122334455667788ULL);
    CHECK(v.printable() == 'N');
    CHECK(v.exec_price() == 1499900);
}

// --- 'X' Order Cancel (23) -------------------------------------------------
void test_order_cancel() {
    Bytes m = header('X');
    m.u64(0x0102030405060708ULL);
    m.u32(200);
    auto v = bind<OrderCancel>(m, 'X');
    CHECK(v.order_ref() == 0x0102030405060708ULL);
    CHECK(v.cancelled_shares() == 200);
}

// --- 'D' Order Delete (19) -------------------------------------------------
void test_order_delete() {
    Bytes m = header('D');
    m.u64(0x0102030405060708ULL);
    auto v = bind<OrderDelete>(m, 'D');
    CHECK(v.order_ref() == 0x0102030405060708ULL);
}

// --- 'U' Order Replace (35) ------------------------------------------------
// Two references. The old one is retired; the new one carries every later
// update. Shares is a new total, not a decrement.
void test_order_replace() {
    Bytes m = header('U');
    m.u64(0x0102030405060708ULL);
    m.u64(0x090A0B0C0D0E0F10ULL);
    m.u32(250);
    m.u32(1500100);
    auto v = bind<OrderReplace>(m, 'U');
    CHECK(v.old_order_ref() == 0x0102030405060708ULL);
    CHECK(v.new_order_ref() == 0x090A0B0C0D0E0F10ULL);
    CHECK(v.old_order_ref() != v.new_order_ref());
    CHECK(v.shares() == 250);
    CHECK(v.price() == 1500100);
}

// --- 'P' Trade, non-cross (44) ---------------------------------------------
// The fixture carries the zero order reference and hardcoded 'B' side that
// real sessions carry, so the test documents what those fields are worth.
void test_trade() {
    Bytes m = header('P');
    m.u64(0);
    m.u8('B');
    m.u32(400);
    m.alpha("NVDA", 8);
    m.u32(3000000);
    m.u64(0x1122334455667788ULL);
    auto v = bind<Trade>(m, 'P');
    CHECK(v.order_ref() == 0); // zero since December 2010
    CHECK(v.side() == 'B');    // hardcoded since 14 July 2014
    CHECK(v.shares() == 400);
    CHECK(v.stock() == "NVDA");
    CHECK(v.price() == 3000000);
    CHECK(v.match_number() == 0x1122334455667788ULL);
    CHECK(!touches_book('P'));
}

// --- 'Q' Cross Trade (40) --------------------------------------------------
// Shares is an 8-byte field here, unlike the 4-byte Shares on 'A' and 'P'.
void test_cross_trade() {
    Bytes m = header('Q');
    m.u64(1234567890ULL);
    m.alpha("AAPL", 8);
    m.u32(1500000);
    m.u64(0x1122334455667788ULL);
    m.u8('O'); // opening cross
    auto v = bind<CrossTrade>(m, 'Q');
    CHECK(v.shares() == 1234567890ULL);
    CHECK(v.stock() == "AAPL");
    CHECK(v.cross_price() == 1500000);
    CHECK(v.match_number() == 0x1122334455667788ULL);
    CHECK(v.cross_type() == 'O');
    CHECK(!touches_book('Q'));
}

// Zero shares on a cross trade is valid, not corrupt: the specification
// permits it when order interest was insufficient. See docs/design.md
// record 007.
void test_cross_trade_zero_shares() {
    Bytes m = header('Q');
    m.u64(0);
    m.alpha("AAPL", 8);
    m.u32(1500000);
    m.u64(0x1122334455667788ULL);
    m.u8('C');
    auto v = bind<CrossTrade>(m, 'Q');
    CHECK(v.shares() == 0);
    CHECK(v.cross_price() == 1500000); // the rest of the message still decodes
    CHECK(v.cross_type() == 'C');
}

// --- 'B' Broken Trade (19) -------------------------------------------------
void test_broken_trade() {
    Bytes m = header('B');
    m.u64(0x1122334455667788ULL);
    auto v = bind<BrokenTrade>(m, 'B');
    CHECK(v.match_number() == 0x1122334455667788ULL);
    CHECK(!touches_book('B'));
}

// --- 'I' NOII (50) ---------------------------------------------------------
// The longest message. Paired and imbalance shares are 8 bytes; the three
// prices are Price(4).
void test_noii() {
    Bytes m = header('I');
    m.u64(1000000ULL);
    m.u64(250000ULL);
    m.u8('B'); // buy imbalance
    m.alpha("AAPL", 8);
    m.u32(1510000); // far
    m.u32(1505000); // near
    m.u32(1500000); // reference
    m.u8('O');      // opening cross
    m.u8(' ');      // price variation indicator
    auto v = bind<Noii>(m, 'I');
    CHECK(v.paired_shares() == 1000000ULL);
    CHECK(v.imbalance_shares() == 250000ULL);
    CHECK(v.imbalance_direction() == 'B');
    CHECK(v.stock() == "AAPL");
    CHECK(v.far_price() == 1510000);
    CHECK(v.near_price() == 1505000);
    CHECK(v.ref_price() == 1500000);
    CHECK(v.cross_type() == 'O');
    CHECK(v.price_variation() == ' ');
    static_assert(kMsgLen['I'] == kMaxMsgLen);
}

// Values introduced after the sample period decode as ordinary bytes. No
// session in docs/data.md contains them, so this fixture is their only
// coverage.
void test_noii_post_2020_values() {
    Bytes m = header('I');
    m.u64(0);
    m.u64(0);
    m.u8('P'); // paused; added 2023
    m.alpha("AAPL", 8);
    m.u32(0);
    m.u32(0);
    m.u32(0);
    m.u8('A'); // extended trading close; added 2022
    m.u8(' ');
    auto v = bind<Noii>(m, 'I');
    CHECK(v.imbalance_direction() == 'P');
    CHECK(v.cross_type() == 'A');
}

// --- 'N' RPII (20) ---------------------------------------------------------
void test_rpii() {
    Bytes m = header('N');
    m.alpha("AAPL", 8);
    m.u8('A'); // interest on both sides
    auto v = bind<Rpii>(m, 'N');
    CHECK(v.stock() == "AAPL");
    CHECK(v.interest_flag() == 'A');
}

// --- 'O' Direct Listing With Capital Raise (48) ----------------------------
// Introduced 2023-04-28 and absent from every session in docs/data.md, so this
// fixture is the only coverage the layout has.
void test_direct_listing_cap_raise() {
    Bytes m = header('O');
    m.alpha("DLCR", 8);
    m.u8('Y');
    m.u32(1000000); // min allowable
    m.u32(3000000); // max allowable
    m.u32(2000000); // near execution
    m.u64(0x1122334455667788ULL);
    m.u32(1800000); // lower collar
    m.u32(2200000); // upper collar
    auto v = bind<DirectListingCapRaise>(m, 'O');
    CHECK(v.stock() == "DLCR");
    CHECK(v.open_eligibility() == 'Y');
    CHECK(v.min_allowable_price() == 1000000);
    CHECK(v.max_allowable_price() == 3000000);
    CHECK(v.near_exec_price() == 2000000);
    CHECK(v.near_exec_time() == 0x1122334455667788ULL);
    CHECK(v.lower_collar() == 1800000);
    CHECK(v.upper_collar() == 2200000);
}

// ---------------------------------------------------------------------------
// Alpha decoding
// ---------------------------------------------------------------------------

// Symbols are left justified and space padded on the right. A symbol that
// fills the field must not lose its last character, and an all-space field
// must come back empty rather than as eight spaces.
void test_alpha_padding() {
    Bytes full = header('R');
    full.alpha("ABCDEFGH", 8);
    for (int i = 0; i < 20; ++i) full.u8(0);
    StockDirectory v{};
    v.p = full.data();
    CHECK(v.stock() == "ABCDEFGH");
    CHECK(v.stock().size() == 8);

    Bytes blank = header('R');
    blank.alpha("", 8);
    for (int i = 0; i < 20; ++i) blank.u8(0);
    StockDirectory w{};
    w.p = blank.data();
    CHECK(w.stock().empty());

    // An interior space is part of the symbol and must survive.
    Bytes mid = header('R');
    mid.alpha("A B", 8);
    for (int i = 0; i < 20; ++i) mid.u8(0);
    StockDirectory x{};
    x.p = mid.data();
    CHECK(x.stock() == "A B");
}

} // namespace

int main() {
    test_system_event();
    test_stock_directory();
    test_stock_trading_action();
    test_reg_sho();
    test_market_participant();
    test_mwcb_decline_level();
    test_mwcb_status();
    test_ipo_quoting_period();
    test_luld_auction_collar();
    test_operational_halt();
    test_add_order();
    test_add_order_mpid();
    test_order_executed();
    test_order_executed_price();
    test_order_cancel();
    test_order_delete();
    test_order_replace();
    test_trade();
    test_cross_trade();
    test_cross_trade_zero_shares();
    test_broken_trade();
    test_noii();
    test_noii_post_2020_values();
    test_rpii();
    test_direct_listing_cap_raise();
    test_alpha_padding();

    if (failures == 0) {
        std::printf("all message fixtures passed\n");
    } else {
        std::printf("%d failure(s)\n", failures);
    }
    return failures ? 1 : 0;
}
