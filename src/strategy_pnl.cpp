// strategy_pnl -- the pre-registered maker strategy, EXPLORATORY.
//
// EXPLORATORY, and the label is not decoration. docs/preregistration.md
// section 8 ends "Signal threshold *(to be filled)*. Minimum fills per session
// *(to be filled)*." Both are registered constants that were never filled, so
// the strategy component was never fully registered and cannot produce a
// confirmatory verdict. docs/heldout-harness-amendment.md section 2 records
// the two parameters declared in their place and why.
//
// Declared parameters, fixed before any held-out feature was computed:
//
//   signal threshold   ZERO. Section 6 posts "when the signal's magnitude
//                      exceeds a threshold". At zero the rule degenerates to
//                      the primary's own sign rule -- quote on the side the
//                      signal favours, whenever it has a side -- which is the
//                      only value that requires no choice. Any positive value
//                      would be a number picked with held-out data on disk.
//   minimum fills      NO RULE. Fill counts are reported per session instead.
//
// What is registered and is implemented exactly:
//
//   * one round lot at the inside, on the side the signal favours (section 6)
//   * fills from the queue simulator under record 020, EXACT market-by-order
//     queue position -- this program does not reimplement the fill rules, it
//     drives include/carteret/queue_sim.hpp
//   * rule 4 OFF in the primary arm, ON as a labelled sensitivity (section 6)
//   * inventory limit one round lot per symbol; no new quote while a position
//     is open in that symbol
//   * cancellation when the window ends, if unfilled
//   * exit at the end of the following window, at the mid, as a modelled
//     liquidation -- which overstates realisable P&L by the exit's own half
//     spread, as section 6 states rather than corrects
//   * base tier and top tier from section 7, both reported
//
//   usage: strategy_pnl [--window N] [--symbols N] [--rule4] [--out FILE]
//                       <session-file>

#include "carteret/mapped_file.hpp"
#include "carteret/parser.hpp"
#include "carteret/queue_sim.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace carteret;

namespace {

constexpr std::uint64_t kNs = 1000000000ULL;
constexpr std::uint64_t kOpenNs = 34200ULL * kNs;
constexpr std::uint64_t kCloseNs = 57600ULL * kNs;

// Section 7, dollars per share.
//
// THE TAPE IS DERIVABLE, and an earlier comment here said it was not. The
// Stock Directory 'R' message carries Market Category, which maps to tape:
// Q, G and S are NASDAQ-listed and print on Tape C; N is NYSE, Tape A; and
// A, P, Z and V are Tape B. The uniform Tape C rate is still what the primary
// figures use, because that is what the run executed, and Tape C at 0.0015 is
// the LOWER add rebate -- so the uniform figure cannot flatter the strategy.
// The per-tape breakdown is reported beside it: fills are counted by tape and
// the rebate is reweighted, which is accounting and changes no fill.
constexpr double kAddBase = 0.0015;   // Tape C, NASDAQ-listed
constexpr double kAddBaseAB = 0.0020; // Tapes A and B
constexpr double kAddTop = 0.00305;   // top tier, not tape-split in section 7

// Market Category -> tape. Anything unrecognised is counted separately rather
// than folded into a tape it may not belong to.
inline char tape_of(unsigned char cat) {
    switch (cat) {
    case 'Q':
    case 'G':
    case 'S': return 'C';
    case 'N': return 'A';
    case 'A':
    case 'P':
    case 'Z':
    case 'V': return 'B';
    default: return '?';
    }
}

struct Inside {
    bool valid = false;
    Price bid = 0, ask = 0;
    std::uint64_t bid_qty = 0, ask_qty = 0;
};

struct Quote {
    bool open = false; // a quote is live in the simulator
    bool filled = false;
    unsigned char side = 0;
    Price price = 0;
    double mid_at_entry = 0.0;
    double spread_at_entry = 0.0;
};

struct SymState {
    char tape = '?';
    Inside prev;
    std::uint32_t updates = 0;
    bool halted = false;
    bool win_halted = false;
    std::uint64_t win_start_ts = 0;
    double qi = 0.0; // the signal at the last window close
    bool have_signal = false;
    Quote quote;
};

struct Tally {
    std::uint64_t quotes = 0;
    std::uint64_t fills = 0;
    std::uint64_t fills_tape_a = 0, fills_tape_b = 0, fills_tape_c = 0, fills_tape_x = 0;
    double gross_halfspreads = 0.0; // (exit mid - entry) * side, in half spreads
    double gross_dollars = 0.0;     // same, in dollars per share
    double sum_spread_dollars = 0.0;
};

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
    template<class V>
    void on(V) {}
};

struct Strategy {
    QueueSimulator sim;
    std::unordered_map<std::uint16_t, SymState> st;
    std::unordered_set<std::uint16_t>* selected;
    std::unordered_map<std::uint16_t, std::string>* names;
    std::uint32_t window;
    Tally tally;

    explicit Strategy(const QueueSimConfig& cfg) : sim(cfg) {}

    [[nodiscard]] Inside read_inside(std::uint16_t locate) const {
        Inside in;
        const RefSymbol* s = sim.book().symbol(locate);
        if (!s || !s->has_bid() || !s->has_ask()) return in;
        in.valid = true;
        in.bid = s->best_bid();
        in.ask = s->best_ask();
        in.bid_qty = sim.book().depth_at(locate, kBuy, in.bid).shares;
        in.ask_qty = sim.book().depth_at(locate, kSell, in.ask).shares;
        return in;
    }

    void on_trading_action(std::uint16_t locate, unsigned char state) {
        SymState& s = st[locate];
        s.halted = (state == 'H');
        s.win_halted = true;
    }

    // Called after the simulator has applied a message.
    void after(std::uint16_t locate, std::uint64_t ts) {
        if (!selected->count(locate)) return;
        SymState& s = st[locate];
        const Inside now = read_inside(locate);
        const Inside before = s.prev;
        s.prev = now;
        if (!now.valid || !before.valid) return;
        if (now.bid == before.bid && now.ask == before.ask && now.bid_qty == before.bid_qty &&
            now.ask_qty == before.ask_qty) {
            return; // not an inside update
        }
        if (s.updates == 0) s.win_start_ts = ts;
        ++s.updates;
        if (s.halted) s.win_halted = true;
        if (s.updates < window) return;

        // --- window closes here -------------------------------------
        const double mid = ((double)now.bid + (double)now.ask) / 2.0;
        const double spread = (double)now.ask - (double)now.bid;
        const double qsum = (double)(now.bid_qty + now.ask_qty);
        const bool in_bounds = s.win_start_ts >= kOpenNs && ts < kCloseNs;
        const bool usable = in_bounds && !s.win_halted && qsum > 0 && spread > 0;

        // Settle any quote from the PREVIOUS window: exit at this mid.
        if (s.quote.open) {
            if (s.quote.filled) {
                const double sgn = (s.quote.side == kBuy) ? 1.0 : -1.0;
                const double g = (mid - (double)s.quote.price) * sgn; // Price(4) units
                ++tally.fills;
                switch (s.tape) {
                case 'A': ++tally.fills_tape_a; break;
                case 'B': ++tally.fills_tape_b; break;
                case 'C': ++tally.fills_tape_c; break;
                default: ++tally.fills_tape_x; break;
                }
                tally.gross_dollars += g / 10000.0;
                tally.gross_halfspreads +=
                    s.quote.spread_at_entry > 0 ? g / (s.quote.spread_at_entry / 2.0) : 0.0;
                tally.sum_spread_dollars += s.quote.spread_at_entry / 10000.0;
            }
            sim.cancel_symbol(locate, ts); // unfilled quotes are cancelled
            s.quote = Quote{};
        }

        // --- place for the window just closed, on the signal's side ---
        if (usable) {
            const double qi = ((double)now.bid_qty - (double)now.ask_qty) / qsum;
            // Signal threshold ZERO: quote whenever the signal has a side.
            if (qi != 0.0) {
                const unsigned char side = (qi > 0) ? kBuy : kSell;
                sim.place_directed(locate, side, ts);
                s.quote.open = true;
                s.quote.side = side;
                s.quote.price = (side == kBuy) ? now.bid : now.ask;
                s.quote.mid_at_entry = mid;
                s.quote.spread_at_entry = spread;
                ++tally.quotes;
            }
        }
        s.updates = 0;
        s.win_halted = false;
    }
};

struct Driver {
    Strategy* s;
    void on(SystemEvent v) { s->sim.on(v); }
    void on(StockDirectory v) {
        s->sim.on(v);
        s->st[v.locate()].tape = tape_of(v.market_category());
    }
    void on(StockTradingAction v) { s->on_trading_action(v.locate(), v.trading_state()); }
    void on(AddOrder v) {
        s->sim.on(v);
        s->after(v.locate(), v.ts());
    }
    void on(AddOrderMpid v) {
        s->sim.on(v);
        s->after(v.locate(), v.ts());
    }
    void on(OrderCancel v) {
        s->sim.on(v);
        s->after(v.locate(), v.ts());
    }
    void on(OrderDelete v) {
        s->sim.on(v);
        s->after(v.locate(), v.ts());
    }
    void on(OrderReplace v) {
        s->sim.on(v);
        s->after(v.locate(), v.ts());
    }
    void on(OrderExecuted v) {
        s->sim.on(v);
        s->after(v.locate(), v.ts());
    }
    void on(OrderExecutedPrice v) {
        s->sim.on(v);
        s->after(v.locate(), v.ts());
    }
    void on(Trade v) {
        s->sim.on(v);
        s->after(v.locate(), v.ts());
    }
    void on(CrossTrade v) {
        s->sim.on(v);
        s->after(v.locate(), v.ts());
    }
    void on(BrokenTrade v) {
        s->sim.on(v);
        s->after(v.locate(), v.ts());
    }
    template<class V>
    void on(V) {}
};

} // namespace

int main(int argc, char** argv) {
    std::string path, out_path;
    std::uint32_t window = 50;
    std::size_t n_symbols = 50;
    bool rule4 = false;

    for (int i = 1; i < argc; ++i) {
        const auto next = [&](const char* flag) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s needs a value\n", flag);
                std::exit(2);
            }
            return argv[++i];
        };
        if (std::strcmp(argv[i], "--window") == 0)
            window = (std::uint32_t)std::atoi(next("--window"));
        else if (std::strcmp(argv[i], "--symbols") == 0)
            n_symbols = (std::size_t)std::atoi(next("--symbols"));
        else if (std::strcmp(argv[i], "--rule4") == 0)
            rule4 = true;
        else if (std::strcmp(argv[i], "--out") == 0)
            out_path = next("--out");
        else if (argv[i][0] == '-') {
            std::fprintf(stderr, "unknown flag %s\n", argv[i]);
            return 2;
        } else
            path = argv[i];
    }
    if (path.empty()) {
        std::fprintf(stderr, "usage: strategy_pnl <session-file>\n");
        return 2;
    }

    // --- universe: the busiest symbols, exactly as export_features picks them
    MappedFile mf(path.c_str());
    Census c;
    {
        Parser<Census> p(c);
        p.run(mf.bytes());
    }
    std::vector<std::pair<std::uint64_t, std::uint16_t>> rank;
    rank.reserve(c.messages.size());
    for (auto& [loc, n] : c.messages) rank.push_back({n, loc});
    std::sort(rank.begin(), rank.end(), std::greater<>());
    if (rank.size() > n_symbols) rank.resize(n_symbols);
    std::vector<std::uint16_t> universe;
    for (auto& [n, loc] : rank) universe.push_back(loc);
    std::unordered_set<std::uint16_t> selected(universe.begin(), universe.end());

    // --- the strategy, driving the queue simulator
    QueueSimConfig cfg;
    cfg.universe = universe;
    cfg.order_size = 100;
    cfg.rule4_non_displayed_fills = rule4;
    cfg.timer_placement = false; // the strategy places, not a timer
    cfg.max_life_ns = ~0ULL;     // cancellation is by window, not by clock

    Strategy strat(cfg);
    strat.selected = &selected;
    strat.names = &c.names;
    strat.window = window;
    strat.sim.on_fill = [&strat](const SyntheticOrder& o, QueueModel m, std::uint64_t,
                                 FillReason) {
        if (m != QueueModel::Exact) return; // the registered model
        auto it = strat.st.find(o.locate);
        if (it != strat.st.end() && it->second.quote.open) it->second.quote.filled = true;
    };

    // The parser drives the simulator, then the strategy sees the same message.
    Driver d{&strat};
    {
        Parser<Driver> p(d);
        p.run(mf.bytes());
    }

    const Tally& t = strat.tally;
    const double fills = (double)t.fills;
    const double fill_rate = t.quotes ? fills / (double)t.quotes : 0.0;
    const double gross_per_fill = fills ? t.gross_dollars / fills : 0.0;
    const double gross_hs = fills ? t.gross_halfspreads / fills : 0.0;
    const double net_base = gross_per_fill + kAddBase;
    const double net_top = gross_per_fill + kAddTop;

    std::printf("EXPLORATORY  strategy P&L -- NOT a registered result\n");
    std::printf("EXPLORATORY  session            %s\n", path.c_str());
    std::printf("EXPLORATORY  rule 4             %s\n",
                rule4 ? "ON (sensitivity)" : "OFF (primary arm)");
    std::printf("EXPLORATORY  signal threshold   0  (declared; section 8 never filled one)\n");
    std::printf("EXPLORATORY  minimum fills rule none (declared; counts reported instead)\n");
    std::printf("EXPLORATORY  quotes placed      %llu\n", (unsigned long long)t.quotes);
    std::printf("EXPLORATORY  fills              %llu\n", (unsigned long long)t.fills);
    std::printf("EXPLORATORY  fill rate          %.4f\n", fill_rate);
    std::printf("EXPLORATORY  gross per fill     %.6f half-spreads\n", gross_hs);
    std::printf("EXPLORATORY  gross per fill     %.8f $/share\n", gross_per_fill);
    std::printf("EXPLORATORY  add rebate base    %.8f $/share\n", kAddBase);
    std::printf("EXPLORATORY  add rebate top     %.8f $/share\n", kAddTop);
    std::printf("EXPLORATORY  NET per fill base  %.8f $/share\n", net_base);
    std::printf("EXPLORATORY  NET per fill top   %.8f $/share  (sensitivity)\n", net_top);
    std::printf("EXPLORATORY  mean spread        %.6f $/share\n",
                fills ? t.sum_spread_dollars / fills : 0.0);
    // Post-hoc, accounting only: the same fills, with the rebate each one
    // actually earns given its tape.
    const double weighted_add = fills
                                    ? (kAddBase * (double)t.fills_tape_c +
                                       kAddBaseAB * (double)(t.fills_tape_a + t.fills_tape_b) +
                                       kAddBase * (double)t.fills_tape_x) /
                                          fills
                                    : 0.0;
    std::printf("EXPLORATORY  fills by tape      A %llu  B %llu  C %llu  unknown %llu\n",
                (unsigned long long)t.fills_tape_a, (unsigned long long)t.fills_tape_b,
                (unsigned long long)t.fills_tape_c, (unsigned long long)t.fills_tape_x);
    std::printf("EXPLORATORY  per-tape rebate    %.8f $/share (weighted; unknown at Tape C)\n",
                weighted_add);
    std::printf("EXPLORATORY  NET per fill base, per-tape  %.8f $/share  (post-hoc)\n",
                gross_per_fill + weighted_add);
    std::printf("EXPLORATORY  exit is a modelled liquidation at the mid, so this\n");
    std::printf("EXPLORATORY  overstates realisable P&L by the exit's half spread.\n");

    if (!out_path.empty()) {
        std::FILE* f = std::fopen(out_path.c_str(), "w");
        if (f) {
            std::fprintf(f, "session,rule4,quotes,fills,fill_rate,gross_halfspreads,"
                            "gross_dollars,net_base,net_top\n");
            std::fprintf(f, "%s,%d,%llu,%llu,%.6f,%.6f,%.8f,%.8f,%.8f\n", path.c_str(),
                         rule4 ? 1 : 0, (unsigned long long)t.quotes,
                         (unsigned long long)t.fills, fill_rate, gross_hs, gross_per_fill,
                         net_base, net_top);
            std::fclose(f);
        }
    }
    return 0;
}
