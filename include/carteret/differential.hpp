// carteret/differential.hpp -- message-by-message comparison of the two books.
//
// Correctness layer 4. The reference book (record 014) and the fast book are
// driven by the same message stream, and after every message the harness
// compares:
//
//   - the level the message touched: aggregate shares, order count, and the
//     full sequence of order references in FIFO order. Aggregates alone would
//     miss a queue-ordering defect, which is exactly the quantity the queue
//     study depends on.
//   - the best bid and offer on both sides of the affected symbol.
//
// Every N messages, and again at end of session, it compares a hash of the
// complete book state on both sides, which catches a divergence at a level no
// message has touched since it appeared.
//
// On the first divergence it reports the message index, the message bytes, the
// symbol, the price and both books' view of the level, then stops. A harness
// that continued would report thousands of consequences of one cause.
//
// This proves the two agree. It does not prove the reference book is right;
// layers 1, 2, 3 and 5 are what constrain that.

#pragma once

#include "fast_book.hpp"
#include "messages.hpp"
#include "parser.hpp"
#include "reference_book.hpp"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace carteret {

struct Divergence {
    bool found = false;
    std::uint64_t message_index = 0;
    unsigned char message_type = 0;
    std::uint16_t locate = 0;
    unsigned char side = 0;
    Price price = 0;
    std::string what;
    std::string reference;
    std::string fast;
};

struct DifferentialStats {
    std::uint64_t messages = 0;
    std::uint64_t level_comparisons = 0;
    std::uint64_t bbo_comparisons = 0;
    std::uint64_t full_comparisons = 0;
    std::uint64_t invariant_checks = 0;
    std::uint64_t invariant_violations = 0;
};

// Drives both books and compares them. Constructed with the fast book's
// configuration so that a sizing failure is visible as a divergence rather
// than as a silently smaller book.
// The Book parameter exists so that the comparison can be tested against a
// book that is deliberately wrong. A gate that has never been seen to fail is
// not known to work, and the only way to make this one fail is to give it two
// books that disagree. tests/test_gate_negatives.cpp substitutes a wrapper
// that drops one removal; every other use takes the default and is unchanged.
template<class HashPolicy = MultiplyShiftHash, class Book = FastBook<HashPolicy>>
class Differential {
public:
    // invariant_every: how often the structural invariants of both books are
    // checked during the replay. The check walks every level of every symbol,
    // so it cannot run per message on a full session; debug builds default to
    // a small interval and release builds to a large one. Zero checks only at
    // the end. See docs/correctness.md, layer 5.
#ifdef NDEBUG
    static constexpr std::uint64_t kDefaultInvariantEvery = 1u << 24;
#else
    static constexpr std::uint64_t kDefaultInvariantEvery = 1u << 16;
#endif

    explicit Differential(FastBookConfig cfg = {}, std::uint64_t full_compare_every = 1u << 20,
                          std::uint64_t invariant_every = kDefaultInvariantEvery)
        : fast_(cfg), full_every_(full_compare_every), invariant_every_(invariant_every) {}

    // --- handler interface: apply to both, then compare --------------------

    void on(SystemEvent v) {
        ref_.on(v);
        fast_.on(v);
        ++stats_.messages;
    }
    void on(StockDirectory v) {
        ref_.on(v);
        ++stats_.messages;
    }

    // 'H' changes no book state, so it is forwarded to the reference book
    // only, which retains the trading state. That state is what separates a
    // crossed book the venue allowed -- because it was not matching the
    // symbol -- from one this reconstruction produced.
    void on(StockTradingAction v) {
        ref_.on(v);
        ++stats_.messages;
    }

    // 'h' changes no book state either; the reference book uses it for the
    // same reason it uses 'H'.
    void on(OperationalHalt v) {
        ref_.on(v);
        ++stats_.messages;
    }

    void on(AddOrder v) {
        if (done()) return;
        apply(v);
        compare_level(v.type(), v.locate(), v.side(), v.price());
        compare_bbo(v.type(), v.locate());
        periodic(v.type());
    }
    void on(AddOrderMpid v) {
        if (done()) return;
        apply(v);
        compare_level(v.type(), v.locate(), v.side(), v.price());
        compare_bbo(v.type(), v.locate());
        periodic(v.type());
    }

    // A modify names a reference, not a price, so the touched level has to be
    // resolved from the reference BEFORE the message is applied. Afterwards
    // the order may be gone and the level with it.
    void on(OrderExecuted v) { modify(v, v.order_ref()); }
    void on(OrderExecutedPrice v) { modify(v, v.order_ref()); }
    void on(OrderCancel v) { modify(v, v.order_ref()); }
    void on(OrderDelete v) { modify(v, v.order_ref()); }

    // A replace touches two levels: the old order's and the new price's.
    void on(OrderReplace v) {
        if (done()) return;
        unsigned char side = 0;
        Price old_price = 0;
        const bool known = locate_of(v.old_order_ref(), side, old_price);
        apply(v);
        if (known) compare_level(v.type(), v.locate(), side, old_price);
        compare_level(v.type(), v.locate(), side, v.price());
        compare_bbo(v.type(), v.locate());
        periodic(v.type());
    }

    void on(Trade v) {
        if (done()) return;
        apply(v);
        compare_bbo(v.type(), v.locate());
        periodic(v.type());
    }
    void on(CrossTrade v) {
        if (done()) return;
        apply(v);
        periodic(v.type());
    }
    void on(BrokenTrade v) {
        if (done()) return;
        apply(v);
        periodic(v.type());
    }

    // --- results -----------------------------------------------------------

    [[nodiscard]] const Divergence& divergence() const noexcept { return div_; }
    [[nodiscard]] const DifferentialStats& stats() const noexcept { return stats_; }
    // Which venue is being replayed, for 'h' Operational Halt market-code
    // matching. Set before the run; see ReferenceBook.
    void set_venue_market_code(unsigned char c) noexcept { ref_.set_venue_market_code(c); }

    [[nodiscard]] const ReferenceBook& reference() const noexcept { return ref_; }
    [[nodiscard]] const Book& fast() const noexcept { return fast_; }

    // Run at end of session. Compares every level of every symbol, so a
    // divergence that no message touched again is still caught.
    bool compare_everything() {
        if (div_.found) return false;
        ++stats_.full_comparisons;
        for (std::uint16_t locate = 0; locate < 0xFFFF; ++locate) {
            const RefSymbol* sym = ref_.symbol(locate);
            if (!sym) continue;
            for (const auto& [price, lvl] : sym->bids) {
                (void)lvl;
                if (!compare_level_now(0, locate, kBuy, price)) return false;
            }
            for (const auto& [price, lvl] : sym->asks) {
                (void)lvl;
                if (!compare_level_now(0, locate, kSell, price)) return false;
            }
            if (!compare_bbo_now(0, locate)) return false;
        }
        // The live order populations must match exactly: a reference present
        // in one book and absent from the other is a defect even when no level
        // shows it.
        if (ref_.live_orders() != fast_.live_orders()) {
            record(0, 0, 0, 0, "live order count", std::to_string(ref_.live_orders()),
                   std::to_string(fast_.live_orders()));
            return false;
        }
        return true;
    }

    void report(std::FILE* out) const {
        if (!div_.found) {
            std::fprintf(out, "no divergence over %llu messages\n",
                         (unsigned long long)stats_.messages);
            return;
        }
        std::fprintf(out, "DIVERGENCE at message %llu (type '%c')\n",
                     (unsigned long long)div_.message_index,
                     div_.message_type ? div_.message_type : '?');
        std::fprintf(out, "  locate    %u\n", unsigned(div_.locate));
        std::fprintf(out, "  side      %c\n", div_.side ? div_.side : '?');
        std::fprintf(out, "  price     %u\n", unsigned(div_.price));
        std::fprintf(out, "  field     %s\n", div_.what.c_str());
        std::fprintf(out, "  reference %s\n", div_.reference.c_str());
        std::fprintf(out, "  fast      %s\n", div_.fast.c_str());
    }

private:
    [[nodiscard]] bool done() const noexcept { return div_.found; }

    template<class V>
    void apply(V v) {
        ref_.on(v);
        fast_.on(v);
        ++stats_.messages;
    }

    template<class V>
    void modify(V v, Ref ref) {
        if (done()) return;
        unsigned char side = 0;
        Price price = 0;
        const bool known = locate_of(ref, side, price);
        apply(v);
        if (known) compare_level(v.type(), v.locate(), side, price);
        compare_bbo(v.type(), v.locate());
        periodic(v.type());
    }

    // Resolves a reference through the reference book, which is the oracle. If
    // the fast book disagrees about whether the reference exists, the level
    // comparison that follows will catch it.
    bool locate_of(Ref ref, unsigned char& side, Price& price) const {
        const RefOrder* o = ref_.order(ref);
        if (!o) return false;
        side = o->side;
        price = o->price;
        return true;
    }

    void periodic(unsigned char type) {
        (void)type;
        if (invariant_every_ != 0 && stats_.messages % invariant_every_ == 0) {
            check_invariants_now();
        }
        if (full_every_ == 0) return;
        if (stats_.messages % full_every_ != 0) return;
        ++stats_.full_comparisons;
        compare_everything();
    }

    // Structural invariants are internal consistency, which is distinct from
    // the two books agreeing: both could agree and both be self-inconsistent,
    // and the comparison alone would not show it.
    void check_invariants_now() {
        if (div_.found) return;
        ++stats_.invariant_checks;
        const std::size_t rb = ref_.check_invariants();
        const std::size_t fb = fast_.check_invariants();
        stats_.invariant_violations += rb + fb;
        if (rb || fb) {
            record(0, 0, 0, 0, "invariant violations", std::to_string(rb), std::to_string(fb));
        }
    }

    void compare_level(unsigned char type, std::uint16_t locate, unsigned char side,
                       Price price) {
        compare_level_now(type, locate, side, price);
    }

    bool compare_level_now(unsigned char type, std::uint16_t locate, unsigned char side,
                           Price price) {
        if (div_.found) return false;
        ++stats_.level_comparisons;
        const LevelSnapshot r = ref_.level(locate, side, price);
        const FastLevelSnapshot f = fast_.level(locate, side, price);

        if (r.present != f.present) {
            record(type, locate, side, price, "level present", r.present ? "yes" : "no",
                   f.present ? "yes" : "no");
            return false;
        }
        if (!r.present) return true;
        if (r.shares != f.shares) {
            record(type, locate, side, price, "level shares", std::to_string(r.shares),
                   std::to_string(f.shares));
            return false;
        }
        if (r.orders != f.orders) {
            record(type, locate, side, price, "level order count", std::to_string(r.orders),
                   std::to_string(f.orders));
            return false;
        }
        // Queue composition, not just depth. A book that matches on aggregates
        // and not on FIFO order is wrong for every queue-position result.
        if (r.fifo != f.fifo) {
            record(type, locate, side, price, "level FIFO order", join(r.fifo), join(f.fifo));
            return false;
        }
        return true;
    }

    void compare_bbo(unsigned char type, std::uint16_t locate) {
        compare_bbo_now(type, locate);
    }

    bool compare_bbo_now(unsigned char type, std::uint16_t locate) {
        if (div_.found) return false;
        ++stats_.bbo_comparisons;
        const RefSymbol* sym = ref_.symbol(locate);
        const bool rb = sym && sym->has_bid();
        const bool ra = sym && sym->has_ask();
        const bool fb = fast_.has_bid(locate);
        const bool fa = fast_.has_ask(locate);

        if (rb != fb) {
            record(type, locate, kBuy, 0, "has bid", rb ? "yes" : "no", fb ? "yes" : "no");
            return false;
        }
        if (ra != fa) {
            record(type, locate, kSell, 0, "has ask", ra ? "yes" : "no", fa ? "yes" : "no");
            return false;
        }
        if (rb && sym->best_bid() != fast_.best_bid(locate)) {
            record(type, locate, kBuy, sym->best_bid(), "best bid",
                   std::to_string(sym->best_bid()), std::to_string(fast_.best_bid(locate)));
            return false;
        }
        if (ra && sym->best_ask() != fast_.best_ask(locate)) {
            record(type, locate, kSell, sym->best_ask(), "best ask",
                   std::to_string(sym->best_ask()), std::to_string(fast_.best_ask(locate)));
            return false;
        }
        return true;
    }

    static std::string join(const std::vector<Ref>& v) {
        std::string s;
        for (std::size_t i = 0; i < v.size(); ++i) {
            if (i) s += ',';
            s += std::to_string(v[i]);
            if (i >= 15 && v.size() > 16) {
                s += ",... (";
                s += std::to_string(v.size());
                s += " total)";
                break;
            }
        }
        return s.empty() ? "(empty)" : s;
    }

    void record(unsigned char type, std::uint16_t locate, unsigned char side, Price price,
                const char* what, std::string r, std::string f) {
        div_.found = true;
        div_.message_index = stats_.messages;
        div_.message_type = type;
        div_.locate = locate;
        div_.side = side;
        div_.price = price;
        div_.what = what;
        div_.reference = std::move(r);
        div_.fast = std::move(f);
    }

    ReferenceBook ref_;
    Book fast_;
    Divergence div_;
    DifferentialStats stats_;
    std::uint64_t full_every_;
    std::uint64_t invariant_every_;
};

} // namespace carteret
