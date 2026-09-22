// carteret/messages.hpp -- zero-copy typed views over a framed ITCH 5.0 message.
//
// One struct per message type. Each holds a single pointer into the mapped
// buffer and decodes a field only when the accessor is called, so a handler
// that reads two fields of an Add Order pays for two fields.
//
// Every accessor goes through the memcpy-based helpers in wire.hpp; nothing
// here casts into the buffer. Offsets come from spec.hpp, whose layouts are
// checked by the tiling audit at compile time.
//
// Lifetime: a view is valid only while the buffer it points into lives. Views
// are passed by value to handlers and never stored.

#pragma once

#include "spec.hpp"
#include "wire.hpp"

#include <cstdint>
#include <string_view>
#include <type_traits>

namespace carteret {

// Common header fields, inherited by every typed view.
struct Header {
    const unsigned char* p = nullptr;

    [[nodiscard]] unsigned char type() const noexcept { return p[off::kType]; }
    [[nodiscard]] std::uint16_t locate() const noexcept { return be16(p + off::kStockLocate); }
    [[nodiscard]] std::uint64_t ts() const noexcept { return timestamp(p); }
    [[nodiscard]] std::uint16_t track() const noexcept { return tracking(p); }
};

// 'S' System Event
struct SystemEvent : Header {
    // 'O' start of messages, 'S' start of system hours, 'Q' start of market
    // hours, 'M' end of market hours, 'E' end of system hours, 'C' end of
    // messages. 'B' and 'D' can still arrive after 'E'.
    [[nodiscard]] unsigned char event_code() const noexcept { return p[off::sysevent::kEventCode]; }
};

// 'R' Stock Directory
struct StockDirectory : Header {
    [[nodiscard]] std::string_view stock() const noexcept {
        return alpha(p + off::stockdir::kStock, 8);
    }
    [[nodiscard]] unsigned char market_category() const noexcept {
        return p[off::stockdir::kMarketCategory];
    }
    [[nodiscard]] unsigned char financial_status() const noexcept {
        return p[off::stockdir::kFinancialStatus];
    }
    [[nodiscard]] std::uint32_t round_lot_size() const noexcept {
        return be32(p + off::stockdir::kRoundLotSize);
    }
    [[nodiscard]] unsigned char round_lots_only() const noexcept {
        return p[off::stockdir::kRoundLotsOnly];
    }
    [[nodiscard]] unsigned char issue_classification() const noexcept {
        return p[off::stockdir::kIssueClassification];
    }
    [[nodiscard]] std::string_view issue_sub_type() const noexcept {
        return alpha(p + off::stockdir::kIssueSubType, 2);
    }
    [[nodiscard]] unsigned char authenticity() const noexcept {
        return p[off::stockdir::kAuthenticity];
    }
    [[nodiscard]] unsigned char short_sale_threshold() const noexcept {
        return p[off::stockdir::kShortSaleThreshold];
    }
    [[nodiscard]] unsigned char ipo_flag() const noexcept { return p[off::stockdir::kIPOFlag]; }
    [[nodiscard]] unsigned char luld_ref_price_tier() const noexcept {
        return p[off::stockdir::kLULDRefPriceTier];
    }
    [[nodiscard]] unsigned char etp_flag() const noexcept { return p[off::stockdir::kETPFlag]; }
    [[nodiscard]] std::uint32_t etp_leverage_factor() const noexcept {
        return be32(p + off::stockdir::kETPLeverageFactor);
    }
    [[nodiscard]] unsigned char inverse_indicator() const noexcept {
        return p[off::stockdir::kInverseIndicator];
    }
};

// 'H' Stock Trading Action
struct StockTradingAction : Header {
    [[nodiscard]] std::string_view stock() const noexcept {
        return alpha(p + off::tradingaction::kStock, 8);
    }
    [[nodiscard]] unsigned char trading_state() const noexcept {
        return p[off::tradingaction::kTradingState];
    }
    [[nodiscard]] unsigned char reserved() const noexcept {
        return p[off::tradingaction::kReserved];
    }
    [[nodiscard]] std::string_view reason() const noexcept {
        return alpha(p + off::tradingaction::kReason, 4);
    }
};

// 'Y' Reg SHO Short Sale Price Test Restricted Indicator
struct RegSHO : Header {
    [[nodiscard]] std::string_view stock() const noexcept {
        return alpha(p + off::regsho::kStock, 8);
    }
    [[nodiscard]] unsigned char action() const noexcept { return p[off::regsho::kRegSHOAction]; }
};

// 'L' Market Participant Position
struct MarketParticipant : Header {
    [[nodiscard]] std::string_view mpid() const noexcept { return alpha(p + off::mpart::kMPID, 4); }
    [[nodiscard]] std::string_view stock() const noexcept {
        return alpha(p + off::mpart::kStock, 8);
    }
    [[nodiscard]] unsigned char primary_market_maker() const noexcept {
        return p[off::mpart::kPrimaryMarketMaker];
    }
    [[nodiscard]] unsigned char market_maker_mode() const noexcept {
        return p[off::mpart::kMarketMakerMode];
    }
    [[nodiscard]] unsigned char participant_state() const noexcept {
        return p[off::mpart::kMarketParticipantState];
    }
};

// 'V' Market-Wide Circuit Breaker Decline Level. All three are Price(8).
struct MwcbDeclineLevel : Header {
    [[nodiscard]] std::uint64_t level1() const noexcept {
        return be64(p + off::mwcb_decline::kLevel1);
    }
    [[nodiscard]] std::uint64_t level2() const noexcept {
        return be64(p + off::mwcb_decline::kLevel2);
    }
    [[nodiscard]] std::uint64_t level3() const noexcept {
        return be64(p + off::mwcb_decline::kLevel3);
    }
};

// 'W' Market-Wide Circuit Breaker Status
struct MwcbStatus : Header {
    [[nodiscard]] unsigned char breached_level() const noexcept {
        return p[off::mwcb_status::kBreachedLevel];
    }
};

// 'K' IPO Quoting Period Update. Stock Locate is documented as always 0 here,
// so the symbol comes from the Stock field. release_time() is SECONDS since
// midnight, unlike every other time field in the protocol.
struct IpoQuotingPeriod : Header {
    [[nodiscard]] std::string_view stock() const noexcept {
        return alpha(p + off::ipo_quote::kStock, 8);
    }
    [[nodiscard]] std::uint32_t release_time() const noexcept {
        return be32(p + off::ipo_quote::kReleaseTime);
    }
    [[nodiscard]] unsigned char release_qualifier() const noexcept {
        return p[off::ipo_quote::kReleaseQualifier];
    }
    [[nodiscard]] std::uint32_t ipo_price() const noexcept {
        return be32(p + off::ipo_quote::kIPOPrice);
    }
};

// 'J' LULD Auction Collar
struct LuldAuctionCollar : Header {
    [[nodiscard]] std::string_view stock() const noexcept {
        return alpha(p + off::luld_collar::kStock, 8);
    }
    [[nodiscard]] std::uint32_t ref_price() const noexcept {
        return be32(p + off::luld_collar::kRefPrice);
    }
    [[nodiscard]] std::uint32_t upper_collar() const noexcept {
        return be32(p + off::luld_collar::kUpperCollar);
    }
    [[nodiscard]] std::uint32_t lower_collar() const noexcept {
        return be32(p + off::luld_collar::kLowerCollar);
    }
    [[nodiscard]] std::uint32_t collar_extension() const noexcept {
        return be32(p + off::luld_collar::kCollarExtension);
    }
};

// 'h' Operational Halt
struct OperationalHalt : Header {
    [[nodiscard]] std::string_view stock() const noexcept {
        return alpha(p + off::op_halt::kStock, 8);
    }
    [[nodiscard]] unsigned char market_code() const noexcept { return p[off::op_halt::kMarketCode]; }
    [[nodiscard]] unsigned char halt_action() const noexcept { return p[off::op_halt::kHaltAction]; }
};

// 'A' Add Order, no MPID attribution
struct AddOrder : Header {
    [[nodiscard]] std::uint64_t order_ref() const noexcept { return be64(p + off::add::kOrderRef); }
    [[nodiscard]] unsigned char side() const noexcept { return p[off::add::kSide]; }
    [[nodiscard]] std::uint32_t shares() const noexcept { return be32(p + off::add::kShares); }
    [[nodiscard]] std::string_view stock() const noexcept { return alpha(p + off::add::kStock, 8); }
    [[nodiscard]] std::uint32_t price() const noexcept { return be32(p + off::add::kPrice); }
};

// 'F' Add Order with MPID attribution. Identical to 'A' through offset 36,
// which is why the book can treat the two with one code path.
struct AddOrderMpid : Header {
    [[nodiscard]] std::uint64_t order_ref() const noexcept {
        return be64(p + off::add_mpid::kOrderRef);
    }
    [[nodiscard]] unsigned char side() const noexcept { return p[off::add_mpid::kSide]; }
    [[nodiscard]] std::uint32_t shares() const noexcept { return be32(p + off::add_mpid::kShares); }
    [[nodiscard]] std::string_view stock() const noexcept {
        return alpha(p + off::add_mpid::kStock, 8);
    }
    [[nodiscard]] std::uint32_t price() const noexcept { return be32(p + off::add_mpid::kPrice); }
    [[nodiscard]] std::string_view attribution() const noexcept {
        return alpha(p + off::add_mpid::kAttribution, 4);
    }
};

// 'E' Order Executed. No price field; the resting order's price applies.
// executed_shares() is a decrement, not a new total.
struct OrderExecuted : Header {
    [[nodiscard]] std::uint64_t order_ref() const noexcept { return be64(p + off::exec::kOrderRef); }
    [[nodiscard]] std::uint32_t executed_shares() const noexcept {
        return be32(p + off::exec::kExecutedShares);
    }
    [[nodiscard]] std::uint64_t match_number() const noexcept {
        return be64(p + off::exec::kMatchNumber);
    }
};

// 'C' Order Executed With Price. printable() == 'N' means the execution is not
// published to the consolidated tape; the book effect is identical either way.
struct OrderExecutedPrice : Header {
    [[nodiscard]] std::uint64_t order_ref() const noexcept {
        return be64(p + off::exec_price::kOrderRef);
    }
    [[nodiscard]] std::uint32_t executed_shares() const noexcept {
        return be32(p + off::exec_price::kExecutedShares);
    }
    [[nodiscard]] std::uint64_t match_number() const noexcept {
        return be64(p + off::exec_price::kMatchNumber);
    }
    [[nodiscard]] unsigned char printable() const noexcept { return p[off::exec_price::kPrintable]; }
    [[nodiscard]] std::uint32_t exec_price() const noexcept {
        return be32(p + off::exec_price::kExecPrice);
    }
};

// 'X' Order Cancel. A partial cancel: cancelled_shares() is a decrement, and
// the order is removed only when its displayed quantity reaches zero.
struct OrderCancel : Header {
    [[nodiscard]] std::uint64_t order_ref() const noexcept {
        return be64(p + off::cancel::kOrderRef);
    }
    [[nodiscard]] std::uint32_t cancelled_shares() const noexcept {
        return be32(p + off::cancel::kCancelledShares);
    }
};

// 'D' Order Delete
struct OrderDelete : Header {
    [[nodiscard]] std::uint64_t order_ref() const noexcept { return be64(p + off::del::kOrderRef); }
};

// 'U' Order Replace. Carries no side, stock or attribution; all three are
// retained from the original Add. shares() is a new total, unlike 'E', 'C'
// and 'X'.
struct OrderReplace : Header {
    [[nodiscard]] std::uint64_t old_order_ref() const noexcept {
        return be64(p + off::replace::kOldOrderRef);
    }
    [[nodiscard]] std::uint64_t new_order_ref() const noexcept {
        return be64(p + off::replace::kNewOrderRef);
    }
    [[nodiscard]] std::uint32_t shares() const noexcept { return be32(p + off::replace::kShares); }
    [[nodiscard]] std::uint32_t price() const noexcept { return be32(p + off::replace::kPrice); }
};

// 'P' Trade, non-cross. No book effect. order_ref() has been zero since
// December 2010 and side() hardcoded 'B' since 14 July 2014; neither carries
// information, and trade sign cannot be read from side().
struct Trade : Header {
    [[nodiscard]] std::uint64_t order_ref() const noexcept {
        return be64(p + off::trade::kOrderRef);
    }
    [[nodiscard]] unsigned char side() const noexcept { return p[off::trade::kSide]; }
    [[nodiscard]] std::uint32_t shares() const noexcept { return be32(p + off::trade::kShares); }
    [[nodiscard]] std::string_view stock() const noexcept {
        return alpha(p + off::trade::kStock, 8);
    }
    [[nodiscard]] std::uint32_t price() const noexcept { return be32(p + off::trade::kPrice); }
    [[nodiscard]] std::uint64_t match_number() const noexcept {
        return be64(p + off::trade::kMatchNumber);
    }
};

// 'Q' Cross Trade. No book effect. Zero shares is a valid report.
struct CrossTrade : Header {
    [[nodiscard]] std::uint64_t shares() const noexcept { return be64(p + off::cross::kShares); }
    [[nodiscard]] std::string_view stock() const noexcept {
        return alpha(p + off::cross::kStock, 8);
    }
    [[nodiscard]] std::uint32_t cross_price() const noexcept {
        return be32(p + off::cross::kCrossPrice);
    }
    [[nodiscard]] std::uint64_t match_number() const noexcept {
        return be64(p + off::cross::kMatchNumber);
    }
    [[nodiscard]] unsigned char cross_type() const noexcept { return p[off::cross::kCrossType]; }
};

// 'B' Broken Trade. No book effect; can arrive after end of system hours.
struct BrokenTrade : Header {
    [[nodiscard]] std::uint64_t match_number() const noexcept {
        return be64(p + off::broken::kMatchNumber);
    }
};

// 'I' Net Order Imbalance Indicator
struct Noii : Header {
    [[nodiscard]] std::uint64_t paired_shares() const noexcept {
        return be64(p + off::noii::kPairedShares);
    }
    [[nodiscard]] std::uint64_t imbalance_shares() const noexcept {
        return be64(p + off::noii::kImbalanceShares);
    }
    [[nodiscard]] unsigned char imbalance_direction() const noexcept {
        return p[off::noii::kImbalanceDirection];
    }
    [[nodiscard]] std::string_view stock() const noexcept { return alpha(p + off::noii::kStock, 8); }
    [[nodiscard]] std::uint32_t far_price() const noexcept { return be32(p + off::noii::kFarPrice); }
    [[nodiscard]] std::uint32_t near_price() const noexcept {
        return be32(p + off::noii::kNearPrice);
    }
    [[nodiscard]] std::uint32_t ref_price() const noexcept { return be32(p + off::noii::kRefPrice); }
    [[nodiscard]] unsigned char cross_type() const noexcept { return p[off::noii::kCrossType]; }
    [[nodiscard]] unsigned char price_variation() const noexcept {
        return p[off::noii::kPriceVariation];
    }
};

// 'N' Retail Price Improvement Indicator. The program ended 2014-12-31, so
// near-zero counts are expected in the sample sessions.
struct Rpii : Header {
    [[nodiscard]] std::string_view stock() const noexcept { return alpha(p + off::rpii::kStock, 8); }
    [[nodiscard]] unsigned char interest_flag() const noexcept {
        return p[off::rpii::kInterestFlag];
    }
};

// 'O' Direct Listing With Capital Raise Price Discovery. Introduced in
// specification revision 2023-04-28; absent from every session in
// docs/data.md. The units of near_exec_time() are not stated in the
// specification, so the field is decoded and not interpreted.
struct DirectListingCapRaise : Header {
    [[nodiscard]] std::string_view stock() const noexcept {
        return alpha(p + off::direct_listing::kStock, 8);
    }
    [[nodiscard]] unsigned char open_eligibility() const noexcept {
        return p[off::direct_listing::kOpenEligibility];
    }
    [[nodiscard]] std::uint32_t min_allowable_price() const noexcept {
        return be32(p + off::direct_listing::kMinAllowablePrice);
    }
    [[nodiscard]] std::uint32_t max_allowable_price() const noexcept {
        return be32(p + off::direct_listing::kMaxAllowablePrice);
    }
    [[nodiscard]] std::uint32_t near_exec_price() const noexcept {
        return be32(p + off::direct_listing::kNearExecPrice);
    }
    [[nodiscard]] std::uint64_t near_exec_time() const noexcept {
        return be64(p + off::direct_listing::kNearExecTime);
    }
    [[nodiscard]] std::uint32_t lower_collar() const noexcept {
        return be32(p + off::direct_listing::kLowerCollar);
    }
    [[nodiscard]] std::uint32_t upper_collar() const noexcept {
        return be32(p + off::direct_listing::kUpperCollar);
    }
};

// Views are non-owning and must stay trivially copyable so that passing one to
// a handler costs a register.
static_assert(sizeof(AddOrder) == sizeof(const unsigned char*));
static_assert(std::is_trivially_copyable_v<AddOrder>);
static_assert(std::is_trivially_copyable_v<Noii>);

} // namespace carteret
