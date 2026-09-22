// carteret/parser.hpp -- framing loop and typed dispatch.
//
// Parser<Handler> is templated on its handler rather than built on an abstract
// base class. Dispatch runs once per message -- on the order of 2.7e8 times for
// a NASDAQ session -- and every handler in this repository is known at compile
// time, so an indirect call the inliner cannot see through buys nothing. See
// docs/design.md record 006.
//
// A handler implements `on(T)` for the message types it cares about and
// nothing for the rest; dispatch to an absent overload compiles to nothing.
// There is no base class to inherit and no `using` declaration to forget.
//
// Unknown types and length mismatches never reach a handler's `on()`. They are
// counted by the frame reader and reported through the optional hooks below,
// which receive the raw frame rather than a typed view.

#pragma once

#include "messages.hpp"
#include "spec.hpp"
#include "wire.hpp"

#include <cstdint>
#include <span>

namespace carteret {

// How a replay ended.
enum class ParseEnd : unsigned char {
    EndOfSession, // zero-length prefix, the well-formed terminator
    Truncated,    // ran off the end of the buffer without one
};

struct ParseStats {
    std::uint64_t dispatched = 0; // messages handed to the handler
    std::uint64_t unknown = 0;    // type byte not in the ITCH 5.0 set
    std::uint64_t mismatch = 0;   // known type, prefix length disagrees
    std::size_t bytes = 0;        // consumed, including length prefixes
    ParseEnd end = ParseEnd::Truncated;
    Framing framing = Framing::LengthPrefixed; // which form the buffer used

    [[nodiscard]] bool clean() const noexcept {
        return end == ParseEnd::EndOfSession && unknown == 0 && mismatch == 0;
    }
};

namespace detail {

// Calls h.on(v) when the handler declares that overload, and compiles to
// nothing when it does not.
template <class H, class V>
[[gnu::always_inline]] inline void offer(H& h, V v) noexcept {
    if constexpr (requires { h.on(v); }) h.on(v);
}

template <class V>
[[gnu::always_inline]] inline V view(const unsigned char* p) noexcept {
    V v{};
    v.p = p;
    return v;
}

} // namespace detail

template <class Handler>
class Parser {
public:
    explicit Parser(Handler& h) noexcept : h_(h) {}

    // Replays an entire buffer. Returns once the end-of-session marker is read
    // or the buffer is exhausted, whichever comes first. The framing form is
    // detected unless the caller names one.
    ParseStats run(std::span<const unsigned char> buf) noexcept {
        return run(buf, detect_framing(buf));
    }

    ParseStats run(std::span<const unsigned char> buf, Framing framing) noexcept {
        FrameReader rd(buf, framing);
        ParseStats st;
        st.framing = framing;
        MsgView m;

        for (;;) {
            const FrameStatus fs = rd.next(m);
            if (fs == FrameStatus::EndOfSession) {
                st.end = ParseEnd::EndOfSession;
                break;
            }
            if (fs == FrameStatus::Truncated) {
                st.end = ParseEnd::Truncated;
                break;
            }
            if (fs == FrameStatus::UnknownType) {
                if constexpr (requires { h_.on_unknown_type(m); }) h_.on_unknown_type(m);
                continue;
            }
            if (fs == FrameStatus::LengthMismatch) {
                if constexpr (requires { h_.on_length_mismatch(m); }) h_.on_length_mismatch(m);
                continue;
            }
            dispatch(m.data);
            ++st.dispatched;
        }

        st.unknown = rd.unknown();
        st.mismatch = rd.mismatch();
        st.bytes = rd.offset();
        return st;
    }

private:
    // One switch over the type byte. The compiler is free to turn this into a
    // jump table; the cases are dense enough over the printable range that it
    // usually does.
    [[gnu::always_inline]] void dispatch(const unsigned char* p) noexcept {
        using namespace detail;
        switch (p[off::kType]) {
        case 'S': offer(h_, view<SystemEvent>(p)); break;
        case 'R': offer(h_, view<StockDirectory>(p)); break;
        case 'H': offer(h_, view<StockTradingAction>(p)); break;
        case 'Y': offer(h_, view<RegSHO>(p)); break;
        case 'L': offer(h_, view<MarketParticipant>(p)); break;
        case 'V': offer(h_, view<MwcbDeclineLevel>(p)); break;
        case 'W': offer(h_, view<MwcbStatus>(p)); break;
        case 'K': offer(h_, view<IpoQuotingPeriod>(p)); break;
        case 'J': offer(h_, view<LuldAuctionCollar>(p)); break;
        case 'h': offer(h_, view<OperationalHalt>(p)); break;
        case 'A': offer(h_, view<AddOrder>(p)); break;
        case 'F': offer(h_, view<AddOrderMpid>(p)); break;
        case 'E': offer(h_, view<OrderExecuted>(p)); break;
        case 'C': offer(h_, view<OrderExecutedPrice>(p)); break;
        case 'X': offer(h_, view<OrderCancel>(p)); break;
        case 'D': offer(h_, view<OrderDelete>(p)); break;
        case 'U': offer(h_, view<OrderReplace>(p)); break;
        case 'P': offer(h_, view<Trade>(p)); break;
        case 'Q': offer(h_, view<CrossTrade>(p)); break;
        case 'B': offer(h_, view<BrokenTrade>(p)); break;
        case 'I': offer(h_, view<Noii>(p)); break;
        case 'N': offer(h_, view<Rpii>(p)); break;
        case 'O': offer(h_, view<DirectListingCapRaise>(p)); break;
        default:
            // Unreachable: the frame reader rejects any type byte with a zero
            // entry in kMsgLen before the message reaches dispatch, and every
            // nonzero entry has a case above. Falling through here would mean
            // kMsgLen and this switch had drifted apart.
            break;
        }
    }

    Handler& h_;
};

} // namespace carteret
