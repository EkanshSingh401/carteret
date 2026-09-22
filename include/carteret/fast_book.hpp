// carteret/fast_book.hpp -- the market-by-order book the benchmarks measure.
//
// Same semantics as ReferenceBook, different structures:
//
//   levels   a flat array indexed in whole cents over a per-symbol window,
//            with an ordered map for prices outside it (record 018)
//   BBO      a two-level bitmap over that array, queried with clz/ctz, so the
//            search after a level empties is bounded (record 019)
//   queue    an intrusive doubly-linked FIFO per level over a pooled array of
//            orders, addressed by 32-bit index rather than by pointer
//   index    open-addressed, hash as a template policy (record 016)
//   order    24 or 32 bytes, a compile-time switch (record 017)
//
// Nothing here allocates after construction. Every capacity is reserved up
// front from the configuration, and exhausting one is counted and reported
// rather than grown on the hot path, so a sizing error surfaces as a number
// instead of as a latency spike (record 024).
//
// The fast book does not carry a symbol string or MPID attribution: locate
// codes address symbols within a session (record 013), and attribution is not
// part of the book state the differential harness compares. Anything needing
// attribution reads it from the message or from the reference book.

#pragma once

#include "book_types.hpp"
#include "hash_policy.hpp"
#include "messages.hpp"
#include "order_index.hpp"
#include "spec.hpp"

#include <bit>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

namespace carteret {

// Order record size, in bytes. 24 and 32 are the two candidates: from a
// 64-byte-aligned base, 24-byte records straddle a cache line 2 times in 8 and
// 32-byte records never do, while 24 packs 2.67 per line against 2. Which wins
// is an open question (record 017), so both are built and measured. The two
// layouts carry identical fields and differ only in trailing padding, so the
// experiment measures the straddle and nothing else.
#ifndef CARTERET_ORDER_BYTES
#define CARTERET_ORDER_BYTES 24
#endif

// Width of the per-symbol flat level array, in cents. Must be a multiple of 64
// so the first-level bitmap tiles whole words, and at most 4096 so the summary
// bitmap fits in one word. Window width against overflow rate is a logged
// experiment; the default is a starting point, not a result.
#ifndef CARTERET_WINDOW_TICKS
#define CARTERET_WINDOW_TICKS 256
#endif

inline constexpr std::size_t kWindowTicks = CARTERET_WINDOW_TICKS;
static_assert(kWindowTicks % 64 == 0, "the level bitmap tiles 64-bit words");
static_assert(kWindowTicks <= 4096, "the summary bitmap must fit in one 64-bit word");
inline constexpr std::size_t kBitmapWords = kWindowTicks / 64;

inline constexpr std::uint32_t kNoLevel = 0xFFFFFFFFu;

// A resting order. Addressed by 32-bit pool index, never by pointer, so the
// pool can be relocated at construction and so each link costs 4 bytes rather
// than 8.
struct FastOrder {
    std::uint32_t next = kNoOrder; // next in its level's FIFO, or the free list
    std::uint32_t prev = kNoOrder;
    std::uint32_t shares = 0;
    Price price = 0;
    std::uint16_t locate = 0;
    std::uint8_t side = 0;
    std::uint8_t in_overflow = 0;
    // Explicit trailing padding. The two variants carry identical fields; only
    // the alignment against a 64-byte line differs, which is the whole point
    // of the experiment.
    std::uint8_t pad[CARTERET_ORDER_BYTES - 20] = {};
};
static_assert(sizeof(FastOrder) == CARTERET_ORDER_BYTES,
              "the order record must be exactly the configured size, or the "
              "cache-line straddle experiment measures something else");

// One price level. 24 bytes, so the aggregate and both FIFO ends come from at
// most one cache line.
struct FastLevel {
    std::uint64_t shares = 0;
    std::uint32_t orders = 0;
    std::uint32_t head = kNoOrder;
    std::uint32_t tail = kNoOrder;
    std::uint32_t reserved = 0;
};
static_assert(sizeof(FastLevel) == 24);

// Occupancy over one side's flat window. The summary bit for a word is set if
// and only if that word is nonzero, so finding the extreme occupied level is
// two scans rather than a walk over the window.
struct LevelBitmap {
    std::uint64_t words[kBitmapWords] = {};
    std::uint64_t summary = 0;

    void set(std::size_t i) noexcept {
        words[i >> 6] |= (1ULL << (i & 63));
        summary |= (1ULL << (i >> 6));
    }
    void clear(std::size_t i) noexcept {
        const std::size_t w = i >> 6;
        words[w] &= ~(1ULL << (i & 63));
        if (words[w] == 0) summary &= ~(1ULL << w);
    }
    [[nodiscard]] bool empty() const noexcept { return summary == 0; }

    // Lowest occupied index, or kWindowTicks if none.
    [[nodiscard]] std::size_t lowest() const noexcept {
        if (summary == 0) return kWindowTicks;
        const std::size_t w = static_cast<std::size_t>(std::countr_zero(summary));
        return (w << 6) + static_cast<std::size_t>(std::countr_zero(words[w]));
    }
    // Highest occupied index, or kWindowTicks if none.
    [[nodiscard]] std::size_t highest() const noexcept {
        if (summary == 0) return kWindowTicks;
        const std::size_t w = 63 - static_cast<std::size_t>(std::countl_zero(summary));
        return (w << 6) + 63 - static_cast<std::size_t>(std::countl_zero(words[w]));
    }
};

// One side of one symbol.
struct FastSide {
    std::vector<FastLevel> levels; // kWindowTicks entries, allocated once
    LevelBitmap occupied;
    std::map<Price, FastLevel> overflow; // ordered: supplies the BBO beyond the window
};

struct FastSymbol {
    FastSide bid;
    FastSide ask;
    std::int64_t base_cents = -1; // window origin; -1 until the first order
};

struct FastCounters {
    std::uint64_t orphan_execute = 0;
    std::uint64_t orphan_execute_price = 0;
    std::uint64_t orphan_cancel = 0;
    std::uint64_t orphan_delete = 0;
    std::uint64_t orphan_replace = 0;
    std::uint64_t duplicate_add = 0;
    std::uint64_t zero_share_cross = 0;
    std::uint64_t removed_at_zero = 0;
    std::uint64_t over_execute = 0;
    std::uint64_t crossed_observations = 0;
    std::uint64_t locked_observations = 0;
    std::uint64_t after_end_of_system_hours = 0;
    std::uint64_t book_messages = 0;

    // Structure-specific, with no counterpart in the reference book.
    std::uint64_t overflow_hits = 0;   // orders placed outside the flat window
    std::uint64_t sub_cent_prices = 0; // prices that are not a whole cent
    std::uint64_t pool_exhausted = 0;  // order pool capacity reached
    std::uint64_t symbol_overflow = 0; // locate code beyond the configured maximum
    std::uint64_t index_failures = 0;

    [[nodiscard]] std::uint64_t orphans() const noexcept {
        return orphan_execute + orphan_execute_price + orphan_cancel + orphan_delete +
               orphan_replace;
    }
};

struct FastBookConfig {
    std::size_t max_orders = 4u << 20;  // peak simultaneously resting orders
    std::size_t max_symbols = 1u << 14; // highest stock locate code plus one
    std::size_t index_hint = 4u << 20;  // expected peak index occupancy
};

// Mirrors ReferenceBook::level(), so the differential harness can compare the
// two through one shape.
struct FastLevelSnapshot {
    bool present = false;
    std::uint64_t shares = 0;
    std::uint32_t orders = 0;
    std::vector<Ref> fifo;
};

template<class HashPolicy = MultiplyShiftHash>
class FastBook {
public:
    explicit FastBook(FastBookConfig cfg = {}) : cfg_(cfg), index_(cfg.index_hint) {
        pool_.resize(cfg_.max_orders);
        refs_.resize(cfg_.max_orders, 0);
        symbols_.resize(cfg_.max_symbols);
        // LIFO free list, seeded in reverse so the first allocation takes slot
        // 0 and the pool fills forward. The slot a cancel frees is the one the
        // next add takes, which is what keeps short-lived orders resident
        // (record 015).
        free_head_ = kNoOrder;
        for (std::size_t i = cfg_.max_orders; i-- > 0;) {
            pool_[i].next = free_head_;
            free_head_ = static_cast<std::uint32_t>(i);
        }
    }

    // --- handler interface -------------------------------------------------

    void on(SystemEvent v) {
        if (v.event_code() == 'E') after_hours_ = true;
    }

    void on(AddOrder v) {
        tick();
        add(v.locate(), v.order_ref(), v.side(), v.price(), v.shares());
    }
    void on(AddOrderMpid v) {
        tick();
        add(v.locate(), v.order_ref(), v.side(), v.price(), v.shares());
    }
    void on(OrderExecuted v) {
        tick();
        if (!reduce(v.order_ref(), v.executed_shares())) ++counters_.orphan_execute;
        check_bbo(v.locate());
    }
    void on(OrderExecutedPrice v) {
        tick();
        if (!reduce(v.order_ref(), v.executed_shares())) ++counters_.orphan_execute_price;
        check_bbo(v.locate());
    }
    void on(OrderCancel v) {
        tick();
        if (!reduce(v.order_ref(), v.cancelled_shares())) ++counters_.orphan_cancel;
        check_bbo(v.locate());
    }
    void on(OrderDelete v) {
        tick();
        if (!erase_ref(v.order_ref())) ++counters_.orphan_delete;
        check_bbo(v.locate());
    }
    void on(OrderReplace v) {
        tick();
        const std::uint32_t idx = index_.find(v.old_order_ref());
        if (idx == kNoOrder) {
            ++counters_.orphan_replace;
            return;
        }
        // Side is retained from the original add; 'U' does not carry it
        // (record 009). The order goes to the back of the queue at the new
        // price even when the price is unchanged, which is what remove-then-add
        // produces.
        const std::uint16_t locate = pool_[idx].locate;
        const std::uint8_t side = pool_[idx].side;
        erase_ref(v.old_order_ref());
        add(locate, v.new_order_ref(), side, v.price(), v.shares());
        check_bbo(locate);
    }

    // No book effect (record 011).
    void on(Trade) { tick(); }
    void on(BrokenTrade) { tick(); }
    void on(CrossTrade v) {
        tick();
        if (v.shares() == 0) ++counters_.zero_share_cross;
    }

    // --- queries -----------------------------------------------------------

    [[nodiscard]] const FastCounters& counters() const noexcept { return counters_; }
    [[nodiscard]] std::size_t live_orders() const noexcept { return index_.size(); }
    [[nodiscard]] const OrderIndex<HashPolicy>& index() const noexcept { return index_; }

    [[nodiscard]] bool has_bid(std::uint16_t locate) const noexcept {
        return best(locate, kBuy).first;
    }
    [[nodiscard]] bool has_ask(std::uint16_t locate) const noexcept {
        return best(locate, kSell).first;
    }
    [[nodiscard]] Price best_bid(std::uint16_t locate) const noexcept {
        return best(locate, kBuy).second;
    }
    [[nodiscard]] Price best_ask(std::uint16_t locate) const noexcept {
        return best(locate, kSell).second;
    }

    [[nodiscard]] FastLevelSnapshot level(std::uint16_t locate, unsigned char side,
                                          Price price) const {
        FastLevelSnapshot out;
        if (locate >= symbols_.size()) return out;
        const FastSymbol& sym = symbols_[locate];
        const FastSide& s = (side == kBuy) ? sym.bid : sym.ask;
        const FastLevel* lvl = find_level(sym, s, price);
        if (!lvl || lvl->orders == 0) return out;
        out.present = true;
        out.shares = lvl->shares;
        out.orders = lvl->orders;
        for (std::uint32_t i = lvl->head; i != kNoOrder; i = pool_[i].next) {
            out.fifo.push_back(refs_[i]);
        }
        return out;
    }

    // Structural invariants over the whole book. Returns the violation count,
    // which must be zero.
    [[nodiscard]] std::size_t check_invariants() const {
        std::size_t bad = 0;
        for (std::size_t locate = 0; locate < symbols_.size(); ++locate) {
            const FastSymbol& sym = symbols_[locate];
            for (const FastSide* s : {&sym.bid, &sym.ask}) {
                // The window may be unallocated while the overflow map is
                // populated, so the overflow levels are checked either way.
                for (std::size_t i = 0; i < s->levels.size(); ++i) {
                    const FastLevel& lvl = s->levels[i];
                    const bool bit = (s->occupied.words[i >> 6] >> (i & 63)) & 1ULL;
                    // The occupancy bit must agree with the level exactly, or
                    // the BBO search reports a price with nothing on it.
                    if (bit != (lvl.orders != 0)) ++bad;
                    bad += walk_level(lvl);
                }
                for (const auto& [price, lvl] : s->overflow) {
                    (void)price;
                    if (lvl.orders == 0) ++bad; // an emptied overflow level is dropped
                    bad += walk_level(lvl);
                }
            }
        }
        return bad;
    }

    [[nodiscard]] static const char* policy_name() noexcept { return HashPolicy::name; }
    [[nodiscard]] static constexpr std::size_t order_bytes() noexcept {
        return sizeof(FastOrder);
    }
    [[nodiscard]] static constexpr std::size_t window_ticks() noexcept { return kWindowTicks; }

private:
    void tick() {
        ++counters_.book_messages;
        if (after_hours_) ++counters_.after_end_of_system_hours;
    }

    // Price(4) carries four implied decimals; the axis is whole cents
    // (record 005). A price that is not a whole cent cannot be indexed and
    // goes to the overflow map, counted, rather than being rounded onto a
    // level it does not belong on.
    static bool to_cents(Price p, std::int64_t& cents) noexcept {
        if (p % 100u != 0u) return false;
        cents = static_cast<std::int64_t>(p / 100u);
        return true;
    }

    std::size_t walk_level(const FastLevel& lvl) const {
        std::size_t bad = 0;
        std::uint64_t sum = 0;
        std::uint32_t n = 0;
        std::uint32_t prev = kNoOrder;
        for (std::uint32_t i = lvl.head; i != kNoOrder; i = pool_[i].next) {
            if (pool_[i].prev != prev) ++bad;
            if (pool_[i].shares == 0) ++bad; // zero-share orders must be gone
            sum += pool_[i].shares;
            prev = i;
            ++n;
            if (n > lvl.orders) break; // a cycle; stop rather than spin
        }
        if (prev != lvl.tail) ++bad;
        if (n != lvl.orders) ++bad;
        if (sum != lvl.shares) ++bad;
        return bad;
    }

    std::uint32_t alloc() noexcept {
        if (free_head_ == kNoOrder) {
            ++counters_.pool_exhausted;
            return kNoOrder;
        }
        const std::uint32_t i = free_head_;
        free_head_ = pool_[i].next;
        return i;
    }

    void release(std::uint32_t i) noexcept {
        pool_[i].next = free_head_;
        pool_[i].prev = kNoOrder;
        pool_[i].shares = 0;
        free_head_ = i;
    }

    FastSide& side_of(FastSymbol& sym, unsigned char side) noexcept {
        return (side == kBuy) ? sym.bid : sym.ask;
    }

    // Resolves a price to its level, allocating the symbol's window on first
    // use. Returns nullptr only when the pool or symbol table is exhausted.
    FastLevel* level_for(FastSymbol& sym, FastSide& s, Price price, bool& overflowed) {
        std::int64_t cents = 0;
        const bool whole = to_cents(price, cents);
        if (!whole) {
            ++counters_.sub_cent_prices;
            overflowed = true;
            ++counters_.overflow_hits;
            return &s.overflow[price];
        }
        if (sym.base_cents < 0) {
            // Centre the window on the first price seen for the symbol, which
            // is the only information available at that point.
            sym.base_cents = cents - static_cast<std::int64_t>(kWindowTicks / 2);
            if (sym.base_cents < 0) sym.base_cents = 0;
        }
        if (s.levels.empty()) s.levels.resize(kWindowTicks);

        const std::int64_t off = cents - sym.base_cents;
        if (off < 0 || off >= static_cast<std::int64_t>(kWindowTicks)) {
            overflowed = true;
            ++counters_.overflow_hits;
            return &s.overflow[price];
        }
        overflowed = false;
        return &s.levels[static_cast<std::size_t>(off)];
    }

    const FastLevel* find_level(const FastSymbol& sym, const FastSide& s, Price price) const {
        std::int64_t cents = 0;
        if (to_cents(price, cents) && sym.base_cents >= 0 && !s.levels.empty()) {
            const std::int64_t off = cents - sym.base_cents;
            if (off >= 0 && off < static_cast<std::int64_t>(kWindowTicks)) {
                return &s.levels[static_cast<std::size_t>(off)];
            }
        }
        const auto it = s.overflow.find(price);
        return it == s.overflow.end() ? nullptr : &it->second;
    }

    void add(std::uint16_t locate, Ref ref, unsigned char side, Price price,
             std::uint32_t shares) {
        if (locate >= symbols_.size()) {
            ++counters_.symbol_overflow;
            return;
        }
        // A reference is day-unique (record 012). A live duplicate means the
        // feed or the reconstruction is wrong; the incoming order wins, as in
        // the reference book, so the two stay comparable.
        if (index_.find(ref) != kNoOrder) {
            ++counters_.duplicate_add;
            erase_ref(ref);
        }

        const std::uint32_t idx = alloc();
        if (idx == kNoOrder) return;

        FastSymbol& sym = symbols_[locate];
        FastSide& s = side_of(sym, side);
        bool overflowed = false;
        FastLevel* lvl = level_for(sym, s, price, overflowed);

        FastOrder& o = pool_[idx];
        o.next = kNoOrder;
        o.prev = lvl->tail;
        o.shares = shares;
        o.price = price;
        o.locate = locate;
        o.side = static_cast<std::uint8_t>(side);
        o.in_overflow = overflowed ? 1u : 0u;
        refs_[idx] = ref;

        if (lvl->tail != kNoOrder)
            pool_[lvl->tail].next = idx;
        else
            lvl->head = idx;
        lvl->tail = idx;

        const bool was_empty = (lvl->orders == 0);
        lvl->shares += shares;
        ++lvl->orders;

        if (!overflowed && was_empty) {
            std::int64_t cents = 0;
            to_cents(price, cents);
            s.occupied.set(static_cast<std::size_t>(cents - sym.base_cents));
        }

        if (!index_.insert(ref, idx)) {
            ++counters_.index_failures;
            unlink(idx);
            release(idx);
            return;
        }
        check_bbo(locate);
    }

    // Deducts shares. Returns false when the reference names no live order.
    bool reduce(Ref ref, std::uint32_t qty) {
        const std::uint32_t idx = index_.find(ref);
        if (idx == kNoOrder) return false;
        FastOrder& o = pool_[idx];

        std::uint32_t taken = qty;
        if (qty > o.shares) {
            ++counters_.over_execute;
            taken = o.shares;
        }

        FastLevel* lvl = mutable_level(o);
        if (lvl) lvl->shares -= taken;
        o.shares -= taken;

        if (o.shares == 0) {
            ++counters_.removed_at_zero;
            index_.erase(ref);
            unlink(idx);
            release(idx);
        }
        return true;
    }

    bool erase_ref(Ref ref) {
        const std::uint32_t idx = index_.erase(ref);
        if (idx == kNoOrder) return false;
        FastOrder& o = pool_[idx];
        FastLevel* lvl = mutable_level(o);
        if (lvl) lvl->shares -= o.shares;
        unlink(idx);
        release(idx);
        return true;
    }

    FastLevel* mutable_level(const FastOrder& o) {
        if (o.locate >= symbols_.size()) return nullptr;
        FastSymbol& sym = symbols_[o.locate];
        FastSide& s = side_of(sym, o.side);
        if (o.in_overflow) {
            const auto it = s.overflow.find(o.price);
            return it == s.overflow.end() ? nullptr : &it->second;
        }
        std::int64_t cents = 0;
        if (!to_cents(o.price, cents) || sym.base_cents < 0 || s.levels.empty()) return nullptr;
        const std::int64_t off = cents - sym.base_cents;
        if (off < 0 || off >= static_cast<std::int64_t>(kWindowTicks)) return nullptr;
        return &s.levels[static_cast<std::size_t>(off)];
    }

    // Removes an order from its level's FIFO and clears the occupancy bit if
    // the level is now empty.
    void unlink(std::uint32_t idx) {
        FastOrder& o = pool_[idx];
        if (o.locate >= symbols_.size()) return;
        FastSymbol& sym = symbols_[o.locate];
        FastSide& s = side_of(sym, o.side);
        FastLevel* lvl = mutable_level(o);
        if (!lvl) return;

        if (o.prev != kNoOrder)
            pool_[o.prev].next = o.next;
        else
            lvl->head = o.next;
        if (o.next != kNoOrder)
            pool_[o.next].prev = o.prev;
        else
            lvl->tail = o.prev;
        o.next = kNoOrder;
        o.prev = kNoOrder;
        if (lvl->orders) --lvl->orders;

        if (lvl->orders == 0) {
            if (o.in_overflow) {
                // The overflow map drops emptied levels so it does not grow
                // without bound, and so that it matches the reference book,
                // which cannot distinguish an empty level from an absent one.
                s.overflow.erase(o.price);
            } else {
                std::int64_t cents = 0;
                to_cents(o.price, cents);
                s.occupied.clear(static_cast<std::size_t>(cents - sym.base_cents));
            }
        }
    }

    // Best price on a side. The flat window and the overflow map are both
    // consulted: an order outside the window still sets the inside, and a BBO
    // read from the bitmap alone would be wrong whenever the overflow map held
    // a better price.
    [[nodiscard]] std::pair<bool, Price> best(std::uint16_t locate,
                                              unsigned char side) const noexcept {
        if (locate >= symbols_.size()) return {false, 0};
        const FastSymbol& sym = symbols_[locate];
        const FastSide& s = (side == kBuy) ? sym.bid : sym.ask;

        bool have = false;
        Price out = 0;
        // The window origin is unset until the symbol's first whole-cent
        // price. A symbol whose first order is sub-cent has an empty window
        // and a populated overflow map, so the overflow side must be consulted
        // regardless of whether the window exists.
        if (sym.base_cents >= 0 && !s.occupied.empty()) {
            const std::size_t i = (side == kBuy) ? s.occupied.highest() : s.occupied.lowest();
            if (i < kWindowTicks) {
                out = static_cast<Price>((sym.base_cents + static_cast<std::int64_t>(i)) * 100);
                have = true;
            }
        }
        if (!s.overflow.empty()) {
            const Price candidate =
                (side == kBuy) ? s.overflow.rbegin()->first : s.overflow.begin()->first;
            if (!have) {
                out = candidate;
                have = true;
            } else if ((side == kBuy) ? (candidate > out) : (candidate < out)) {
                out = candidate;
            }
        }
        return {have, out};
    }

    // Crossed and locked books occur legitimately around halts and auctions.
    // Counted per observation, never repaired (record 007).
    void check_bbo(std::uint16_t locate) {
        const auto b = best(locate, kBuy);
        const auto a = best(locate, kSell);
        if (!b.first || !a.first) return;
        if (b.second > a.second)
            ++counters_.crossed_observations;
        else if (b.second == a.second)
            ++counters_.locked_observations;
    }

    FastBookConfig cfg_;
    OrderIndex<HashPolicy> index_;
    std::vector<FastOrder> pool_;
    std::vector<Ref> refs_; // parallel to pool_; keeps FastOrder at its target size
    std::vector<FastSymbol> symbols_;
    std::uint32_t free_head_ = kNoOrder;
    FastCounters counters_;
    bool after_hours_ = false;
};

} // namespace carteret
