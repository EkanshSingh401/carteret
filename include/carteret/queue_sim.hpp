// carteret/queue_sim.hpp -- synthetic passive orders and the queue-position models.
//
// Places non-impacting synthetic limit orders at the inside and tracks, under
// four different views of the queue, how many shares sit ahead of each one and
// when it fills. Stage 7.
//
// The contribution is not the exact model -- HftBacktest ships an
// L3FIFOQueueModel and this implements the same idea (docs/design.md record
// 021). It is the *bias* of each market-by-price approximation measured
// against the exact position, on real US-equity ITCH, with the fill rules
// fixed in advance.
//
// FILL RULES. These are docs/design.md record 020, and the pre-registration
// cites that record by number. For a synthetic bid of size q at price p:
//
//   1  execution of a real order AHEAD at p -> deduct its shares. No fill.
//   2  execution of a real order BEHIND at p, including any execution at p
//      once the ahead-count is zero -> the aggressor consumed everything in
//      front of that order, which includes this one. FILL.
//   3  trade-through: an execution on the same side at a price worse than p
//      (for a bid, a resting bid below p) -> the aggressor walked through this
//      level. FILL.
//   4  non-displayed print at exactly p -> a MODELLING CHOICE, not an
//      inference. Reported both with the rule on and with it off.
//   5  no-impact double counting: when the synthetic order fills, the real
//      order that triggered the fill still executes in the replay, so
//      liquidity at p is double counted by q. Stated, not corrected.
//
// THE FOUR MODELS. All four see the same executed volume at p. They differ
// only in how a *cancel* at p is attributed, which is what the comparison
// isolates:
//
//   Exact        market-by-order. The set of order references ahead at entry
//                is known, so a cancel is attributed exactly, and rule 2 is a
//                direct observation rather than an inference.
//   Conservative market-by-price. Assumes every cancel came from behind, so
//                the ahead-count never falls except on an execution. The
//                slowest model, and the one a cautious backtest uses.
//   Optimistic   assumes every cancel came from ahead. The fastest model.
//   Proportional assumes cancels are drawn uniformly from the level, so a
//                cancel of c from a level of d reduces the ahead-count by
//                c * ahead / d.
//
// Exact is not bracketed by the other three: optimistic can still be slower
// than exact when the real cancels happened to be entirely ahead and larger
// than proportional would allow. Which way the bias runs, and how large it is,
// is the measurement.

#pragma once

#include "book_types.hpp"
#include "messages.hpp"
#include "reference_book.hpp"

#include <array>
#include <cstdint>
#include <random>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace carteret {

enum class QueueModel : std::uint8_t {
    Exact = 0,
    Conservative = 1,
    Optimistic = 2,
    Proportional = 3,
    kCount = 4,
};

inline constexpr const char* kQueueModelName[] = {"exact", "conservative", "optimistic",
                                                  "proportional"};

// Why a synthetic order stopped being live, under one model.
enum class FillReason : std::uint8_t {
    None = 0,
    ExecutionBehind = 1,   // rule 2
    TradeThrough = 2,      // rule 3
    NonDisplayedPrint = 3, // rule 4, only when enabled
    Expired = 4,           // reached its maximum life unfilled; worth zero
    LevelGone = 5,         // the price left the book entirely before filling
};

inline constexpr const char* kFillReasonName[] = {"none",          "execution_behind",
                                                  "trade_through", "non_displayed_print",
                                                  "expired",       "level_gone"};

struct QueueSimConfig {
    std::uint64_t seed = 20190130;
    // Mean gap between placements, in nanoseconds of exchange time. One order
    // every 250 ms over a 6.5-hour session is roughly 94,000 placements.
    std::uint64_t mean_interarrival_ns = 250'000'000ULL;
    Shares order_size = 100;                       // one round lot
    std::uint64_t max_life_ns = 60'000'000'000ULL; // 60 s, then cancelled
    std::array<std::uint64_t, 3> horizons_ns = {1'000'000'000ULL, 10'000'000'000ULL,
                                                60'000'000'000ULL};
    bool rule4_non_displayed_fills = false;         // reported both ways
    std::uint64_t start_ns = 34'200'000'000'000ULL; // 09:30:00
    std::uint64_t end_ns = 57'600'000'000'000ULL;   // 16:00:00
    std::size_t max_concurrent = 4096;
};

// Queue depth at entry, in shares ahead. Results are broken out by this
// because the whole question is whether position matters, and a model that
// looks unbiased in aggregate can be badly wrong at the front of the queue
// where the fills are.
inline constexpr std::array<std::uint64_t, 6> kDepthBucketEdges = {0,    100,   500,
                                                                   2000, 10000, 50000};
inline std::size_t depth_bucket(std::uint64_t ahead) noexcept {
    std::size_t i = 0;
    while (i + 1 < kDepthBucketEdges.size() && ahead >= kDepthBucketEdges[i + 1]) ++i;
    return i;
}

// Log-spaced time-to-fill buckets, two per decade from 1 ms to 100 s.
inline constexpr std::size_t kFillTimeBuckets = 12;
inline std::size_t fill_time_bucket(std::uint64_t ns) noexcept {
    double edge = 1e6; // 1 ms
    std::size_t i = 0;
    const double x = static_cast<double>(ns);
    while (i + 1 < kFillTimeBuckets && x >= edge * 3.1622776601683795) {
        edge *= 3.1622776601683795;
        ++i;
    }
    return i;
}
inline double fill_time_bucket_low_ns(std::size_t i) noexcept {
    double edge = 1e6;
    for (std::size_t k = 0; k < i; ++k) edge *= 3.1622776601683795;
    return edge;
}

// One model's view of one synthetic order.
struct ModelState {
    double ahead = 0.0; // fractional: the proportional model removes fractions
    bool live = true;
    bool filled = false;
    FillReason reason = FillReason::None;
    std::uint64_t fill_ts = 0;
};

struct SyntheticOrder {
    std::uint16_t locate = 0;
    unsigned char side = 0;
    Price price = 0;
    Shares size = 0;
    std::uint64_t entered_ts = 0;
    std::uint64_t depth_at_entry = 0; // shares ahead at entry, the exact figure
    std::uint32_t level_orders_at_entry = 0;
    std::array<ModelState, static_cast<std::size_t>(QueueModel::kCount)> models;
    // References resting ahead at entry. Exact attribution needs this and
    // nothing else: anything not in here joined behind.
    std::unordered_set<Ref> ahead_refs;
    bool any_live = true;
};

// A fill awaiting valuation: the mid is read at entry plus each horizon, so
// the value of a fill is measured against where the price went afterwards.
struct PendingValuation {
    std::uint16_t locate = 0;
    unsigned char side = 0;
    Price fill_price = 0;
    std::uint64_t due_ts = 0;
    std::uint8_t model = 0;
    std::uint8_t horizon = 0;
    std::size_t depth_bucket = 0;
};

struct ModelResults {
    std::uint64_t placed = 0;
    std::uint64_t filled = 0;
    std::array<std::uint64_t, 6> reasons{};
    std::array<std::uint64_t, kFillTimeBuckets> time_to_fill{};
    // Per depth bucket: placements, fills, and summed value at each horizon in
    // Price(4) units times shares, kept as a double because the proportional
    // model produces fractional positions.
    std::array<std::uint64_t, kDepthBucketEdges.size()> placed_by_depth{};
    std::array<std::uint64_t, kDepthBucketEdges.size()> filled_by_depth{};
    std::array<std::array<double, 3>, kDepthBucketEdges.size()> value_by_depth{};
    std::array<std::array<std::uint64_t, 3>, kDepthBucketEdges.size()> valued_by_depth{};
    std::uint64_t sum_time_to_fill_ns = 0;

    [[nodiscard]] double fill_rate() const noexcept {
        return placed ? static_cast<double>(filled) / static_cast<double>(placed) : 0.0;
    }
};

class QueueSimulator {
public:
    // How often the set of two-sided symbols is rebuilt. A full rebuild walks
    // every locate code, so it cannot run per placement.
    static constexpr std::uint64_t kUniverseRefreshNs = 60'000'000'000ULL;
    // How often expired orders are swept out of the live set.
    static constexpr std::uint64_t kSweepNs = 100'000'000ULL;

    explicit QueueSimulator(QueueSimConfig cfg = {}) : cfg_(cfg), rng_(cfg.seed), book_() {
        next_placement_ = cfg_.start_ns;
    }

    // --- handler interface -------------------------------------------------
    //
    // Every book-affecting message is resolved against the book BEFORE it is
    // applied, because after the fact the named order may be gone and with it
    // its side and price.

    void on(SystemEvent v) { book_.on(v); }
    void on(StockDirectory v) {
        book_.on(v);
        const std::string_view s = v.stock();
        if (v.locate() >= names_.size()) names_.resize(v.locate() + 1u);
        names_[v.locate()] = std::string(s);
    }

    void on(AddOrder v) {
        book_.on(v);
        tick(v.ts());
    }
    void on(AddOrderMpid v) {
        book_.on(v);
        tick(v.ts());
    }

    void on(OrderExecuted v) {
        const Resolved r = resolve(v.order_ref());
        book_.on(v);
        if (r.found) on_execution(r, v.executed_shares(), v.ts());
        tick(v.ts());
    }
    void on(OrderExecutedPrice v) {
        const Resolved r = resolve(v.order_ref());
        book_.on(v);
        if (r.found) on_execution(r, v.executed_shares(), v.ts());
        tick(v.ts());
    }
    void on(OrderCancel v) {
        const Resolved r = resolve(v.order_ref());
        book_.on(v);
        if (r.found) on_cancel(r, v.cancelled_shares(), v.ts());
        tick(v.ts());
    }
    void on(OrderDelete v) {
        const Resolved r = resolve(v.order_ref());
        book_.on(v);
        if (r.found) on_cancel(r, r.shares, v.ts());
        tick(v.ts());
    }
    void on(OrderReplace v) {
        const Resolved r = resolve(v.old_order_ref());
        book_.on(v);
        // A replace removes the old order from its level. To a market-by-price
        // observer that is indistinguishable from a cancel of the same size,
        // which is precisely why the approximations differ from exact here.
        if (r.found) on_cancel(r, r.shares, v.ts());
        tick(v.ts());
    }

    // Rule 4. A non-displayed print at exactly p suggests displayed interest
    // at p was exhausted, but 'P' carries no usable side and midpoint-pegged
    // prints trade between ticks, so this is a modelling choice reported both
    // ways rather than an inference.
    void on(Trade v) {
        if (cfg_.rule4_non_displayed_fills) {
            for (const std::size_t i : at_locate(v.locate())) {
                SyntheticOrder& o = live_[i];
                if (!o.any_live || o.price != v.price()) continue;
                fill_all_live(o, v.ts(), FillReason::NonDisplayedPrint);
            }
        }
        tick(v.ts());
    }
    void on(CrossTrade v) { tick(v.ts()); }
    void on(BrokenTrade v) { tick(v.ts()); }

    // --- results -----------------------------------------------------------

    [[nodiscard]] const std::array<ModelResults, 4>& results() const noexcept { return res_; }
    [[nodiscard]] const QueueSimConfig& config() const noexcept { return cfg_; }
    [[nodiscard]] std::uint64_t placements() const noexcept { return placements_; }
    [[nodiscard]] std::uint64_t skipped_no_inside() const noexcept { return skipped_; }
    // Placements whose scheduled instant fell so far behind the message stream
    // that the schedule was reset rather than fired in a burst.
    [[nodiscard]] std::uint64_t schedule_overruns() const noexcept { return overrun_; }

    // Retires anything still live at end of session, so the fill rate has a
    // fixed denominator rather than one that depends on when the file stops.
    void finish(std::uint64_t ts) {
        for (auto& o : live_) {
            if (!o.any_live) continue;
            expire(o, ts);
        }
        live_.clear();
    }

private:
    struct Resolved {
        bool found = false;
        Ref ref = 0;
        std::uint16_t locate = 0;
        unsigned char side = 0;
        Price price = 0;
        Shares shares = 0;
    };

    Resolved resolve(Ref ref) const {
        Resolved r;
        const RefOrder* o = book_.order(ref);
        if (!o) return r;
        r.found = true;
        r.ref = ref;
        r.locate = o->locate;
        r.side = o->side;
        r.price = o->price;
        r.shares = o->shares;
        return r;
    }

    [[nodiscard]] bool mid(std::uint16_t locate, double& out) const {
        const RefSymbol* s = book_.symbol(locate);
        if (!s || !s->has_bid() || !s->has_ask()) return false;
        out = (static_cast<double>(s->best_bid()) + static_cast<double>(s->best_ask())) / 2.0;
        return true;
    }

    // An execution at a synthetic order's own price, or worse on its side.
    void on_execution(const Resolved& r, Shares qty, std::uint64_t ts) {
        for (const std::size_t i : at_locate(r.locate)) {
            SyntheticOrder& o = live_[i];
            if (!o.any_live || o.side != r.side) continue;

            // Rule 3: an execution on the same side at a worse price means the
            // aggressor walked through this level.
            const bool worse = (o.side == kBuy) ? (r.price < o.price) : (r.price > o.price);
            if (worse) {
                fill_all_live(o, ts, FillReason::TradeThrough);
                continue;
            }
            if (r.price != o.price) continue;

            // Exact: the executed reference is either ahead or behind, and
            // which it is settles rules 1 and 2 outright.
            ModelState& exact = o.models[static_cast<std::size_t>(QueueModel::Exact)];
            if (exact.live) {
                if (o.ahead_refs.count(r.ref)) {
                    exact.ahead -= static_cast<double>(qty);
                    if (exact.ahead <= 0.0) exact.ahead = 0.0;
                    if (qty >= r.shares) o.ahead_refs.erase(r.ref);
                } else {
                    fill(o, QueueModel::Exact, ts, FillReason::ExecutionBehind);
                }
            }

            // The approximations see only volume at the price. An execution
            // consumes from the front, so it reduces the ahead-count in all of
            // them; once that count is zero the next execution at the price
            // fills this order.
            for (const QueueModel m :
                 {QueueModel::Conservative, QueueModel::Optimistic, QueueModel::Proportional}) {
                ModelState& s = o.models[static_cast<std::size_t>(m)];
                if (!s.live) continue;
                if (s.ahead <= 0.0) {
                    fill(o, m, ts, FillReason::ExecutionBehind);
                } else {
                    s.ahead -= static_cast<double>(qty);
                    if (s.ahead < 0.0) s.ahead = 0.0;
                }
            }
            refresh_live(o);
        }
    }

    // A cancel, delete or replace at a synthetic order's price. This is the
    // only event the four models treat differently, and therefore the whole
    // source of the bias being measured.
    void on_cancel(const Resolved& r, Shares qty, std::uint64_t ts) {
        (void)ts;
        for (auto& o : live_) {
            if (!o.any_live || o.locate != r.locate || o.side != r.side || o.price != r.price) {
                continue;
            }

            ModelState& exact = o.models[static_cast<std::size_t>(QueueModel::Exact)];
            if (exact.live && o.ahead_refs.count(r.ref)) {
                exact.ahead -= static_cast<double>(qty);
                if (exact.ahead < 0.0) exact.ahead = 0.0;
                if (qty >= r.shares) o.ahead_refs.erase(r.ref);
            }

            // The denominator is the level's aggregate depth as it stood
            // BEFORE this removal, which is what a market-by-price observer
            // would have seen. The book has already applied the message, so
            // the removed shares are added back rather than tracked
            // separately -- a separately tracked figure would drift, because
            // orders joining behind change the level's depth too.
            const auto d = book_.depth_at(o.locate, o.side, o.price);
            const double before = static_cast<double>(d.shares) + static_cast<double>(qty);

            // Conservative: every cancel is assumed to be behind, so the
            // ahead-count does not move.

            ModelState& opt = o.models[static_cast<std::size_t>(QueueModel::Optimistic)];
            if (opt.live) {
                opt.ahead -= static_cast<double>(qty);
                if (opt.ahead < 0.0) opt.ahead = 0.0;
            }

            ModelState& prop = o.models[static_cast<std::size_t>(QueueModel::Proportional)];
            if (prop.live && before > 0.0) {
                // A cancel of c from a level of d, with a ahead, is assumed to
                // remove c * a/d from in front. When everything on the level
                // is ahead this matches the optimistic model; when most of the
                // level joined behind it barely moves, matching conservative.
                const double share = prop.ahead / before;
                prop.ahead -= static_cast<double>(qty) * share;
                if (prop.ahead < 0.0) prop.ahead = 0.0;
            }
        }
    }

    void fill(SyntheticOrder& o, QueueModel m, std::uint64_t ts, FillReason why) {
        ModelState& s = o.models[static_cast<std::size_t>(m)];
        if (!s.live) return;
        s.live = false;
        s.filled = true;
        s.reason = why;
        s.fill_ts = ts;

        const std::size_t mi = static_cast<std::size_t>(m);
        const std::size_t db = depth_bucket(o.depth_at_entry);
        ++res_[mi].filled;
        ++res_[mi].filled_by_depth[db];
        ++res_[mi].reasons[static_cast<std::size_t>(why)];
        const std::uint64_t ttf = ts - o.entered_ts;
        res_[mi].sum_time_to_fill_ns += ttf;
        ++res_[mi].time_to_fill[fill_time_bucket(ttf)];

        for (std::uint8_t h = 0; h < 3; ++h) {
            PendingValuation p;
            p.locate = o.locate;
            p.side = o.side;
            p.fill_price = o.price;
            p.due_ts = ts + cfg_.horizons_ns[h];
            p.model = static_cast<std::uint8_t>(mi);
            p.horizon = h;
            p.depth_bucket = db;
            pending_.push_back(p);
        }
        refresh_live(o);
    }

    void fill_all_live(SyntheticOrder& o, std::uint64_t ts, FillReason why) {
        for (std::size_t m = 0; m < o.models.size(); ++m) {
            if (o.models[m].live) fill(o, static_cast<QueueModel>(m), ts, why);
        }
    }

    void expire(SyntheticOrder& o, std::uint64_t ts) {
        (void)ts;
        for (std::size_t m = 0; m < o.models.size(); ++m) {
            if (!o.models[m].live) continue;
            o.models[m].live = false;
            o.models[m].reason = FillReason::Expired;
            ++res_[m].reasons[static_cast<std::size_t>(FillReason::Expired)];
        }
        o.any_live = false;
    }

    void refresh_live(SyntheticOrder& o) {
        for (const ModelState& s : o.models) {
            if (s.live) return;
        }
        o.any_live = false;
    }

    // Called after every message: advances the placement clock, retires
    // expired orders, and settles valuations whose horizon has passed.
    void tick(std::uint64_t ts) {
        if (ts < last_ts_) return; // timestamps are monotonic; ignore any that are not
        last_ts_ = ts;
        settle(ts);

        // Expiry sweeps every live order, so it runs on a coarse interval
        // rather than per message; the maximum life is 60 s and the sweep
        // interval is 100 ms, so an order outlives its limit by at most that.
        if (ts - last_sweep_ >= kSweepNs) {
            last_sweep_ = ts;
            for (auto& o : live_) {
                if (o.any_live && ts - o.entered_ts >= cfg_.max_life_ns) expire(o, ts);
            }
            compact();
        }

        // A placement is scheduled at an instant, but the book state is only
        // defined at a message, so the order enters at the first message at or
        // after its scheduled time. With messages microseconds apart and a
        // mean interarrival of a quarter second, the two differ negligibly;
        // across a quiet gap they can differ more, and the count of placements
        // that had to catch up is reported rather than hidden.
        int catching_up = 0;
        while (next_placement_ < cfg_.end_ns && ts >= next_placement_) {
            if (++catching_up > 64) {
                ++overrun_;
                next_placement_ = ts + sample_gap();
                break;
            }
            place(ts);
            next_placement_ += sample_gap();
        }
    }

    std::uint64_t sample_gap() {
        std::exponential_distribution<double> d(1.0 /
                                                static_cast<double>(cfg_.mean_interarrival_ns));
        return static_cast<std::uint64_t>(d(rng_)) + 1u;
    }

    // Places one synthetic order: a random symbol from those currently
    // two-sided, a random side, at the inside, one round lot.
    void place(std::uint64_t ts) {
        // The set of two-sided symbols grows through the morning and changes
        // through the day, so it is rebuilt periodically rather than fixed at
        // the first placement.
        if (two_sided_.empty() || last_ts_ - universe_refreshed_ > kUniverseRefreshNs) {
            refresh_universe();
        }
        if (two_sided_.empty()) {
            ++skipped_;
            return;
        }
        const std::uint16_t locate = two_sided_[std::uniform_int_distribution<std::size_t>(
            0, two_sided_.size() - 1)(rng_)];
        const RefSymbol* sym = book_.symbol(locate);
        if (!sym || !sym->has_bid() || !sym->has_ask()) {
            ++skipped_;
            return;
        }

        SyntheticOrder o;
        o.locate = locate;
        o.side = (rng_() & 1u) ? kBuy : kSell;
        o.price = (o.side == kBuy) ? sym->best_bid() : sym->best_ask();
        o.size = cfg_.order_size;
        o.entered_ts = ts;

        const LevelSnapshot lv = book_.level(locate, o.side, o.price);
        if (!lv.present) {
            ++skipped_;
            return;
        }
        o.depth_at_entry = lv.shares;
        o.level_orders_at_entry = lv.orders;
        o.ahead_refs.insert(lv.fifo.begin(), lv.fifo.end());
        for (ModelState& s : o.models) s.ahead = static_cast<double>(lv.shares);

        const std::size_t db = depth_bucket(o.depth_at_entry);
        for (std::size_t m = 0; m < res_.size(); ++m) {
            ++res_[m].placed;
            ++res_[m].placed_by_depth[db];
        }
        ++placements_;
        by_locate_[locate].push_back(live_.size());
        live_.push_back(std::move(o));
    }

    // Live synthetic orders on one symbol. Empty for the overwhelming majority
    // of messages, which is the point: without this index every message would
    // scan every live order.
    [[nodiscard]] const std::vector<std::size_t>& at_locate(std::uint16_t locate) const {
        static const std::vector<std::size_t> kEmpty;
        const auto it = by_locate_.find(locate);
        return it == by_locate_.end() ? kEmpty : it->second;
    }

    void refresh_universe() {
        two_sided_.clear();
        for (std::uint32_t l = 0; l <= 0xFFFF; ++l) {
            const RefSymbol* s = book_.symbol(static_cast<std::uint16_t>(l));
            if (s && s->has_bid() && s->has_ask()) {
                two_sided_.push_back(static_cast<std::uint16_t>(l));
            }
        }
        universe_refreshed_ = last_ts_;
    }

    void compact() {
        std::vector<SyntheticOrder> keep;
        keep.reserve(live_.size());
        for (auto& o : live_) {
            if (o.any_live) keep.push_back(std::move(o));
        }
        live_.swap(keep);
        by_locate_.clear();
        for (std::size_t i = 0; i < live_.size(); ++i) by_locate_[live_[i].locate].push_back(i);
    }

    // Values a fill against the mid at its horizon. A buy is worth the mid
    // minus what was paid; a sell the reverse. Unfilled orders are worth zero
    // and are simply absent from the value totals.
    void settle(std::uint64_t ts) {
        if (pending_.empty()) return;
        std::vector<PendingValuation> still;
        still.reserve(pending_.size());
        for (const PendingValuation& p : pending_) {
            if (p.due_ts > ts) {
                still.push_back(p);
                continue;
            }
            double m = 0.0;
            if (!mid(p.locate, m)) continue; // no two-sided book: not valued
            const double edge = (p.side == kBuy) ? (m - static_cast<double>(p.fill_price))
                                                 : (static_cast<double>(p.fill_price) - m);
            res_[p.model].value_by_depth[p.depth_bucket][p.horizon] += edge;
            ++res_[p.model].valued_by_depth[p.depth_bucket][p.horizon];
        }
        pending_.swap(still);
    }

    QueueSimConfig cfg_;
    std::mt19937_64 rng_;
    ReferenceBook book_;
    std::vector<std::string> names_;
    std::vector<SyntheticOrder> live_;
    std::unordered_map<std::uint16_t, std::vector<std::size_t>> by_locate_;
    std::vector<PendingValuation> pending_;
    std::vector<std::uint16_t> two_sided_;
    std::array<ModelResults, 4> res_{};
    std::uint64_t next_placement_ = 0;
    std::uint64_t last_ts_ = 0;
    std::uint64_t universe_refreshed_ = 0;
    std::uint64_t placements_ = 0;
    std::uint64_t skipped_ = 0;
    std::uint64_t overrun_ = 0;
    std::uint64_t last_sweep_ = 0;
};

} // namespace carteret
