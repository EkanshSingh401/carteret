// carteret/determinism.hpp -- hashes over the reconstructed event stream and book.
//
// Correctness layer 5. Two hashes, each answering a different question:
//
//   event stream   every book effect, in order, as the book applied it. Two
//                  runs that produce the same digest applied the same effects
//                  in the same order.
//   book state     the complete final book, canonicalised. Two runs with the
//                  same digest ended in the same state.
//
// Both are needed. A reordering that cancels out leaves the final book equal
// and the event stream different; a dropped effect late in the session can
// leave the event stream nearly equal and the book wrong.
//
// The digests are committed for the synthetic session, and CI fails on any
// change that the commit message does not explain. That is what gives
// "replay-exact" something it can fail.
//
// What this does not establish: that the reconstruction is right. A
// consistently wrong reconstruction hashes consistently. It detects change,
// not error.

#pragma once

#include "book_types.hpp"
#include "messages.hpp"
#include "reference_book.hpp"
#include "sha256.hpp"

#include <cstdint>
#include <string>

namespace carteret {

// Hashes each book effect as a fixed-width record. Fixed width matters: a
// delimiterless concatenation of variable-width fields lets two different
// event streams produce the same bytes.
class EventStreamHash {
public:
    // The kind byte distinguishes effects that would otherwise share a field
    // layout, so an execute and a cancel of the same size on the same order
    // do not hash alike.
    enum class Effect : std::uint8_t {
        Add = 1,
        Execute = 2,
        ExecuteWithPrice = 3,
        Cancel = 4,
        Delete = 5,
        Replace = 6,
        Trade = 7,
        Cross = 8,
        Broken = 9,
        SystemEvent = 10,
    };

    void add(std::uint64_t ts, std::uint16_t locate, Ref ref, unsigned char side, Price price,
             Shares shares) noexcept {
        head(Effect::Add, ts, locate);
        h_.update_u64(ref);
        h_.update_u8(side);
        h_.update_u32(price);
        h_.update_u32(shares);
    }
    void reduce(Effect e, std::uint64_t ts, std::uint16_t locate, Ref ref,
                Shares qty) noexcept {
        head(e, ts, locate);
        h_.update_u64(ref);
        h_.update_u32(qty);
    }
    void remove(std::uint64_t ts, std::uint16_t locate, Ref ref) noexcept {
        head(Effect::Delete, ts, locate);
        h_.update_u64(ref);
    }
    void replace(std::uint64_t ts, std::uint16_t locate, Ref old_ref, Ref new_ref, Price price,
                 Shares shares) noexcept {
        head(Effect::Replace, ts, locate);
        h_.update_u64(old_ref);
        h_.update_u64(new_ref);
        h_.update_u32(price);
        h_.update_u32(shares);
    }
    void print(Effect e, std::uint64_t ts, std::uint16_t locate, Price price,
               std::uint64_t shares, std::uint64_t match) noexcept {
        head(e, ts, locate);
        h_.update_u32(price);
        h_.update_u64(shares);
        h_.update_u64(match);
    }
    void system_event(std::uint64_t ts, unsigned char code) noexcept {
        head(Effect::SystemEvent, ts, 0);
        h_.update_u8(code);
    }

    [[nodiscard]] std::string hex() const { return h_.hex(); }
    [[nodiscard]] std::uint64_t count() const noexcept { return n_; }

private:
    void head(Effect e, std::uint64_t ts, std::uint16_t locate) noexcept {
        h_.update_u8(static_cast<std::uint8_t>(e));
        h_.update_u64(ts);
        h_.update_u32(locate);
        ++n_;
    }

    Sha256 h_;
    std::uint64_t n_ = 0;
};

// Canonical hash of a whole reference book. Walked in a fixed order --
// ascending locate, bids then asks, ascending price, FIFO order within a level
// -- so the digest depends on the book's contents and not on any container's
// iteration order or allocation history.
inline std::string hash_book(const ReferenceBook& book) {
    Sha256 h;
    for (std::uint32_t locate = 0; locate <= 0xFFFF; ++locate) {
        const RefSymbol* sym = book.symbol(static_cast<std::uint16_t>(locate));
        if (!sym) continue;
        if (sym->bids.empty() && sym->asks.empty()) continue;
        h.update_u32(locate);
        for (int side = 0; side < 2; ++side) {
            const auto& m = (side == 0) ? sym->bids : sym->asks;
            h.update_u8(static_cast<std::uint8_t>(side == 0 ? kBuy : kSell));
            h.update_u32(static_cast<std::uint32_t>(m.size()));
            for (const auto& [price, lvl] : m) {
                h.update_u32(price);
                h.update_u64(lvl.shares);
                h.update_u32(lvl.orders);
                // Queue composition is part of the state: a book with the same
                // depth in a different order is a different book for every
                // queue-position result.
                for (const Ref r : lvl.fifo) {
                    h.update_u64(r);
                    const RefOrder* o = book.order(r);
                    h.update_u32(o ? o->shares : 0u);
                }
            }
        }
    }
    return h.hex();
}

// A parser handler that drives a reference book and hashes the effects as it
// goes. Used by the determinism tool and by the golden-hash test.
struct DeterministicReplay {
    ReferenceBook book;
    EventStreamHash events;

    void on(SystemEvent v) {
        book.on(v);
        events.system_event(v.ts(), v.event_code());
    }
    void on(StockDirectory v) { book.on(v); }
    void on(AddOrder v) {
        book.on(v);
        events.add(v.ts(), v.locate(), v.order_ref(), v.side(), v.price(), v.shares());
    }
    void on(AddOrderMpid v) {
        book.on(v);
        events.add(v.ts(), v.locate(), v.order_ref(), v.side(), v.price(), v.shares());
    }
    void on(OrderExecuted v) {
        book.on(v);
        events.reduce(EventStreamHash::Effect::Execute, v.ts(), v.locate(), v.order_ref(),
                      v.executed_shares());
    }
    void on(OrderExecutedPrice v) {
        book.on(v);
        events.reduce(EventStreamHash::Effect::ExecuteWithPrice, v.ts(), v.locate(),
                      v.order_ref(), v.executed_shares());
    }
    void on(OrderCancel v) {
        book.on(v);
        events.reduce(EventStreamHash::Effect::Cancel, v.ts(), v.locate(), v.order_ref(),
                      v.cancelled_shares());
    }
    void on(OrderDelete v) {
        book.on(v);
        events.remove(v.ts(), v.locate(), v.order_ref());
    }
    void on(OrderReplace v) {
        book.on(v);
        events.replace(v.ts(), v.locate(), v.old_order_ref(), v.new_order_ref(), v.price(),
                       v.shares());
    }
    void on(Trade v) {
        book.on(v);
        events.print(EventStreamHash::Effect::Trade, v.ts(), v.locate(), v.price(), v.shares(),
                     v.match_number());
    }
    void on(CrossTrade v) {
        book.on(v);
        events.print(EventStreamHash::Effect::Cross, v.ts(), v.locate(), v.cross_price(),
                     v.shares(), v.match_number());
    }
    void on(BrokenTrade v) {
        book.on(v);
        events.print(EventStreamHash::Effect::Broken, v.ts(), v.locate(), 0, 0,
                     v.match_number());
    }

    [[nodiscard]] std::string event_hash() const { return events.hex(); }
    [[nodiscard]] std::string book_hash() const { return hash_book(book); }
};

} // namespace carteret
