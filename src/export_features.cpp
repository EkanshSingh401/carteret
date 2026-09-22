// export_features -- per-window signal features and labels from a session.
//
// Stage 8 input. Emits one row per completed window of inside updates, with
// the features and the label defined in docs/preregistration.md section 5.
//
// Event time is the primary axis. A window is N consecutive updates to the
// inside -- a change in the best bid or offer price or size -- so a quiet
// symbol produces few windows and a busy one many, which is the intended
// weighting. Clock-time windows are a robustness check and are not produced
// here.
//
// The label for window k is the mid-price change over window k+1, so a row is
// written only once the following window has closed. That ordering is the
// whole distinction in section 1 of the pre-registration: the feature is known
// before the move it is asked to predict.
//
// Trade sign is known exactly rather than inferred. The feed names the order
// each execution hits and the book knows that order's side, so no Lee-Ready
// style classifier is used and no classification error enters the feature.
//
//   usage: export_features [--window N] [--symbols N] [--out FILE] <session-file>

#include "carteret/mapped_file.hpp"
#include "carteret/parser.hpp"
#include "carteret/reference_book.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace carteret;

namespace {

constexpr std::uint64_t kNs = 1000000000ULL;
constexpr std::uint64_t kOpenNs = 34200ULL * kNs;  // 09:30:00
constexpr std::uint64_t kCloseNs = 57600ULL * kNs; // 16:00:00

struct Inside {
    bool valid = false;
    Price bid = 0, ask = 0;
    std::uint64_t bid_qty = 0, ask_qty = 0;
};

struct Window {
    std::uint32_t updates = 0;
    double ofi = 0.0;       // unnormalised; divided by mean depth when closed
    double depth_sum = 0.0; // (bid_qty + ask_qty) / 2, per update
    double signed_exec = 0.0;
    double total_exec = 0.0;
    std::uint64_t start_ts = 0;
    bool halted = false;
};

// A closed window's features, held until the next window closes so the label
// can be attached.
struct Pending {
    bool have = false;
    double ofi = 0.0, queue_imbalance = 0.0, trade_sign = 0.0;
    double micro_dev = 0.0; // ticks, not half-spreads; see record 031
    double mid = 0.0, spread = 0.0;
    std::uint64_t end_ts = 0;
    std::uint32_t updates = 0;
};

struct SymbolState {
    Inside prev;
    Window win;
    Pending pending;
    bool halted = false;
    std::string name;
};

class FeatureExporter {
public:
    FeatureExporter(const std::vector<std::uint16_t>& universe, std::uint32_t window,
                    std::FILE* out)
        : window_(window), out_(out) {
        selected_.insert(universe.begin(), universe.end());
        std::fprintf(out_, "symbol,window_end_ts,updates,ofi,queue_imbalance,"
                           "micro_dev_ticks,trade_sign,mid,spread_ticks,label_ticks,"
                           "label_halfspreads\n");
    }

    void on(SystemEvent v) { book_.on(v); }

    void on(StockDirectory v) {
        book_.on(v);
        state_[v.locate()].name = std::string(v.stock());
    }

    // A halt invalidates the window it lands in: the inside is not comparable
    // across it, and the resumption prints a large mid change that is not a
    // prediction target.
    void on(StockTradingAction v) {
        SymbolState& s = state_[v.locate()];
        s.halted = (v.trading_state() == 'H');
        s.win.halted = true;
    }

    void on(AddOrder v) {
        book_.on(v);
        touch(v.locate(), v.ts(), 0.0, 0.0);
    }
    void on(AddOrderMpid v) {
        book_.on(v);
        touch(v.locate(), v.ts(), 0.0, 0.0);
    }
    void on(OrderCancel v) {
        book_.on(v);
        touch(v.locate(), v.ts(), 0.0, 0.0);
    }
    void on(OrderDelete v) {
        book_.on(v);
        touch(v.locate(), v.ts(), 0.0, 0.0);
    }
    void on(OrderReplace v) {
        book_.on(v);
        touch(v.locate(), v.ts(), 0.0, 0.0);
    }

    // The aggressor's side follows exactly from the resting order's side: an
    // execution against a resting bid is a sell, against a resting ask a buy.
    // It has to be read before the book applies the message, because the order
    // may not survive it.
    void on(OrderExecuted v) { executed(v.locate(), v.order_ref(), v.executed_shares(), v); }
    void on(OrderExecutedPrice v) {
        executed(v.locate(), v.order_ref(), v.executed_shares(), v);
    }

    void on(Trade v) { touch(v.locate(), v.ts(), 0.0, 0.0); }
    void on(CrossTrade v) { touch(v.locate(), v.ts(), 0.0, 0.0); }
    void on(BrokenTrade v) { touch(v.locate(), v.ts(), 0.0, 0.0); }

    [[nodiscard]] std::uint64_t rows() const noexcept { return rows_; }
    [[nodiscard]] std::uint64_t dropped_halt() const noexcept { return dropped_halt_; }
    [[nodiscard]] std::uint64_t dropped_bounds() const noexcept { return dropped_bounds_; }

private:
    template<class V>
    void executed(std::uint16_t locate, Ref ref, std::uint32_t qty, V v) {
        double signed_qty = 0.0;
        if (const RefOrder* o = book_.order(ref)) {
            signed_qty =
                (o->side == kBuy) ? -static_cast<double>(qty) : static_cast<double>(qty);
        }
        book_.on(v);
        touch(locate, v.ts(), signed_qty, static_cast<double>(qty));
    }

    [[nodiscard]] Inside read_inside(std::uint16_t locate) const {
        Inside in;
        const RefSymbol* s = book_.symbol(locate);
        if (!s || !s->has_bid() || !s->has_ask()) return in;
        in.valid = true;
        in.bid = s->best_bid();
        in.ask = s->best_ask();
        in.bid_qty = book_.depth_at(locate, kBuy, in.bid).shares;
        in.ask_qty = book_.depth_at(locate, kSell, in.ask).shares;
        return in;
    }

    void touch(std::uint16_t locate, std::uint64_t ts, double signed_exec, double total_exec) {
        if (!selected_.count(locate)) return;
        SymbolState& s = state_[locate];

        s.win.signed_exec += signed_exec;
        s.win.total_exec += total_exec;

        const Inside now = read_inside(locate);
        const Inside before = s.prev;
        s.prev = now;
        if (!now.valid || !before.valid) return;
        if (now.bid == before.bid && now.ask == before.ask && now.bid_qty == before.bid_qty &&
            now.ask_qty == before.ask_qty) {
            return; // not an inside update
        }

        // Cont-Kukanov-Stoikov order flow imbalance, one term per side.
        double e = 0.0;
        if (now.bid >= before.bid) e += static_cast<double>(now.bid_qty);
        if (now.bid <= before.bid) e -= static_cast<double>(before.bid_qty);
        if (now.ask <= before.ask) e -= static_cast<double>(now.ask_qty);
        if (now.ask >= before.ask) e += static_cast<double>(before.ask_qty);

        s.win.ofi += e;
        s.win.depth_sum +=
            (static_cast<double>(now.bid_qty) + static_cast<double>(now.ask_qty)) / 2.0;
        if (s.win.updates == 0) s.win.start_ts = ts;
        ++s.win.updates;
        if (s.halted) s.win.halted = true;

        if (s.win.updates >= window_) close_window(s, now, ts);
    }

    void close_window(SymbolState& s, const Inside& now, std::uint64_t ts) {
        const double mid = (static_cast<double>(now.bid) + static_cast<double>(now.ask)) / 2.0;
        const double spread = static_cast<double>(now.ask) - static_cast<double>(now.bid);
        const double qsum = static_cast<double>(now.bid_qty + now.ask_qty);
        const bool in_bounds = s.win.start_ts >= kOpenNs && ts < kCloseNs;
        const bool usable = in_bounds && !s.win.halted && qsum > 0 && spread > 0;

        if (s.pending.have) {
            if (usable) {
                const double label = mid - s.pending.mid;
                std::fprintf(out_, "%s,%llu,%u,%.6f,%.6f,%.6f,%.6f,%.4f,%.2f,%.4f,%.6f\n",
                             s.name.c_str(), (unsigned long long)s.pending.end_ts,
                             s.pending.updates, s.pending.ofi, s.pending.queue_imbalance,
                             s.pending.micro_dev, s.pending.trade_sign, s.pending.mid / 10000.0,
                             s.pending.spread / 100.0, label / 100.0,
                             label / (s.pending.spread / 2.0));
                ++rows_;
            } else if (!in_bounds) {
                ++dropped_bounds_;
            } else {
                ++dropped_halt_;
            }
        }

        Pending p;
        p.have = usable;
        const double mean_depth =
            s.win.updates ? s.win.depth_sum / static_cast<double>(s.win.updates) : 0.0;
        p.ofi = mean_depth > 0 ? s.win.ofi / mean_depth : 0.0;
        p.queue_imbalance =
            qsum > 0
                ? (static_cast<double>(now.bid_qty) - static_cast<double>(now.ask_qty)) / qsum
                : 0.0;
        // Micro-price deviation is emitted in TICKS, deliberately, not
        // normalised by the half spread. Normalising it makes it algebraically
        // identical to queue imbalance:
        //
        //   micro - mid = (spread/2) * (q_bid - q_ask) / (q_bid + q_ask)
        //
        // so (micro - mid) / (spread/2) IS queue imbalance, for every input.
        // Emitting both normalised would put the same feature in the
        // correction family twice. In ticks it carries the spread as well and
        // is genuinely distinct. See docs/design.md record 031.
        const double micro =
            qsum > 0 ? (static_cast<double>(now.ask_qty) * static_cast<double>(now.bid) +
                        static_cast<double>(now.bid_qty) * static_cast<double>(now.ask)) /
                           qsum
                     : mid;
        p.micro_dev = (micro - mid) / 100.0;
        p.trade_sign = s.win.total_exec > 0 ? s.win.signed_exec / s.win.total_exec : 0.0;
        p.mid = mid;
        p.spread = spread;
        p.end_ts = ts;
        p.updates = s.win.updates;
        s.pending = p;
        s.win = Window{};
    }

    ReferenceBook book_;
    std::unordered_map<std::uint16_t, SymbolState> state_;
    std::unordered_set<std::uint16_t> selected_;
    std::uint32_t window_;
    std::FILE* out_;
    std::uint64_t rows_ = 0;
    std::uint64_t dropped_halt_ = 0;
    std::uint64_t dropped_bounds_ = 0;
};

// Ranks symbols by book activity, so the universe comes from the session
// itself rather than from a list of tickers someone expected to be liquid.
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
    std::string out_path = "results/features.csv";
    std::uint32_t window = 50;
    std::size_t n_symbols = 50;

    for (int i = 1; i < argc; ++i) {
        const auto next = [&](const char* flag) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s needs a value\n", flag);
                std::exit(2);
            }
            return argv[++i];
        };
        if (std::strcmp(argv[i], "--window") == 0)
            window = static_cast<std::uint32_t>(std::strtoul(next("--window"), nullptr, 10));
        else if (std::strcmp(argv[i], "--symbols") == 0)
            n_symbols = std::strtoull(next("--symbols"), nullptr, 10);
        else if (std::strcmp(argv[i], "--out") == 0)
            out_path = next("--out");
        else
            path = argv[i];
    }
    if (path.empty() || window == 0) {
        std::fprintf(stderr,
                     "usage: %s [--window N] [--symbols N] [--out FILE] <session-file>\n",
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

    std::FILE* out = std::fopen(out_path.c_str(), "w");
    if (!out) {
        std::fprintf(stderr, "cannot write %s\n", out_path.c_str());
        return 1;
    }

    FeatureExporter ex(universe, window, out);
    {
        Parser<FeatureExporter> parser(ex);
        parser.run(mf.bytes());
    }
    std::fclose(out);

    std::printf("session          %s\n", path.c_str());
    std::printf("window           %u inside updates\n", window);
    std::printf("universe         %zu symbols\n", universe.size());
    std::printf("rows             %llu\n", (unsigned long long)ex.rows());
    std::printf("dropped, halt    %llu\n", (unsigned long long)ex.dropped_halt());
    std::printf("dropped, bounds  %llu\n", (unsigned long long)ex.dropped_bounds());
    std::printf("wrote %s\n", out_path.c_str());
    return 0;
}
