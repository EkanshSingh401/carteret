// carteret/spec.hpp -- NASDAQ TotalView-ITCH 5.0 wire constants and field offsets.
//
// Source: NQTVITCHspecification.pdf, revision 2023-04-28, sections 1.1-1.8.
//
// Conventions, from the specification's Data Types section:
//   - Integer fields are big-endian (network byte order), unsigned.
//   - Alpha fields are ASCII, left justified, space padded right.
//   - Price(4) is a 32-bit integer with 4 implied decimals. The maximum is
//     200000.0000 = 0x77359400.
//   - Price(8) is the 64-bit form, also with 4 implied decimals.
//   - Timestamps are 6 bytes: nanoseconds since midnight.
//   - Stock Locate occupies offset 1 in every message, which lets a consumer
//     filter by symbol without decoding the body.
//
// Every field of every message type is declared below, with its wire type
// named beside it, and every layout is checked at compile time by the tiling
// audit at the foot of this file. See docs/design.md record 003.

#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>

static_assert(std::endian::native == std::endian::little,
              "carteret assumes a little-endian host; wire.hpp byte-swaps on "
              "that assumption.");

namespace carteret {

// ---------------------------------------------------------------------------
// Message types
// ---------------------------------------------------------------------------

enum class MsgType : unsigned char {
    SystemEvent = 'S',
    StockDirectory = 'R',
    StockTradingAction = 'H',
    RegSHO = 'Y',
    MarketParticipant = 'L',
    MwcbDeclineLevel = 'V',
    MwcbStatus = 'W',
    IpoQuotingPeriod = 'K',
    LuldAuctionCollar = 'J',
    OperationalHalt = 'h', // lowercase; distinct from 'H' Stock Trading Action
    AddOrder = 'A',
    AddOrderMpid = 'F',
    OrderExecuted = 'E',
    OrderExecutedPrice = 'C',
    OrderCancel = 'X',
    OrderDelete = 'D',
    OrderReplace = 'U',
    Trade = 'P',
    CrossTrade = 'Q',
    BrokenTrade = 'B',
    Noii = 'I',
    Rpii = 'N',
    DirectListingCapRaise = 'O',
};

// On-the-wire length of each message, including the leading type byte and
// excluding the 2-byte BinaryFILE length prefix. A zero entry marks a type
// byte that is not an ITCH 5.0 message type.
inline constexpr std::array<std::uint8_t, 256> kMsgLen = [] {
    std::array<std::uint8_t, 256> t{};
    t['S'] = 12;
    t['R'] = 39;
    t['H'] = 25;
    t['Y'] = 20;
    t['L'] = 26;
    t['V'] = 35;
    t['W'] = 12;
    t['K'] = 28;
    t['J'] = 35;
    t['h'] = 21;
    t['A'] = 36;
    t['F'] = 40;
    t['E'] = 31;
    t['C'] = 36;
    t['X'] = 23;
    t['D'] = 19;
    t['U'] = 35;
    t['P'] = 44;
    t['Q'] = 40;
    t['B'] = 19;
    t['I'] = 50;
    t['N'] = 20;
    t['O'] = 48;
    return t;
}();

inline constexpr std::size_t kMaxMsgLen = 50; // 'I' (NOII) is the longest message
inline constexpr std::size_t kHeaderLen = 11; // common to every message type

// ---------------------------------------------------------------------------
// Field offsets
//
// The wire type of each field is named in the trailing comment as
// "<length> <type>", where <type> is one of: alpha, int, Price(4), Price(8).
// ---------------------------------------------------------------------------

namespace off {

// Common header, present in every message.
inline constexpr std::size_t kType = 0;        // 1 alpha
inline constexpr std::size_t kStockLocate = 1; // 2 int
inline constexpr std::size_t kTracking = 3;    // 2 int
inline constexpr std::size_t kTimestamp = 5;   // 6 int, ns since midnight

// 'S' System Event (12)
//
// Event codes: 'O' start of messages, 'S' start of system hours, 'Q' start of
// market hours, 'M' end of market hours, 'E' end of system hours, 'C' end of
// messages.
//
// Broken Trade ('B') and Order Delete ('D') can still arrive after the 'E'
// end-of-system-hours event. A reconstruction that stops consuming at 'E', or
// that treats a post-'E' message as corruption, loses them.
namespace sysevent {
inline constexpr std::size_t kEventCode = 11; // 1 alpha
} // namespace sysevent

// 'R' Stock Directory (39)
namespace stockdir {
inline constexpr std::size_t kStock = 11;               // 8 alpha
inline constexpr std::size_t kMarketCategory = 19;      // 1 alpha
inline constexpr std::size_t kFinancialStatus = 20;     // 1 alpha
inline constexpr std::size_t kRoundLotSize = 21;        // 4 int
inline constexpr std::size_t kRoundLotsOnly = 25;       // 1 alpha
inline constexpr std::size_t kIssueClassification = 26; // 1 alpha
inline constexpr std::size_t kIssueSubType = 27;        // 2 alpha
inline constexpr std::size_t kAuthenticity = 29;        // 1 alpha, 'P' live | 'T' test
inline constexpr std::size_t kShortSaleThreshold = 30;  // 1 alpha
inline constexpr std::size_t kIPOFlag = 31;             // 1 alpha
inline constexpr std::size_t kLULDRefPriceTier = 32;    // 1 alpha
inline constexpr std::size_t kETPFlag = 33;             // 1 alpha
inline constexpr std::size_t kETPLeverageFactor = 34;   // 4 int
inline constexpr std::size_t kInverseIndicator = 38;    // 1 alpha
} // namespace stockdir

// 'H' Stock Trading Action (25)
namespace tradingaction {
inline constexpr std::size_t kStock = 11;        // 8 alpha
inline constexpr std::size_t kTradingState = 19; // 1 alpha, 'H'|'P'|'Q'|'T'
inline constexpr std::size_t kReserved = 20;     // 1 alpha
inline constexpr std::size_t kReason = 21;       // 4 alpha
} // namespace tradingaction

// 'Y' Reg SHO Short Sale Price Test Restricted Indicator (20)
namespace regsho {
inline constexpr std::size_t kStock = 11;        // 8 alpha
inline constexpr std::size_t kRegSHOAction = 19; // 1 alpha
} // namespace regsho

// 'L' Market Participant Position (26)
namespace mpart {
inline constexpr std::size_t kMPID = 11;                   // 4 alpha
inline constexpr std::size_t kStock = 15;                  // 8 alpha
inline constexpr std::size_t kPrimaryMarketMaker = 23;     // 1 alpha
inline constexpr std::size_t kMarketMakerMode = 24;        // 1 alpha
inline constexpr std::size_t kMarketParticipantState = 25; // 1 alpha
} // namespace mpart

// 'V' Market-Wide Circuit Breaker Decline Level (35)
//
// The three levels are Price(8), not Price(4). Decoding them as 32-bit values
// yields the high half of each price and a field layout that is off by 12
// bytes from the second field onward.
namespace mwcb_decline {
inline constexpr std::size_t kLevel1 = 11; // 8 Price(8)
inline constexpr std::size_t kLevel2 = 19; // 8 Price(8)
inline constexpr std::size_t kLevel3 = 27; // 8 Price(8)
} // namespace mwcb_decline

// 'W' Market-Wide Circuit Breaker Status (12)
//
// The specification's table title for this message is mislabeled; the field
// layout in the table body is correct and is what is transcribed here.
namespace mwcb_status {
inline constexpr std::size_t kBreachedLevel = 11; // 1 alpha, '1'|'2'|'3'
} // namespace mwcb_status

// 'K' IPO Quoting Period Update (28)
//
// Stock Locate is documented as always 0 for this message, so the symbol must
// be read from the Stock field rather than resolved through the locate table.
//
// ReleaseTime is in SECONDS since midnight, not nanoseconds. It is the only
// time field in the protocol that is not nanoseconds, and mixing the units
// places the release 10^9 times too early or too late.
//
// IPOPrice is typed Price(4) in the field table, while the accompanying note
// text describes a legacy ASCII representation. It is decoded here as binary
// Price(4), consistent with the type column. This is the one field in the
// specification where the table and its note disagree, so the decode is
// verified against real data rather than taken from either: see
// docs/correctness.md.
namespace ipo_quote {
inline constexpr std::size_t kStock = 11;            // 8 alpha
inline constexpr std::size_t kReleaseTime = 19;      // 4 int, seconds since midnight
inline constexpr std::size_t kReleaseQualifier = 23; // 1 alpha
inline constexpr std::size_t kIPOPrice = 24;         // 4 Price(4)
} // namespace ipo_quote

// 'J' LULD Auction Collar (35)
namespace luld_collar {
inline constexpr std::size_t kStock = 11;           // 8 alpha
inline constexpr std::size_t kRefPrice = 19;        // 4 Price(4)
inline constexpr std::size_t kUpperCollar = 23;     // 4 Price(4)
inline constexpr std::size_t kLowerCollar = 27;     // 4 Price(4)
inline constexpr std::size_t kCollarExtension = 31; // 4 int
} // namespace luld_collar

// 'h' Operational Halt (21)
namespace op_halt {
inline constexpr std::size_t kStock = 11;      // 8 alpha
inline constexpr std::size_t kMarketCode = 19; // 1 alpha, 'Q' NASDAQ | 'B' BX | 'X' PSX
inline constexpr std::size_t kHaltAction = 20; // 1 alpha, 'H' halted | 'T' halt lifted
} // namespace op_halt

// 'A' Add Order, no MPID attribution (36)
namespace add {
inline constexpr std::size_t kOrderRef = 11; // 8 int, unaligned; see wire.hpp
inline constexpr std::size_t kSide = 19;     // 1 alpha, 'B' | 'S'
inline constexpr std::size_t kShares = 20;   // 4 int
inline constexpr std::size_t kStock = 24;    // 8 alpha
inline constexpr std::size_t kPrice = 32;    // 4 Price(4)
} // namespace add

// 'F' Add Order with MPID attribution (40)
//
// Identical to 'A' through offset 36, then the attribution field. The shared
// prefix is why the book can treat 'A' and 'F' with one code path.
namespace add_mpid {
inline constexpr std::size_t kOrderRef = 11;    // 8 int
inline constexpr std::size_t kSide = 19;        // 1 alpha
inline constexpr std::size_t kShares = 20;      // 4 int
inline constexpr std::size_t kStock = 24;       // 8 alpha
inline constexpr std::size_t kPrice = 32;       // 4 Price(4)
inline constexpr std::size_t kAttribution = 36; // 4 alpha
} // namespace add_mpid

// 'E' Order Executed (31)
//
// Carries no price field; the resting order's price applies. ExecutedShares is
// a decrement, not a new total.
namespace exec {
inline constexpr std::size_t kOrderRef = 11;       // 8 int
inline constexpr std::size_t kExecutedShares = 19; // 4 int
inline constexpr std::size_t kMatchNumber = 23;    // 8 int
} // namespace exec

// 'C' Order Executed With Price (36)
//
// Printable 'N' means the execution is not published to the consolidated tape.
// The book effect is identical either way.
namespace exec_price {
inline constexpr std::size_t kOrderRef = 11;       // 8 int
inline constexpr std::size_t kExecutedShares = 19; // 4 int
inline constexpr std::size_t kMatchNumber = 23;    // 8 int
inline constexpr std::size_t kPrintable = 31;      // 1 alpha, 'Y' | 'N'
inline constexpr std::size_t kExecPrice = 32;      // 4 Price(4)
} // namespace exec_price

// 'X' Order Cancel (23)
//
// A partial cancel. CancelledShares is a decrement; the order is removed only
// when its displayed quantity reaches zero, and no 'D' is guaranteed to follow.
namespace cancel {
inline constexpr std::size_t kOrderRef = 11;        // 8 int
inline constexpr std::size_t kCancelledShares = 19; // 4 int
} // namespace cancel

// 'D' Order Delete (19) -- removes the order outright
namespace del {
inline constexpr std::size_t kOrderRef = 11; // 8 int
} // namespace del

// 'U' Order Replace (35)
//
// Carries no side, no stock and no attribution; all three are retained from the
// original Add. The new reference supersedes the old one for every later
// update, and the order moves to the back of the queue at the new price even
// when the price is unchanged. Shares is a new total, unlike 'E', 'C' and 'X'.
namespace replace {
inline constexpr std::size_t kOldOrderRef = 11; // 8 int
inline constexpr std::size_t kNewOrderRef = 19; // 8 int
inline constexpr std::size_t kShares = 27;      // 4 int, new total quantity
inline constexpr std::size_t kPrice = 31;       // 4 Price(4)
} // namespace replace

// 'P' Trade, non-cross (44)
//
// No book effect; reports non-displayable liquidity after the fact. OrderRef
// has been zero since December 2010, and Side has been hardcoded 'B'
// regardless of the resting side since 14 July 2014, so neither field carries
// information and trade sign cannot be read from Side.
namespace trade {
inline constexpr std::size_t kOrderRef = 11;    // 8 int, always 0
inline constexpr std::size_t kSide = 19;        // 1 alpha, always 'B'
inline constexpr std::size_t kShares = 20;      // 4 int
inline constexpr std::size_t kStock = 24;       // 8 alpha
inline constexpr std::size_t kPrice = 32;       // 4 Price(4)
inline constexpr std::size_t kMatchNumber = 36; // 8 int
} // namespace trade

// 'Q' Cross Trade (40)
//
// No book effect. Zero shares is a valid report, sent when order interest was
// insufficient for the cross.
//
// The specification's table title for this message is mislabeled; the field
// layout in the table body is correct and is what is transcribed here.
namespace cross {
inline constexpr std::size_t kShares = 11;      // 8 int
inline constexpr std::size_t kStock = 19;       // 8 alpha
inline constexpr std::size_t kCrossPrice = 27;  // 4 Price(4)
inline constexpr std::size_t kMatchNumber = 31; // 8 int
inline constexpr std::size_t kCrossType = 39;   // 1 alpha, 'O'|'C'|'H'|'I'
} // namespace cross

// 'B' Broken Trade (19)
//
// No book effect; affects time and sales only. Can arrive after the 'E'
// end-of-system-hours event.
namespace broken {
inline constexpr std::size_t kMatchNumber = 11; // 8 int
} // namespace broken

// 'I' Net Order Imbalance Indicator (50)
//
// ImbalanceDirection gained the value 'P' (paused) in 2023, and CrossType
// gained 'A' (extended trading close) in 2022. Neither occurs in the 2017-2020
// sessions this project uses, so both are decode paths without coverage from
// that data; see docs/data.md.
namespace noii {
inline constexpr std::size_t kPairedShares = 11;       // 8 int
inline constexpr std::size_t kImbalanceShares = 19;    // 8 int
inline constexpr std::size_t kImbalanceDirection = 27; // 1 alpha, 'B'|'S'|'N'|'O'|'P'
inline constexpr std::size_t kStock = 28;              // 8 alpha
inline constexpr std::size_t kFarPrice = 36;           // 4 Price(4)
inline constexpr std::size_t kNearPrice = 40;          // 4 Price(4)
inline constexpr std::size_t kRefPrice = 44;           // 4 Price(4)
inline constexpr std::size_t kCrossType = 48;          // 1 alpha, 'O'|'C'|'H'|'A'
inline constexpr std::size_t kPriceVariation = 49;     // 1 alpha
} // namespace noii

// 'N' Retail Price Improvement Indicator (20)
//
// This message is commonly described as obsolete on the grounds that NASDAQ's
// Retail Price Improvement program ended on 31 December 2014. That is not what
// the data shows. 20190130.BX_ITCH_50 carries 8,301,264 of them -- 10.0% of the
// session -- across 7,217 symbols, spanning 08:00 to 19:00, using all four
// documented InterestFlag values ('A' 530,735, 'B' 2,121,573, 'N' 3,620,049,
// 'S' 2,028,907), and concentrated in liquid names. RITCH independently counts
// the same total, so this is the feed's content and not a decode artifact.
//
// BX operates its own retail program. Whether the NASDAQ venue's sessions also
// carry this message is untested here; see docs/correctness.md.
namespace rpii {
inline constexpr std::size_t kStock = 11;        // 8 alpha
inline constexpr std::size_t kInterestFlag = 19; // 1 alpha, 'B'|'S'|'A'|'N'
} // namespace rpii

// 'O' Direct Listing With Capital Raise Price Discovery (48)
//
// Introduced in specification revision 2023-04-28 and absent from every
// 2017-2020 session, so this layout has no coverage from the data this project
// uses; see docs/data.md.
//
// NearExecTime is 8 bytes of integer. The specification does not state its
// units, so no interpretation is applied here beyond decoding the integer.
namespace direct_listing {
inline constexpr std::size_t kStock = 11;             // 8 alpha
inline constexpr std::size_t kOpenEligibility = 19;   // 1 alpha
inline constexpr std::size_t kMinAllowablePrice = 20; // 4 Price(4)
inline constexpr std::size_t kMaxAllowablePrice = 24; // 4 Price(4)
inline constexpr std::size_t kNearExecPrice = 28;     // 4 Price(4)
inline constexpr std::size_t kNearExecTime = 32;      // 8 int, units not stated in the spec
inline constexpr std::size_t kLowerCollar = 40;       // 4 Price(4)
inline constexpr std::size_t kUpperCollar = 44;       // 4 Price(4)
} // namespace direct_listing

} // namespace off

inline constexpr std::uint32_t kMaxPrice4 = 0x77359400u; // 200000.0000

// Does this message type modify the displayed book?
inline constexpr bool touches_book(unsigned char t) noexcept {
    return t == 'A' || t == 'F' || t == 'E' || t == 'C' || t == 'X' || t == 'D' || t == 'U';
}

// ---------------------------------------------------------------------------
// Compile-time tiling audit
//
// The offsets above are transcribed by hand. A single wrong integer produces
// field values that are wrong but well formed, and a byte-level fixture only
// catches the fields it happens to assert -- a gap between two individually
// correct fields is invisible to it.
//
// Each layout below lists every field of a message body in wire order, pairing
// the named offset constant with that field's length. tiles() then asserts
// that the fields cover their extent exactly: each begins where the previous
// one ended, and the last ends at the message length. Lengths appear only
// here and offsets only above, so the two transcriptions check each other.
//
// See docs/design.md record 003 for what this does not catch.
// ---------------------------------------------------------------------------

struct Field {
    std::size_t off, len;
};

// True only if the fields, in the order given, cover [start, end) with no gap
// and no overlap.
template<std::size_t N>
constexpr bool tiles(const Field (&f)[N], std::size_t start, std::size_t end) {
    std::size_t cursor = start;
    for (std::size_t i = 0; i < N; ++i) {
        if (f[i].off != cursor) return false;
        cursor += f[i].len;
    }
    return cursor == end;
}

namespace layout {

inline constexpr Field kHeader[] = {
    {off::kType, 1},
    {off::kStockLocate, 2},
    {off::kTracking, 2},
    {off::kTimestamp, 6},
};

inline constexpr Field kSystemEvent[] = {
    {off::sysevent::kEventCode, 1},
};

inline constexpr Field kStockDirectory[] = {
    {off::stockdir::kStock, 8},
    {off::stockdir::kMarketCategory, 1},
    {off::stockdir::kFinancialStatus, 1},
    {off::stockdir::kRoundLotSize, 4},
    {off::stockdir::kRoundLotsOnly, 1},
    {off::stockdir::kIssueClassification, 1},
    {off::stockdir::kIssueSubType, 2},
    {off::stockdir::kAuthenticity, 1},
    {off::stockdir::kShortSaleThreshold, 1},
    {off::stockdir::kIPOFlag, 1},
    {off::stockdir::kLULDRefPriceTier, 1},
    {off::stockdir::kETPFlag, 1},
    {off::stockdir::kETPLeverageFactor, 4},
    {off::stockdir::kInverseIndicator, 1},
};

inline constexpr Field kStockTradingAction[] = {
    {off::tradingaction::kStock, 8},
    {off::tradingaction::kTradingState, 1},
    {off::tradingaction::kReserved, 1},
    {off::tradingaction::kReason, 4},
};

inline constexpr Field kRegSHO[] = {
    {off::regsho::kStock, 8},
    {off::regsho::kRegSHOAction, 1},
};

inline constexpr Field kMarketParticipant[] = {
    {off::mpart::kMPID, 4},
    {off::mpart::kStock, 8},
    {off::mpart::kPrimaryMarketMaker, 1},
    {off::mpart::kMarketMakerMode, 1},
    {off::mpart::kMarketParticipantState, 1},
};

inline constexpr Field kMwcbDeclineLevel[] = {
    {off::mwcb_decline::kLevel1, 8},
    {off::mwcb_decline::kLevel2, 8},
    {off::mwcb_decline::kLevel3, 8},
};

inline constexpr Field kMwcbStatus[] = {
    {off::mwcb_status::kBreachedLevel, 1},
};

inline constexpr Field kIpoQuotingPeriod[] = {
    {off::ipo_quote::kStock, 8},
    {off::ipo_quote::kReleaseTime, 4},
    {off::ipo_quote::kReleaseQualifier, 1},
    {off::ipo_quote::kIPOPrice, 4},
};

inline constexpr Field kLuldAuctionCollar[] = {
    {off::luld_collar::kStock, 8},           {off::luld_collar::kRefPrice, 4},
    {off::luld_collar::kUpperCollar, 4},     {off::luld_collar::kLowerCollar, 4},
    {off::luld_collar::kCollarExtension, 4},
};

inline constexpr Field kOperationalHalt[] = {
    {off::op_halt::kStock, 8},
    {off::op_halt::kMarketCode, 1},
    {off::op_halt::kHaltAction, 1},
};

inline constexpr Field kAddOrder[] = {
    {off::add::kOrderRef, 8}, {off::add::kSide, 1},  {off::add::kShares, 4},
    {off::add::kStock, 8},    {off::add::kPrice, 4},
};

inline constexpr Field kAddOrderMpid[] = {
    {off::add_mpid::kOrderRef, 8}, {off::add_mpid::kSide, 1},  {off::add_mpid::kShares, 4},
    {off::add_mpid::kStock, 8},    {off::add_mpid::kPrice, 4}, {off::add_mpid::kAttribution, 4},
};

inline constexpr Field kOrderExecuted[] = {
    {off::exec::kOrderRef, 8},
    {off::exec::kExecutedShares, 4},
    {off::exec::kMatchNumber, 8},
};

inline constexpr Field kOrderExecutedPrice[] = {
    {off::exec_price::kOrderRef, 8},    {off::exec_price::kExecutedShares, 4},
    {off::exec_price::kMatchNumber, 8}, {off::exec_price::kPrintable, 1},
    {off::exec_price::kExecPrice, 4},
};

inline constexpr Field kOrderCancel[] = {
    {off::cancel::kOrderRef, 8},
    {off::cancel::kCancelledShares, 4},
};

inline constexpr Field kOrderDelete[] = {
    {off::del::kOrderRef, 8},
};

inline constexpr Field kOrderReplace[] = {
    {off::replace::kOldOrderRef, 8},
    {off::replace::kNewOrderRef, 8},
    {off::replace::kShares, 4},
    {off::replace::kPrice, 4},
};

inline constexpr Field kTrade[] = {
    {off::trade::kOrderRef, 8}, {off::trade::kSide, 1},  {off::trade::kShares, 4},
    {off::trade::kStock, 8},    {off::trade::kPrice, 4}, {off::trade::kMatchNumber, 8},
};

inline constexpr Field kCrossTrade[] = {
    {off::cross::kShares, 8},      {off::cross::kStock, 8},     {off::cross::kCrossPrice, 4},
    {off::cross::kMatchNumber, 8}, {off::cross::kCrossType, 1},
};

inline constexpr Field kBrokenTrade[] = {
    {off::broken::kMatchNumber, 8},
};

inline constexpr Field kNoii[] = {
    {off::noii::kPairedShares, 8},       {off::noii::kImbalanceShares, 8},
    {off::noii::kImbalanceDirection, 1}, {off::noii::kStock, 8},
    {off::noii::kFarPrice, 4},           {off::noii::kNearPrice, 4},
    {off::noii::kRefPrice, 4},           {off::noii::kCrossType, 1},
    {off::noii::kPriceVariation, 1},
};

inline constexpr Field kRpii[] = {
    {off::rpii::kStock, 8},
    {off::rpii::kInterestFlag, 1},
};

inline constexpr Field kDirectListingCapRaise[] = {
    {off::direct_listing::kStock, 8},
    {off::direct_listing::kOpenEligibility, 1},
    {off::direct_listing::kMinAllowablePrice, 4},
    {off::direct_listing::kMaxAllowablePrice, 4},
    {off::direct_listing::kNearExecPrice, 4},
    {off::direct_listing::kNearExecTime, 8},
    {off::direct_listing::kLowerCollar, 4},
    {off::direct_listing::kUpperCollar, 4},
};

} // namespace layout

static_assert(tiles(layout::kHeader, 0, kHeaderLen), "common header does not tile [0, 11)");

#define CARTERET_ASSERT_BODY_TILES(type_char, table)                                           \
    static_assert(tiles(layout::table, kHeaderLen, kMsgLen[type_char]),                        \
                  "message body '" #type_char "' does not tile [11, kMsgLen)")

CARTERET_ASSERT_BODY_TILES('S', kSystemEvent);
CARTERET_ASSERT_BODY_TILES('R', kStockDirectory);
CARTERET_ASSERT_BODY_TILES('H', kStockTradingAction);
CARTERET_ASSERT_BODY_TILES('Y', kRegSHO);
CARTERET_ASSERT_BODY_TILES('L', kMarketParticipant);
CARTERET_ASSERT_BODY_TILES('V', kMwcbDeclineLevel);
CARTERET_ASSERT_BODY_TILES('W', kMwcbStatus);
CARTERET_ASSERT_BODY_TILES('K', kIpoQuotingPeriod);
CARTERET_ASSERT_BODY_TILES('J', kLuldAuctionCollar);
CARTERET_ASSERT_BODY_TILES('h', kOperationalHalt);
CARTERET_ASSERT_BODY_TILES('A', kAddOrder);
CARTERET_ASSERT_BODY_TILES('F', kAddOrderMpid);
CARTERET_ASSERT_BODY_TILES('E', kOrderExecuted);
CARTERET_ASSERT_BODY_TILES('C', kOrderExecutedPrice);
CARTERET_ASSERT_BODY_TILES('X', kOrderCancel);
CARTERET_ASSERT_BODY_TILES('D', kOrderDelete);
CARTERET_ASSERT_BODY_TILES('U', kOrderReplace);
CARTERET_ASSERT_BODY_TILES('P', kTrade);
CARTERET_ASSERT_BODY_TILES('Q', kCrossTrade);
CARTERET_ASSERT_BODY_TILES('B', kBrokenTrade);
CARTERET_ASSERT_BODY_TILES('I', kNoii);
CARTERET_ASSERT_BODY_TILES('N', kRpii);
CARTERET_ASSERT_BODY_TILES('O', kDirectListingCapRaise);

#undef CARTERET_ASSERT_BODY_TILES

// tiles() must reject a gap, an overlap and a short table, or the assertions
// above prove nothing.
namespace {
inline constexpr Field kTilesSelfTestGood[] = {{11, 4}, {15, 2}};
inline constexpr Field kTilesSelfTestGap[] = {{11, 4}, {16, 1}};
inline constexpr Field kTilesSelfTestOverlap[] = {{11, 4}, {14, 3}};
static_assert(tiles(kTilesSelfTestGood, 11, 17));
static_assert(!tiles(kTilesSelfTestGap, 11, 17));
static_assert(!tiles(kTilesSelfTestOverlap, 11, 17));
static_assert(!tiles(kTilesSelfTestGood, 11, 18)); // short of the message length
} // namespace

} // namespace carteret
