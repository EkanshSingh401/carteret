// carteret/spec.hpp -- NASDAQ TotalView-ITCH 5.0 wire constants and field offsets.
//
// Source: NQTVITCHspecification.pdf, revision 2023-04-28, sections 1.1-1.8.
// Every length and offset below was audited against that revision.
//
// Conventions, from the specification's Data Types section:
//   - Integer fields are big-endian (network byte order), unsigned.
//   - Alpha fields are ASCII, left justified, space padded right.
//   - Price(4) is an integer with 4 implied decimals. Max 200000.0000 = 0x77359400.
//   - Timestamps are 6 bytes: nanoseconds since midnight.
//   - Stock Locate occupies offset 1 in every message, which lets a consumer
//     filter by symbol without decoding the body.

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
    SystemEvent           = 'S',
    StockDirectory        = 'R',
    StockTradingAction    = 'H',
    RegSHO                = 'Y',
    MarketParticipant     = 'L',
    MwcbDeclineLevel      = 'V',
    MwcbStatus            = 'W',
    IpoQuotingPeriod      = 'K',
    LuldAuctionCollar     = 'J',
    OperationalHalt       = 'h',   // lowercase; distinct from 'H' Stock Trading Action
    AddOrder              = 'A',
    AddOrderMpid          = 'F',
    OrderExecuted         = 'E',
    OrderExecutedPrice    = 'C',
    OrderCancel           = 'X',
    OrderDelete           = 'D',
    OrderReplace          = 'U',
    Trade                 = 'P',
    CrossTrade            = 'Q',
    BrokenTrade           = 'B',
    Noii                  = 'I',
    Rpii                  = 'N',
    DirectListingCapRaise = 'O',
};

// On-the-wire length of each message, INCLUDING the leading type byte and
// EXCLUDING the 2-byte BinaryFILE length prefix. 0 = unknown type.
inline constexpr std::array<std::uint8_t, 256> kMsgLen = [] {
    std::array<std::uint8_t, 256> t{};
    t['S'] = 12;  t['R'] = 39;  t['H'] = 25;  t['Y'] = 20;
    t['L'] = 26;  t['V'] = 35;  t['W'] = 12;  t['K'] = 28;
    t['J'] = 35;  t['h'] = 21;  t['A'] = 36;  t['F'] = 40;
    t['E'] = 31;  t['C'] = 36;  t['X'] = 23;  t['D'] = 19;
    t['U'] = 35;  t['P'] = 44;  t['Q'] = 40;  t['B'] = 19;
    t['I'] = 50;  t['N'] = 20;  t['O'] = 48;
    return t;
}();

inline constexpr std::size_t kMaxMsgLen = 50;   // 'I' (NOII) is the longest message

// ---------------------------------------------------------------------------
// Field offsets
// ---------------------------------------------------------------------------

namespace off {

// Present in every message:
inline constexpr std::size_t kType        = 0;   // 1
inline constexpr std::size_t kStockLocate = 1;   // 2
inline constexpr std::size_t kTracking    = 3;   // 2
inline constexpr std::size_t kTimestamp   = 5;   // 6  ns since midnight

// 'A' Add Order, no MPID (36)
namespace add {
    inline constexpr std::size_t kOrderRef = 11;  // 8  unaligned; see wire.hpp
    inline constexpr std::size_t kSide     = 19;  // 1  'B' | 'S'
    inline constexpr std::size_t kShares   = 20;  // 4
    inline constexpr std::size_t kStock    = 24;  // 8  alpha
    inline constexpr std::size_t kPrice    = 32;  // 4  Price(4)
}
// 'F' Add Order with MPID (40) -- identical layout to 'A', plus:
namespace add_mpid {
    inline constexpr std::size_t kAttribution = 36;  // 4 alpha
}
// 'E' Order Executed (31). Carries no price field; the resting order's price applies.
namespace exec {
    inline constexpr std::size_t kOrderRef       = 11;  // 8
    inline constexpr std::size_t kExecutedShares = 19;  // 4
    inline constexpr std::size_t kMatchNumber    = 23;  // 8
}
// 'C' Order Executed With Price (36)
namespace exec_price {
    inline constexpr std::size_t kOrderRef       = 11;  // 8
    inline constexpr std::size_t kExecutedShares = 19;  // 4
    inline constexpr std::size_t kMatchNumber    = 23;  // 8
    inline constexpr std::size_t kPrintable      = 31;  // 1  'Y' | 'N'
    inline constexpr std::size_t kExecPrice      = 32;  // 4  Price(4)
}
// 'X' Order Cancel (23) -- partial cancel. Deduct shares; remove only at zero.
namespace cancel {
    inline constexpr std::size_t kOrderRef        = 11;  // 8
    inline constexpr std::size_t kCancelledShares = 19;  // 4
}
// 'D' Order Delete (19) -- removes the order outright
namespace del {
    inline constexpr std::size_t kOrderRef = 11;         // 8
}
// 'U' Order Replace (35)
// Carries no side, no stock and no attribution; all three are retained from the
// original Add. The new reference supersedes the old one for every later update,
// and the order moves to the back of the queue at the new price.
namespace replace {
    inline constexpr std::size_t kOldOrderRef = 11;      // 8
    inline constexpr std::size_t kNewOrderRef = 19;      // 8
    inline constexpr std::size_t kShares      = 27;      // 4  new TOTAL quantity
    inline constexpr std::size_t kPrice       = 31;      // 4  Price(4)
}
// 'P' Trade, non-cross (44) -- no book effect. Reports non-displayable liquidity.
// OrderRef has been zero since December 2010. Side has been hardcoded 'B'
// regardless of the resting side since 14 July 2014, so trade sign cannot be
// taken from that field.
namespace trade {
    inline constexpr std::size_t kOrderRef    = 11;      // 8  (always 0)
    inline constexpr std::size_t kSide        = 19;      // 1  (always 'B')
    inline constexpr std::size_t kShares      = 20;      // 4
    inline constexpr std::size_t kStock       = 24;      // 8
    inline constexpr std::size_t kPrice       = 32;      // 4
    inline constexpr std::size_t kMatchNumber = 36;      // 8
}
// 'Q' Cross Trade (40) -- no book effect. Zero shares is a valid report.
namespace cross {
    inline constexpr std::size_t kShares      = 11;      // 8
    inline constexpr std::size_t kStock       = 19;      // 8
    inline constexpr std::size_t kCrossPrice  = 27;      // 4
    inline constexpr std::size_t kMatchNumber = 31;      // 8
    inline constexpr std::size_t kCrossType   = 39;      // 1  'O'|'C'|'H'
}
// 'B' Broken Trade (19) -- no book effect. Affects time and sales only.
namespace broken {
    inline constexpr std::size_t kMatchNumber = 11;      // 8
}
// 'S' System Event (12)
// Codes: 'O' start of messages, 'S' start of system hours, 'Q' start of market
// hours, 'M' end of market hours, 'E' end of system hours, 'C' end of messages.
// Broken Trade ('B') and Order Delete ('D') messages can still arrive after the
// 'E' end-of-system-hours event.
namespace sysevent {
    inline constexpr std::size_t kEventCode = 11;        // 1
}
// 'R' Stock Directory (39)
namespace stockdir {
    inline constexpr std::size_t kStock        = 11;     // 8
    inline constexpr std::size_t kMarketCat    = 19;     // 1
    inline constexpr std::size_t kFinStatus    = 20;     // 1
    inline constexpr std::size_t kRoundLotSize = 21;     // 4
}
// 'H' Stock Trading Action (25)
namespace tradingaction {
    inline constexpr std::size_t kStock        = 11;     // 8
    inline constexpr std::size_t kTradingState = 19;     // 1  'H'|'P'|'Q'|'T'
    inline constexpr std::size_t kReserved     = 20;     // 1
    inline constexpr std::size_t kReason       = 21;     // 4
}

}  // namespace off

inline constexpr std::uint32_t kMaxPrice4 = 0x77359400u;  // 200000.0000

// Does this message type modify the displayed book?
inline constexpr bool touches_book(unsigned char t) noexcept {
    return t == 'A' || t == 'F' || t == 'E' || t == 'C' ||
           t == 'X' || t == 'D' || t == 'U';
}

}  // namespace carteret
