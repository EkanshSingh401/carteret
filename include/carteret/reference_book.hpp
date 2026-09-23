// carteret/reference_book.hpp -- the deliberately simple market-by-order book.
//
// std::map per side, std::unordered_map for the order index, std::list for the
// per-level FIFO. Written for transparency, not speed: every semantic rule is
// one obvious statement, and nothing is elided for performance.
//
// This implementation is permanent. It is the differential oracle for the fast
// book and the baseline every speedup is measured against, and both roles
// begin the moment the fast book first passes. See docs/design.md record 014.
//
// Semantic rules implemented here, each with its record:
//   007  crossed and locked books, zero-share crosses, orphaned modifies and
//        post-'E' arrivals are counted, never repaired and never asserted on
//   008  'E', 'C' and 'X' deduct cumulatively; zero shares removes the order
//        without waiting for a 'D'
//   009  'U' retains side, stock and attribution, and goes to the back of the
//        queue at its new price even when the price is unchanged
//   010  'E' executes at the resting order's price; 'C' carries its own
//   011  'P', 'Q' and 'B' have no book effect
//   012  order references are day-unique and not sequential
//   013  stock locate codes are session-scoped

#pragma once

#include "book_types.hpp"
#include "messages.hpp"
#include "spec.hpp"

#include <array>
#include <cstdint>
#include <list>
#include <map>
#include <set>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace carteret {

// One price level. The FIFO holds order references in arrival order; the
// aggregates are maintained alongside so the invariant check has something
// independent to compare against.
struct RefLevel {
    std::uint64_t shares = 0;
    std::uint32_t orders = 0;
    std::list<Ref> fifo;
};

// A resting order. Side, stock and attribution are stored because 'U' does not
// carry them and they must be retained across a replace (record 009).
struct RefOrder {
    std::uint16_t locate = 0;
    unsigned char side = 0;
    Price price = 0;
    Shares shares = 0;
    std::array<char, 8> stock{};
    std::array<char, 4> attribution{};
    std::list<Ref>::iterator slot{}; // position in its level's FIFO
    std::uint64_t added_ts = 0;      // for the order-age attribution in Stage 4
};

// Both sides of one symbol. Ascending maps on both sides: the best bid is the
// last key, the best ask the first. Keeping one comparator keeps the code
// obvious at the cost of an rbegin() on the bid side.
struct RefSymbol {
    std::map<Price, RefLevel> bids;
    std::map<Price, RefLevel> asks;

    [[nodiscard]] bool has_bid() const noexcept { return !bids.empty(); }
    [[nodiscard]] bool has_ask() const noexcept { return !asks.empty(); }
    [[nodiscard]] Price best_bid() const noexcept { return bids.rbegin()->first; }
    [[nodiscard]] Price best_ask() const noexcept { return asks.begin()->first; }
};

// Conditions that are correct and surprising. Counted per session, never
// repaired and never asserted on (record 007). A change in these rates between
// runs means something in the reconstruction changed.
struct RefCounters {
    std::uint64_t orphan_execute = 0; // 'E' naming a reference with no live order
    std::uint64_t orphan_execute_price = 0;
    std::uint64_t orphan_cancel = 0;
    std::uint64_t orphan_delete = 0;
    std::uint64_t orphan_replace = 0;
    std::uint64_t duplicate_add = 0;    // 'A'/'F' reusing a live reference
    std::uint64_t zero_share_cross = 0; // 'Q' reporting no shares
    std::uint64_t removed_at_zero = 0;  // orders that reached zero without a 'D'
    std::uint64_t over_execute = 0;     // executed shares exceeded the resting size
    std::uint64_t missing_level = 0;    // an order's level was absent; internal inconsistency
    std::uint64_t crossed_observations = 0;
    std::uint64_t locked_observations = 0;
    std::uint64_t after_end_of_system_hours = 0;
    std::uint64_t book_messages = 0;

    // WHERE the crossed and locked observations are, not only how many. A
    // gate that fails with a count and no location cannot be acted on, and
    // the first question about a crossed book is always whether it sits in
    // continuous trading or around an auction. Kept on the reference book
    // only: these are diagnostics for a failure, not conditions the two books
    // are compared on.
    std::uint64_t crossed_continuous = 0; // 09:30:00 <= ts < 16:00:00
    std::uint64_t locked_continuous = 0;
    std::uint64_t crossed_first_ts = 0;
    std::uint64_t crossed_last_ts = 0;
    std::uint64_t locked_first_ts = 0;
    std::uint64_t locked_last_ts = 0;
    std::uint64_t crossed_worst_ticks = 0; // deepest crossing seen, in Price(4) units
    std::set<std::uint16_t> crossed_symbols;
    std::set<std::uint16_t> locked_symbols;
    // Observations made while the symbol was NOT in state 'T'. A venue that
    // is not matching a symbol cannot prevent its book from crossing, so this
    // is the split that decides whether a crossing is a market condition or a
    // reconstruction error.
    std::uint64_t crossed_while_not_trading = 0;
    std::uint64_t locked_while_not_trading = 0;
    std::uint64_t trading_state_changes = 0;
    // Observations excused because the symbol had resumed but its reopening
    // cross had not yet run. Between the 'T' that restores trading and the
    // cross that clears the accumulated book, the venue is not yet matching
    // and the book may still be crossed. That window is bounded by observable
    // events rather than by a tolerance chosen to fit.
    //
    // It closes on whichever comes first: the symbol's next cross trade, or
    // the first observation in which its book is NOT crossed or locked. The
    // second bound is what keeps the excuse honest. Waiting only for a cross
    // leaves the window open until the closing cross when no halt cross
    // arrives -- measured at 6.4 hours on one symbol of
    // `12302019.NASDAQ_ITCH50` -- and a gate that excuses a whole afternoon
    // is not a gate. A book that uncrosses has demonstrated that the venue is
    // matching it, and anything after that stands on its own.
    std::uint64_t crossed_awaiting_reopen = 0;
    std::uint64_t locked_awaiting_reopen = 0;
    std::uint64_t reopen_windows = 0;
    std::uint64_t reopen_window_max_ns = 0;
    // Everything else: in state 'T', reopening cross already run or the
    // symbol never halted. These fail the gate.
    std::uint64_t crossed_unexplained = 0;
    std::uint64_t locked_unexplained = 0;
    std::set<std::uint16_t> unexplained_symbols;

    [[nodiscard]] std::uint64_t orphans() const noexcept {
        return orphan_execute + orphan_execute_price + orphan_cancel + orphan_delete +
               orphan_replace;
    }
};

// Aggregate view of one price level, for the differential comparison.
struct LevelSnapshot {
    bool present = false;
    std::uint64_t shares = 0;
    std::uint32_t orders = 0;
    std::vector<Ref> fifo;
};

class ReferenceBook {
public:
    // A handler for Parser. Book-affecting types are applied; 'P', 'Q' and 'B'
    // are observed for their counters only.
    void on(SystemEvent v) {
        if (v.event_code() == 'E') after_hours_ = true;
    }

    // 'H' Stock Trading Action. The book is not changed by it; the trading
    // state is retained because a symbol that is halted, paused or in its
    // quotation-only period is one the venue is not matching, and an unmatched
    // book may legitimately cross. State 'T' is trading; 'H' and 'P' are
    // halted and paused; 'Q' is quotation only.
    void on(StockTradingAction v) {
        const unsigned char st = v.trading_state();
        auto& slot = trading_state_[v.locate()];
        // The instant of entry into 'T' is retained only on a real transition,
        // so a repeated 'T' does not restart the clock.
        if (st == 'T' && slot.state != 'T') {
            slot.entered_t_ts = v.ts();
            // The first action of the session announces a state rather than
            // ending a halt, so it opens no reopening window.
            if (slot.state != 0) {
                slot.awaiting_reopen = true;
                ++counters_.reopen_windows;
            }
        }
        slot.state = st;
        ++counters_.trading_state_changes;
    }

    void on(StockDirectory v) {
        symbol_of(v.locate()); // materialise the slot so locate lookups are dense
        const std::string_view s = v.stock();
        auto& name = names_[v.locate()];
        name.fill(' ');
        for (std::size_t i = 0; i < s.size() && i < 8; ++i) name[i] = s[i];
    }

    void on(AddOrder v) {
        tick();
        add(v.locate(), v.order_ref(), v.side(), v.price(), v.shares(), v.stock(),
            std::string_view{}, v.ts());
    }

    void on(AddOrderMpid v) {
        tick();
        add(v.locate(), v.order_ref(), v.side(), v.price(), v.shares(), v.stock(),
            v.attribution(), v.ts());
    }

    // 'E' has no price field: the execution happens at the resting order's
    // price, which is already recorded (record 010).
    void on(OrderExecuted v) {
        tick();
        if (!execute(v.order_ref(), v.executed_shares())) ++counters_.orphan_execute;
        check_bbo(v.locate(), v.ts());
    }

    // 'C' carries its own price. Printable 'N' keeps the execution off the
    // consolidated tape; the book effect is identical, so nothing here branches
    // on it.
    void on(OrderExecutedPrice v) {
        tick();
        if (!execute(v.order_ref(), v.executed_shares())) ++counters_.orphan_execute_price;
        check_bbo(v.locate(), v.ts());
    }

    void on(OrderCancel v) {
        tick();
        if (!execute(v.order_ref(), v.cancelled_shares())) ++counters_.orphan_cancel;
        check_bbo(v.locate(), v.ts());
    }

    void on(OrderDelete v) {
        tick();
        if (!remove(v.order_ref())) ++counters_.orphan_delete;
        check_bbo(v.locate(), v.ts());
    }

    // 'U' carries no side, stock or attribution, so all three are retained from
    // the order being replaced. The new reference supersedes the old, and the
    // order goes to the back of the queue at its new price even when the price
    // is unchanged (record 009).
    void on(OrderReplace v) {
        tick();
        const auto it = orders_.find(v.old_order_ref());
        if (it == orders_.end()) {
            ++counters_.orphan_replace;
            return;
        }
        const RefOrder old = it->second;
        remove(v.old_order_ref());
        add_retained(old.locate, v.new_order_ref(), old.side, v.price(), v.shares(), old.stock,
                     old.attribution, v.ts());
        check_bbo(v.locate(), v.ts());
    }

    // No book effect (record 011). Observed only for the counters.
    void on(Trade v) {
        tick();
        (void)v;
    }
    void on(BrokenTrade v) {
        tick();
        (void)v;
    }
    // A cross for a symbol closes any reopening window it had open: the
    // accumulated book has been matched, and the venue is matching
    // continuously again from here.
    void on(CrossTrade v) {
        tick();
        if (v.shares() == 0) ++counters_.zero_share_cross;
        const auto it = trading_state_.find(v.locate());
        if (it != trading_state_.end() && it->second.awaiting_reopen) {
            it->second.awaiting_reopen = false;
            const std::uint64_t held =
                v.ts() >= it->second.entered_t_ts ? v.ts() - it->second.entered_t_ts : 0;
            if (held > counters_.reopen_window_max_ns) counters_.reopen_window_max_ns = held;
        }
    }

    // --- queries -----------------------------------------------------------

    [[nodiscard]] const RefCounters& counters() const noexcept { return counters_; }
    [[nodiscard]] std::size_t live_orders() const noexcept { return orders_.size(); }
    [[nodiscard]] std::size_t symbol_count() const noexcept { return symbols_.size(); }

    [[nodiscard]] const RefSymbol* symbol(std::uint16_t locate) const noexcept {
        auto it = symbols_.find(locate);
        return it == symbols_.end() ? nullptr : &it->second;
    }

    [[nodiscard]] const RefOrder* order(Ref r) const noexcept {
        auto it = orders_.find(r);
        return it == orders_.end() ? nullptr : &it->second;
    }

    // Aggregate and FIFO composition of one level, for the differential
    // harness. A level that has never existed and one that has emptied are
    // both reported as absent, because the fast book cannot distinguish them
    // either.
    [[nodiscard]] LevelSnapshot level(std::uint16_t locate, unsigned char side,
                                      Price price) const {
        LevelSnapshot out;
        const RefSymbol* s = symbol(locate);
        if (!s) return out;
        const auto& m = (side == kBuy) ? s->bids : s->asks;
        const auto it = m.find(price);
        if (it == m.end()) return out;
        out.present = true;
        out.shares = it->second.shares;
        out.orders = it->second.orders;
        out.fifo.assign(it->second.fifo.begin(), it->second.fifo.end());
        return out;
    }

    // Aggregate depth at a price, without copying the level's FIFO. level()
    // returns the full order sequence, which the differential harness needs
    // and which a per-message query must not pay for.
    struct Depth {
        bool present = false;
        std::uint64_t shares = 0;
        std::uint32_t orders = 0;
    };

    [[nodiscard]] Depth depth_at(std::uint16_t locate, unsigned char side,
                                 Price price) const noexcept {
        Depth d;
        const auto sit = symbols_.find(locate);
        if (sit == symbols_.end()) return d;
        const auto& m = (side == kBuy) ? sit->second.bids : sit->second.asks;
        const auto it = m.find(price);
        if (it == m.end()) return d;
        d.present = true;
        d.shares = it->second.shares;
        d.orders = it->second.orders;
        return d;
    }

    // Checks every structural invariant over the whole book. Returns the
    // number of violations, which must be zero.
    [[nodiscard]] std::size_t check_invariants() const {
        std::size_t bad = 0;
        for (const auto& [locate, sym] : symbols_) {
            for (const auto* m : {&sym.bids, &sym.asks}) {
                for (const auto& [price, lvl] : *m) {
                    if (lvl.fifo.empty()) ++bad; // an empty level must not be retained
                    if (lvl.orders != lvl.fifo.size()) ++bad;
                    std::uint64_t sum = 0;
                    for (const Ref r : lvl.fifo) {
                        const auto it = orders_.find(r);
                        if (it == orders_.end()) {
                            ++bad;
                            continue;
                        }
                        if (it->second.shares == 0) ++bad; // zero-share orders must be gone
                        if (it->second.price != price) ++bad;
                        if (it->second.locate != locate) ++bad;
                        sum += it->second.shares;
                    }
                    if (sum != lvl.shares) ++bad;
                }
            }
        }
        return bad;
    }

private:
    // One call per message, before any book effect, so a replace counts once
    // even though it performs two book operations.
    void tick() {
        ++counters_.book_messages;
        if (after_hours_) ++counters_.after_end_of_system_hours;
    }

    RefSymbol& symbol_of(std::uint16_t locate) { return symbols_[locate]; }

    void add(std::uint16_t locate, Ref ref, unsigned char side, Price price, Shares shares,
             std::string_view stock, std::string_view attribution, std::uint64_t ts) {
        std::array<char, 8> s{};
        s.fill(' ');
        for (std::size_t i = 0; i < stock.size() && i < 8; ++i) s[i] = stock[i];
        std::array<char, 4> a{};
        a.fill(' ');
        for (std::size_t i = 0; i < attribution.size() && i < 4; ++i) a[i] = attribution[i];
        add_retained(locate, ref, side, price, shares, s, a, ts);
    }

    void add_retained(std::uint16_t locate, Ref ref, unsigned char side, Price price,
                      Shares shares, const std::array<char, 8>& stock,
                      const std::array<char, 4>& attribution, std::uint64_t ts) {
        // A reference is day-unique (record 012), so a live duplicate means the
        // feed or the reconstruction is wrong. Counted, and the incoming order
        // wins, which is what a venue replacing state would do.
        if (orders_.count(ref)) {
            ++counters_.duplicate_add;
            remove(ref);
        }

        RefSymbol& sym = symbol_of(locate);
        auto& side_map = (side == kBuy) ? sym.bids : sym.asks;
        RefLevel& lvl = side_map[price];
        lvl.fifo.push_back(ref);
        lvl.shares += shares;
        ++lvl.orders;

        RefOrder o;
        o.locate = locate;
        o.side = side;
        o.price = price;
        o.shares = shares;
        o.stock = stock;
        o.attribution = attribution;
        o.slot = std::prev(lvl.fifo.end());
        o.added_ts = ts;
        orders_.emplace(ref, o);

        check_bbo(locate, ts);
    }

    // Deducts shares from a resting order. Returns false if the reference names
    // no live order, which is the orphan case. An order reaching zero is
    // removed here and not on a later 'D', because no 'D' is guaranteed
    // (record 008).
    bool execute(Ref ref, Shares qty) {
        const auto it = orders_.find(ref);
        if (it == orders_.end()) return false;
        RefOrder& o = it->second;

        // More shares than are resting cannot happen in a correct feed. Counted
        // rather than clamped silently, and the order is removed.
        Shares taken = qty;
        if (qty > o.shares) {
            ++counters_.over_execute;
            taken = o.shares;
        }

        // Looked up without inserting: an absent level here would mean the
        // book is already inconsistent, and operator[] would create an empty
        // one whose share count then wraps on the subtraction below.
        RefLevel* lvl = level_of(o);
        if (!lvl) {
            ++counters_.missing_level;
            orders_.erase(it);
            return true;
        }
        lvl->shares -= taken;
        o.shares -= taken;

        if (o.shares == 0) {
            ++counters_.removed_at_zero;
            erase(it);
        }
        return true;
    }

    bool remove(Ref ref) {
        const auto it = orders_.find(ref);
        if (it == orders_.end()) return false;
        erase(it);
        return true;
    }

    RefLevel* level_of(const RefOrder& o) {
        const auto sit = symbols_.find(o.locate);
        if (sit == symbols_.end()) return nullptr;
        auto& side_map = (o.side == kBuy) ? sit->second.bids : sit->second.asks;
        const auto lit = side_map.find(o.price);
        return lit == side_map.end() ? nullptr : &lit->second;
    }

    void erase(std::unordered_map<Ref, RefOrder>::iterator it) {
        const RefOrder& o = it->second;
        RefSymbol& sym = symbols_[o.locate];
        auto& side_map = (o.side == kBuy) ? sym.bids : sym.asks;
        const auto lit = side_map.find(o.price);
        if (lit != side_map.end()) {
            RefLevel& lvl = lit->second;
            lvl.fifo.erase(o.slot);
            lvl.shares -= o.shares;
            --lvl.orders;
            // An empty level is dropped: the fast book's flat array cannot
            // distinguish an empty level from one that never existed, so the
            // reference must not either, or the differential diverges on a
            // difference that means nothing.
            if (lvl.fifo.empty()) side_map.erase(lit);
        }
        orders_.erase(it);
    }

    // Counted per observation, never repaired (record 007). A nonzero count
    // fails the replay gate (record 027), so the location is recorded along
    // with the count: continuous session, and the first and last instant.
    static constexpr std::uint64_t kOpenNs = 34'200'000'000'000ULL;  // 09:30:00
    static constexpr std::uint64_t kCloseNs = 57'600'000'000'000ULL; // 16:00:00

    void check_bbo(std::uint16_t locate, std::uint64_t ts) {
        const auto it = symbols_.find(locate);
        if (it == symbols_.end()) return;
        const RefSymbol& s = it->second;
        if (!s.has_bid() || !s.has_ask()) return;
        const bool continuous = ts >= kOpenNs && ts < kCloseNs;
        const auto ts_it = trading_state_.find(locate);
        const bool known = ts_it != trading_state_.end();
        const bool trading = known && ts_it->second.state == 'T';
        const bool awaiting = known && ts_it->second.awaiting_reopen;
        if (s.best_bid() > s.best_ask()) {
            ++counters_.crossed_observations;
            if (continuous) ++counters_.crossed_continuous;
            if (!trading) {
                ++counters_.crossed_while_not_trading;
            } else if (awaiting) {
                ++counters_.crossed_awaiting_reopen;
            } else {
                ++counters_.crossed_unexplained;
                counters_.unexplained_symbols.insert(locate);
            }
            if (counters_.crossed_first_ts == 0) counters_.crossed_first_ts = ts;
            counters_.crossed_last_ts = ts;
            const std::uint64_t depth = s.best_bid() - s.best_ask();
            if (depth > counters_.crossed_worst_ticks) counters_.crossed_worst_ticks = depth;
            counters_.crossed_symbols.insert(locate);
        } else if (s.best_bid() == s.best_ask()) {
            ++counters_.locked_observations;
            if (continuous) ++counters_.locked_continuous;
            if (!trading) {
                ++counters_.locked_while_not_trading;
            } else if (awaiting) {
                ++counters_.locked_awaiting_reopen;
            } else {
                ++counters_.locked_unexplained;
                counters_.unexplained_symbols.insert(locate);
            }
            if (counters_.locked_first_ts == 0) counters_.locked_first_ts = ts;
            counters_.locked_last_ts = ts;
            counters_.locked_symbols.insert(locate);
        } else if (awaiting) {
            // Neither crossed nor locked: the reopening has taken effect, so
            // the window closes here whether or not a cross was seen.
            ts_it->second.awaiting_reopen = false;
            const std::uint64_t held =
                ts >= ts_it->second.entered_t_ts ? ts - ts_it->second.entered_t_ts : 0;
            if (held > counters_.reopen_window_max_ns) counters_.reopen_window_max_ns = held;
        }
    }

    std::map<std::uint16_t, RefSymbol> symbols_;
    std::unordered_map<Ref, RefOrder> orders_;
    std::map<std::uint16_t, std::array<char, 8>> names_;
    RefCounters counters_;
    struct TradingState {
        unsigned char state = 0;
        bool awaiting_reopen = false;
        std::uint64_t entered_t_ts = 0;
    };
    std::unordered_map<std::uint16_t, TradingState> trading_state_;
    bool after_hours_ = false;
};

} // namespace carteret
