// fuzz_frame -- libFuzzer target over framing and typed dispatch.
//
// Correctness layer 1. The property under test is that no input, however
// malformed, causes a read outside the buffer, a non-terminating loop, or a
// crash. Every malformed input must resolve to one of the counted FrameStatus
// outcomes.
//
// The handler reads EVERY field of every message it is given and folds the
// results into a checksum. Dispatching without decoding would fuzz the framing
// loop alone and leave the field offsets -- the part transcribed by hand --
// untested. The checksum exists so the optimiser cannot delete the decodes;
// its value is never inspected.
//
// The input's first byte selects the framing form, so a single corpus reaches
// both readers. The remainder is the buffer under test.
//
// libFuzzer ships with upstream Clang and not with Apple Clang, so this target
// builds under the fuzz preset with a compiler override on macOS. Build and
// run instructions are in tools/fuzz.sh; campaign records are in
// docs/correctness.md.

#include "carteret/messages.hpp"
#include "carteret/parser.hpp"
#include "carteret/wire.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

using namespace carteret;

namespace {

// Reads every field of every type. The accumulator is write-only by design.
struct DecodeEverything {
    std::uint64_t acc = 0;

    void mix(std::uint64_t v) noexcept { acc = acc * 1099511628211ULL ^ v; }
    void mix(std::string_view s) noexcept {
        mix(s.size());
        for (char c : s) mix(static_cast<std::uint64_t>(static_cast<unsigned char>(c)));
    }
    template<class V>
    void header(V v) noexcept {
        mix(v.type());
        mix(v.locate());
        mix(v.ts());
        mix(v.track());
    }

    void on(SystemEvent v) noexcept {
        header(v);
        mix(v.event_code());
    }
    void on(StockDirectory v) noexcept {
        header(v);
        mix(v.stock());
        mix(v.market_category());
        mix(v.financial_status());
        mix(v.round_lot_size());
        mix(v.round_lots_only());
        mix(v.issue_classification());
        mix(v.issue_sub_type());
        mix(v.authenticity());
        mix(v.short_sale_threshold());
        mix(v.ipo_flag());
        mix(v.luld_ref_price_tier());
        mix(v.etp_flag());
        mix(v.etp_leverage_factor());
        mix(v.inverse_indicator());
    }
    void on(StockTradingAction v) noexcept {
        header(v);
        mix(v.stock());
        mix(v.trading_state());
        mix(v.reserved());
        mix(v.reason());
    }
    void on(RegSHO v) noexcept {
        header(v);
        mix(v.stock());
        mix(v.action());
    }
    void on(MarketParticipant v) noexcept {
        header(v);
        mix(v.mpid());
        mix(v.stock());
        mix(v.primary_market_maker());
        mix(v.market_maker_mode());
        mix(v.participant_state());
    }
    void on(MwcbDeclineLevel v) noexcept {
        header(v);
        mix(v.level1());
        mix(v.level2());
        mix(v.level3());
    }
    void on(MwcbStatus v) noexcept {
        header(v);
        mix(v.breached_level());
    }
    void on(IpoQuotingPeriod v) noexcept {
        header(v);
        mix(v.stock());
        mix(v.release_time());
        mix(v.release_qualifier());
        mix(v.ipo_price());
    }
    void on(LuldAuctionCollar v) noexcept {
        header(v);
        mix(v.stock());
        mix(v.ref_price());
        mix(v.upper_collar());
        mix(v.lower_collar());
        mix(v.collar_extension());
    }
    void on(OperationalHalt v) noexcept {
        header(v);
        mix(v.stock());
        mix(v.market_code());
        mix(v.halt_action());
    }
    void on(AddOrder v) noexcept {
        header(v);
        mix(v.order_ref());
        mix(v.side());
        mix(v.shares());
        mix(v.stock());
        mix(v.price());
    }
    void on(AddOrderMpid v) noexcept {
        header(v);
        mix(v.order_ref());
        mix(v.side());
        mix(v.shares());
        mix(v.stock());
        mix(v.price());
        mix(v.attribution());
    }
    void on(OrderExecuted v) noexcept {
        header(v);
        mix(v.order_ref());
        mix(v.executed_shares());
        mix(v.match_number());
    }
    void on(OrderExecutedPrice v) noexcept {
        header(v);
        mix(v.order_ref());
        mix(v.executed_shares());
        mix(v.match_number());
        mix(v.printable());
        mix(v.exec_price());
    }
    void on(OrderCancel v) noexcept {
        header(v);
        mix(v.order_ref());
        mix(v.cancelled_shares());
    }
    void on(OrderDelete v) noexcept {
        header(v);
        mix(v.order_ref());
    }
    void on(OrderReplace v) noexcept {
        header(v);
        mix(v.old_order_ref());
        mix(v.new_order_ref());
        mix(v.shares());
        mix(v.price());
    }
    void on(Trade v) noexcept {
        header(v);
        mix(v.order_ref());
        mix(v.side());
        mix(v.shares());
        mix(v.stock());
        mix(v.price());
        mix(v.match_number());
    }
    void on(CrossTrade v) noexcept {
        header(v);
        mix(v.shares());
        mix(v.stock());
        mix(v.cross_price());
        mix(v.match_number());
        mix(v.cross_type());
    }
    void on(BrokenTrade v) noexcept {
        header(v);
        mix(v.match_number());
    }
    void on(Noii v) noexcept {
        header(v);
        mix(v.paired_shares());
        mix(v.imbalance_shares());
        mix(v.imbalance_direction());
        mix(v.stock());
        mix(v.far_price());
        mix(v.near_price());
        mix(v.ref_price());
        mix(v.cross_type());
        mix(v.price_variation());
    }
    void on(Rpii v) noexcept {
        header(v);
        mix(v.stock());
        mix(v.interest_flag());
    }
    void on(DirectListingCapRaise v) noexcept {
        header(v);
        mix(v.stock());
        mix(v.open_eligibility());
        mix(v.min_allowable_price());
        mix(v.max_allowable_price());
        mix(v.near_exec_price());
        mix(v.near_exec_time());
        mix(v.lower_collar());
        mix(v.upper_collar());
    }

    void on_unknown_type(MsgView m) noexcept { mix(m.len); }
    void on_length_mismatch(MsgView m) noexcept { mix(m.len); }
};

// Keeps the checksum from being optimised away without observing it.
volatile std::uint64_t sink = 0;

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size == 0) return 0;

    const Framing framing = (data[0] & 1u) ? Framing::ZeroPrefixed : Framing::LengthPrefixed;
    const std::span<const unsigned char> buf{reinterpret_cast<const unsigned char*>(data) + 1,
                                             size - 1};

    // The detector runs on every input as well, so that a buffer which crashes
    // it is found even when the explicit form is used for the replay.
    sink = sink ^ static_cast<std::uint64_t>(detect_framing(buf));

    DecodeEverything h;
    Parser<DecodeEverything> parser(h);
    const ParseStats st = parser.run(buf, framing);

    sink = sink ^ h.acc ^ st.dispatched ^ st.unknown ^ st.mismatch ^ st.bytes;
    return 0;
}
