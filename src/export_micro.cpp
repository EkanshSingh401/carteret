// export_micro -- per-symbol microstructure aggregates from a session.
//
// Stage 6 input. Writes compact CSV aggregates rather than a raw event stream:
// a session has 7.5e7 book messages, and the questions being asked -- spread
// and depth by time of day, queue lifetimes, cancel-to-trade ratios, order
// size distributions -- are all answerable from counts and histograms. Raw
// export would be tens of gigabytes of data that must never be committed
// anyway.
//
// Two passes over the mapped file:
//
//   1  count book messages per stock locate and resolve locate to symbol from
//      the Stock Directory messages, then pick the busiest N symbols.
//   2  rebuild the book, sampling the inside of those N symbols on a fixed
//      exchange-time interval, and accumulate the distributions.
//
// The symbol universe is chosen from the data rather than from a hardcoded
// list of well-known tickers, so the selection is reproducible on any session
// and does not smuggle in a survivorship assumption.
//
// Distributions are accumulated as histograms in C++ rather than exported as
// samples, so the output is a few hundred kilobytes and the Python side does
// no heavy lifting.
//
//   usage: export_micro [--symbols N] [--sample-ms N] [--out DIR] <session-file>
//
// Output is written to DIR and is gitignored: it derives from market data.

#include "carteret/fast_book.hpp"
#include "carteret/mapped_file.hpp"
#include "carteret/parser.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

using namespace carteret;

namespace {

constexpr std::uint64_t kNs = 1000000000ULL;
constexpr std::uint64_t kOpenNs = 34200ULL * kNs;  // 09:30:00
constexpr std::uint64_t kCloseNs = 57600ULL * kNs; // 16:00:00

// Log-spaced lifetime buckets. An order's life spans nanoseconds to hours, so
// linear buckets would put almost everything in the first one.
constexpr std::size_t kLifetimeBuckets = 24;
std::size_t lifetime_bucket(std::uint64_t ns) noexcept {
    // Bucket i covers [10^(i/2), 10^((i+1)/2)) nanoseconds, so two buckets per
    // decade: 1ns, 3ns, 10ns ... up to ~1e12 ns (about 17 minutes) and above.
    if (ns == 0) return 0;
    double x = static_cast<double>(ns);
    std::size_t i = 0;
    double edge = 1.0;
    while (i + 1 < kLifetimeBuckets && x >= edge * 3.1622776601683795) {
        edge *= 3.1622776601683795;
        ++i;
    }
    return i;
}
double lifetime_bucket_low_ns(std::size_t i) noexcept {
    double edge = 1.0;
    for (std::size_t k = 0; k < i; ++k) edge *= 3.1622776601683795;
    return edge;
}

// How an order left the book. A replace is distinct from a cancel: the order
// is gone, but the interest is not, so pooling them would overstate the
// cancellation rate.
enum class Exit : std::uint8_t {
    Filled = 0,
    PartiallyFilledThenGone = 1,
    Cancelled = 2,
    Replaced = 3,
    kCount = 4
};

constexpr const char* kExitName[] = {"filled", "partial_then_gone", "cancelled", "replaced"};

struct PerSymbol {
    std::string symbol;
    std::uint64_t messages = 0;
    std::uint64_t adds = 0, executes = 0, cancels = 0, deletes = 0, replaces = 0, trades = 0;
    std::uint64_t shares_added = 0, shares_executed = 0, shares_cancelled = 0;
    std::uint64_t shares_traded_nondisplayed = 0;
};

// Spread and depth accumulated per minute of the trading day.
struct MinuteBucket {
    std::uint64_t samples = 0;
    std::uint64_t spread_ticks_sum = 0;
    std::uint64_t bid_shares_sum = 0;
    std::uint64_t ask_shares_sum = 0;
    std::uint64_t bid_orders_sum = 0;
    std::uint64_t ask_orders_sum = 0;
    std::uint64_t one_tick_spreads = 0;
    std::uint64_t crossed_or_locked = 0;
};

struct Tracked {
    std::uint64_t added_ts = 0;
    std::uint32_t original_shares = 0;
    std::uint32_t remaining = 0;
    std::uint32_t filled = 0; // separates a cancel from a partial fill then cancel
    std::uint16_t locate = 0;
};

class Exporter {
public:
    Exporter(std::vector<std::uint16_t> universe, std::uint64_t sample_ns, FastBookConfig cfg)
        : book_(cfg), sample_ns_(sample_ns) {
        selected_.assign(1u << 16, false);
        for (const std::uint16_t l : universe) selected_[l] = true;
        minutes_.resize(universe.size());
        for (std::size_t i = 0; i < universe.size(); ++i) universe_index_[universe[i]] = i;
        universe_ = std::move(universe);
        for (auto& m : minutes_) m.assign(390, MinuteBucket{}); // 09:30-16:00
    }

    void on(SystemEvent v) { book_.on(v); }

    void on(StockDirectory v) {
        auto& s = sym_[v.locate()];
        s.symbol = std::string(v.stock());
    }

    void on(AddOrder v) {
        add(v.locate(), v.order_ref(), v.side(), v.price(), v.shares(), v.ts());
        book_.on(v);
        sample(v.ts());
    }
    void on(AddOrderMpid v) {
        add(v.locate(), v.order_ref(), v.side(), v.price(), v.shares(), v.ts());
        book_.on(v);
        sample(v.ts());
    }

    void on(OrderExecuted v) {
        reduce(v.locate(), v.order_ref(), v.executed_shares(), v.ts(), true);
        ++sym_[v.locate()].executes;
        sym_[v.locate()].shares_executed += v.executed_shares();
        book_.on(v);
        sample(v.ts());
    }
    void on(OrderExecutedPrice v) {
        reduce(v.locate(), v.order_ref(), v.executed_shares(), v.ts(), true);
        ++sym_[v.locate()].executes;
        sym_[v.locate()].shares_executed += v.executed_shares();
        book_.on(v);
        sample(v.ts());
    }
    void on(OrderCancel v) {
        reduce(v.locate(), v.order_ref(), v.cancelled_shares(), v.ts(), false);
        ++sym_[v.locate()].cancels;
        sym_[v.locate()].shares_cancelled += v.cancelled_shares();
        book_.on(v);
        sample(v.ts());
    }
    void on(OrderDelete v) {
        close_out(v.order_ref(), v.ts(), Exit::Cancelled);
        ++sym_[v.locate()].deletes;
        book_.on(v);
        sample(v.ts());
    }
    void on(OrderReplace v) {
        const auto it = live_.find(v.old_order_ref());
        std::uint16_t locate = v.locate();
        if (it != live_.end()) {
            locate = it->second.locate;
            sym_[locate].shares_cancelled += it->second.remaining;
            close_out(v.old_order_ref(), v.ts(), Exit::Replaced);
        }
        ++sym_[locate].replaces;
        book_.on(v);
        // The replacement's side is unknown to the exporter, which does not
        // track it; the book has it, and the lifetime record only needs the
        // add time and size.
        Tracked t;
        t.added_ts = v.ts();
        t.original_shares = v.shares();
        t.remaining = v.shares();
        t.locate = locate;
        live_[v.new_order_ref()] = t;
        record_size(v.shares());
        sym_[locate].shares_added += v.shares();
        sample(v.ts());
    }

    void on(Trade v) {
        ++sym_[v.locate()].trades;
        sym_[v.locate()].shares_traded_nondisplayed += v.shares();
        sample(v.ts());
    }
    void on(CrossTrade v) { sample(v.ts()); }
    void on(BrokenTrade v) { sample(v.ts()); }

    void write(const std::string& dir) const {
        write_symbols(dir + "/micro_symbols.csv");
        write_spread_depth(dir + "/micro_spread_depth.csv");
        write_lifetimes(dir + "/micro_lifetimes.csv");
        write_sizes(dir + "/micro_order_sizes.csv");
    }

    [[nodiscard]] std::size_t still_live() const noexcept { return live_.size(); }

private:
    void add(std::uint16_t locate, Ref ref, unsigned char side, Price price, Shares shares,
             std::uint64_t ts) {
        (void)side;
        (void)price;
        auto& s = sym_[locate];
        ++s.adds;
        s.shares_added += shares;
        Tracked t;
        t.added_ts = ts;
        t.original_shares = shares;
        t.remaining = shares;
        t.locate = locate;
        live_[ref] = t;
        record_size(shares);
    }

    void record_size(std::uint32_t shares) {
        // Round lots dominate, so the distribution is kept at share
        // granularity up to 1,000 and in 100-share bins above that. A pure
        // log scale would lose the round-lot spikes that are the interesting
        // feature.
        if (shares < 1000)
            ++size_small_[shares];
        else if (shares < 100000)
            ++size_large_[shares / 100];
        else
            ++size_over_;
    }

    void reduce(std::uint16_t locate, Ref ref, std::uint32_t qty, std::uint64_t ts,
                bool executed) {
        (void)locate;
        const auto it = live_.find(ref);
        if (it == live_.end()) return;
        Tracked& t = it->second;
        const std::uint32_t taken = std::min(qty, t.remaining);
        t.remaining -= taken;
        if (executed) t.filled += taken;
        if (t.remaining == 0) {
            const Exit e = executed
                               ? (t.filled == t.original_shares ? Exit::Filled
                                                                : Exit::PartiallyFilledThenGone)
                               : Exit::Cancelled;
            close_out(ref, ts, e);
        }
    }

    void close_out(Ref ref, std::uint64_t ts, Exit e) {
        const auto it = live_.find(ref);
        if (it == live_.end()) return;
        const std::uint64_t life = ts > it->second.added_ts ? ts - it->second.added_ts : 0;
        // An order partly filled and then cancelled is counted as cancelled,
        // but its fill is still visible in the per-symbol share totals.
        Exit eff = e;
        if (e == Exit::Cancelled && it->second.filled > 0) eff = Exit::PartiallyFilledThenGone;
        lifetimes_[static_cast<std::size_t>(eff)][lifetime_bucket(life)]++;
        live_.erase(it);
    }

    // Samples the inside of every selected symbol whenever exchange time
    // crosses the next interval boundary. Sampling on message arrival instead
    // would weight busy symbols and busy minutes, which is exactly the bias a
    // time-of-day profile must not have.
    void sample(std::uint64_t ts) {
        if (ts < kOpenNs || ts >= kCloseNs) return;
        if (next_sample_ == 0) next_sample_ = ts;
        if (ts < next_sample_) return;
        while (next_sample_ <= ts) next_sample_ += sample_ns_;

        const std::size_t minute = static_cast<std::size_t>((ts - kOpenNs) / (60 * kNs));
        if (minute >= 390) return;

        for (std::size_t i = 0; i < universe_.size(); ++i) {
            const std::uint16_t locate = universe_[i];
            if (!book_.has_bid(locate) || !book_.has_ask(locate)) continue;
            const Price bid = book_.best_bid(locate);
            const Price ask = book_.best_ask(locate);
            MinuteBucket& m = minutes_[i][minute];
            ++m.samples;
            if (ask <= bid) {
                ++m.crossed_or_locked;
                continue;
            }
            const std::uint64_t spread_ticks = (ask - bid) / 100u;
            m.spread_ticks_sum += spread_ticks;
            if (spread_ticks == 1) ++m.one_tick_spreads;
            const auto db = book_.depth_at(locate, kBuy, bid);
            const auto da = book_.depth_at(locate, kSell, ask);
            m.bid_shares_sum += db.shares;
            m.ask_shares_sum += da.shares;
            m.bid_orders_sum += db.orders;
            m.ask_orders_sum += da.orders;
        }
    }

    FastBook<MultiplyShiftHash> book_;
    std::uint64_t sample_ns_;
    std::uint64_t next_sample_ = 0;

    std::vector<bool> selected_;
    std::vector<std::uint16_t> universe_;
    std::unordered_map<std::uint16_t, std::size_t> universe_index_;
    std::vector<std::vector<MinuteBucket>> minutes_;

    mutable std::unordered_map<std::uint16_t, PerSymbol> sym_;
    std::unordered_map<Ref, Tracked> live_;

    std::array<std::array<std::uint64_t, kLifetimeBuckets>,
               static_cast<std::size_t>(Exit::kCount)>
        lifetimes_{};
    std::array<std::uint64_t, 1000> size_small_{};
    std::array<std::uint64_t, 1000> size_large_{};
    std::uint64_t size_over_ = 0;

    static std::FILE* open_or_die(const std::string& path) {
        std::FILE* f = std::fopen(path.c_str(), "w");
        if (!f) {
            std::fprintf(stderr, "cannot write %s\n", path.c_str());
            std::exit(1);
        }
        return f;
    }

    void write_symbols(const std::string& path) const {
        std::FILE* f = open_or_die(path);
        std::fprintf(f, "symbol,locate,in_universe,adds,executes,cancels,deletes,replaces,"
                        "trades,shares_added,shares_executed,shares_cancelled,"
                        "shares_nondisplayed\n");
        for (const auto& [locate, s] : sym_) {
            if (s.symbol.empty()) continue;
            std::fprintf(f, "%s,%u,%d,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n",
                         s.symbol.c_str(), unsigned(locate),
                         universe_index_.count(locate) ? 1 : 0, (unsigned long long)s.adds,
                         (unsigned long long)s.executes, (unsigned long long)s.cancels,
                         (unsigned long long)s.deletes, (unsigned long long)s.replaces,
                         (unsigned long long)s.trades, (unsigned long long)s.shares_added,
                         (unsigned long long)s.shares_executed,
                         (unsigned long long)s.shares_cancelled,
                         (unsigned long long)s.shares_traded_nondisplayed);
        }
        std::fclose(f);
    }

    void write_spread_depth(const std::string& path) const {
        std::FILE* f = open_or_die(path);
        std::fprintf(f, "symbol,minute_of_session,samples,two_sided_samples,"
                        "mean_spread_ticks,one_tick_fraction,mean_bid_shares,"
                        "mean_ask_shares,mean_bid_orders,mean_ask_orders,"
                        "crossed_or_locked\n");
        for (std::size_t i = 0; i < universe_.size(); ++i) {
            const auto it = sym_.find(universe_[i]);
            if (it == sym_.end() || it->second.symbol.empty()) continue;
            for (std::size_t m = 0; m < minutes_[i].size(); ++m) {
                const MinuteBucket& b = minutes_[i][m];
                if (b.samples == 0) continue;
                const std::uint64_t two_sided = b.samples - b.crossed_or_locked;
                const double d = two_sided ? static_cast<double>(two_sided) : 1.0;
                std::fprintf(f, "%s,%zu,%llu,%llu,%.4f,%.4f,%.1f,%.1f,%.3f,%.3f,%llu\n",
                             it->second.symbol.c_str(), m, (unsigned long long)b.samples,
                             (unsigned long long)two_sided,
                             static_cast<double>(b.spread_ticks_sum) / d,
                             static_cast<double>(b.one_tick_spreads) / d,
                             static_cast<double>(b.bid_shares_sum) / d,
                             static_cast<double>(b.ask_shares_sum) / d,
                             static_cast<double>(b.bid_orders_sum) / d,
                             static_cast<double>(b.ask_orders_sum) / d,
                             (unsigned long long)b.crossed_or_locked);
            }
        }
        std::fclose(f);
    }

    void write_lifetimes(const std::string& path) const {
        std::FILE* f = open_or_die(path);
        std::fprintf(f, "exit,bucket,low_ns,high_ns,count\n");
        for (std::size_t e = 0; e < static_cast<std::size_t>(Exit::kCount); ++e) {
            for (std::size_t b = 0; b < kLifetimeBuckets; ++b) {
                if (lifetimes_[e][b] == 0) continue;
                std::fprintf(f, "%s,%zu,%.0f,%.0f,%llu\n", kExitName[e], b,
                             lifetime_bucket_low_ns(b), lifetime_bucket_low_ns(b + 1),
                             (unsigned long long)lifetimes_[e][b]);
            }
        }
        std::fclose(f);
    }

    void write_sizes(const std::string& path) const {
        std::FILE* f = open_or_die(path);
        std::fprintf(f, "shares_low,shares_high,count\n");
        for (std::size_t i = 0; i < size_small_.size(); ++i) {
            if (size_small_[i]) {
                std::fprintf(f, "%zu,%zu,%llu\n", i, i + 1, (unsigned long long)size_small_[i]);
            }
        }
        for (std::size_t i = 10; i < size_large_.size(); ++i) {
            if (size_large_[i]) {
                std::fprintf(f, "%zu,%zu,%llu\n", i * 100, (i + 1) * 100,
                             (unsigned long long)size_large_[i]);
            }
        }
        if (size_over_) {
            std::fprintf(f, "100000,,%llu\n", (unsigned long long)size_over_);
        }
        std::fclose(f);
    }
};

// Pass 1: how busy each symbol is, and what each locate code means. Counting
// first means the symbol universe is chosen from this session's own activity
// rather than from a list of tickers someone expected to be liquid.
struct Census {
    std::unordered_map<std::uint16_t, std::uint64_t> messages;
    std::unordered_map<std::uint16_t, std::string> names;

    void on(StockDirectory v) { names[v.locate()] = std::string(v.stock()); }
    void on(AddOrder v) { ++messages[v.locate()]; }
    void on(AddOrderMpid v) { ++messages[v.locate()]; }
    void on(OrderExecuted v) { ++messages[v.locate()]; }
    void on(OrderExecutedPrice v) { ++messages[v.locate()]; }
    void on(OrderCancel v) { ++messages[v.locate()]; }
    void on(OrderDelete v) { ++messages[v.locate()]; }
    void on(OrderReplace v) { ++messages[v.locate()]; }
};

} // namespace

int main(int argc, char** argv) {
    std::string path;
    std::string out = "results/micro";
    std::size_t n_symbols = 50;
    std::uint64_t sample_ms = 1000;

    for (int i = 1; i < argc; ++i) {
        const auto next = [&](const char* flag) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s needs a value\n", flag);
                std::exit(2);
            }
            return argv[++i];
        };
        if (std::strcmp(argv[i], "--symbols") == 0)
            n_symbols = std::strtoull(next("--symbols"), nullptr, 10);
        else if (std::strcmp(argv[i], "--sample-ms") == 0)
            sample_ms = std::strtoull(next("--sample-ms"), nullptr, 10);
        else if (std::strcmp(argv[i], "--out") == 0)
            out = next("--out");
        else
            path = argv[i];
    }
    if (path.empty()) {
        std::fprintf(stderr,
                     "usage: %s [--symbols N] [--sample-ms N] [--out DIR] <session-file>\n",
                     argv[0]);
        return 2;
    }

    MappedFile mf(path);

    Census c;
    {
        Parser<Census> parser(c);
        parser.run(mf.bytes());
    }
    std::vector<std::pair<std::uint64_t, std::uint16_t>> ranked;
    ranked.reserve(c.messages.size());
    for (const auto& [locate, n] : c.messages) {
        if (c.names.count(locate)) ranked.emplace_back(n, locate);
    }
    std::sort(ranked.begin(), ranked.end(), std::greater<>());
    if (ranked.size() > n_symbols) ranked.resize(n_symbols);

    std::vector<std::uint16_t> universe;
    universe.reserve(ranked.size());
    for (const auto& [n, locate] : ranked) universe.push_back(locate);

    std::printf("session           %s\n", path.c_str());
    std::printf("symbols in file   %zu\n", c.names.size());
    std::printf("universe          %zu busiest by book message count\n", universe.size());
    std::printf("sample interval   %llu ms of exchange time\n", (unsigned long long)sample_ms);
    if (!universe.empty()) {
        std::printf("busiest           %s (%llu messages)\n", c.names[universe.front()].c_str(),
                    (unsigned long long)ranked.front().first);
    }

    FastBookConfig cfg;
    cfg.max_orders = 8u << 20;
    cfg.max_symbols = 1u << 14;
    cfg.index_hint = 8u << 20;

    Exporter ex(universe, sample_ms * 1000000ULL, cfg);
    {
        Parser<Exporter> parser(ex);
        parser.run(mf.bytes());
    }
    std::printf("orders still live %zu at end of session\n", ex.still_live());

    ex.write(out);
    std::printf("wrote %s/micro_{symbols,spread_depth,lifetimes,order_sizes}.csv\n",
                out.c_str());
    return 0;
}
