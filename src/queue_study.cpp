// queue_study -- runs the queue-position models over a session and writes the bias table.
//
// Stage 7. Places non-impacting synthetic orders at the inside and reports,
// for each of the four queue models, the fill rate, the time-to-fill
// distribution and the realised value, broken out by queue depth at entry.
//
// The exact market-by-order model is the reference. What the study measures is
// how far each market-by-price approximation departs from it, which is the
// contribution claimed in docs/design.md record 021 -- the exact model itself
// is not novel.
//
// Rule 4 (a non-displayed print at the order's price counts as a fill) is a
// modelling choice, not an inference, so the whole study runs twice and both
// results are reported. See record 020.
//
//   usage: queue_study [--seed N] [--interarrival-ms N] [--max-life-s N]
//                      [--universe N] [--out DIR] [--venue NAME]
//                      [--date YYYY-MM-DD] <session-file>
//
// Placements are restricted to the --universe busiest symbols. Pooling every
// two-sided symbol mixes one-cent spreads with dollar spreads, and the value
// figures are then dominated by a handful of illiquid names whose inside means
// little.

#include "carteret/mapped_file.hpp"
#include "carteret/parser.hpp"
#include "carteret/queue_sim.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

using namespace carteret;

namespace {

struct Options {
    std::string path;
    std::string out = "results/queue";
    std::string venue = "unknown";
    std::string date = "unknown";
    std::uint64_t seed = 20190130;
    std::uint64_t interarrival_ms = 250;
    std::uint64_t max_life_s = 60;
    std::size_t universe = 50; // 0 means every two-sided symbol
    int seeds = 1;             // how many placement sequences to run
};

// Picks the busiest symbols, so placements land on names whose inside is
// meaningful. Pooling every two-sided symbol mixes one-cent spreads with
// dollar spreads and lets a few illiquid names dominate any value figure.
struct UniverseCensus {
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

std::vector<std::uint16_t>
pick_universe(const MappedFile& mf, std::size_t n, std::string& busiest,
              std::unordered_map<std::uint16_t, std::string>& names) {
    UniverseCensus c;
    Parser<UniverseCensus> parser(c);
    parser.run(mf.bytes());

    std::vector<std::pair<std::uint64_t, std::uint16_t>> ranked;
    ranked.reserve(c.messages.size());
    for (const auto& [locate, count] : c.messages) {
        if (c.names.count(locate)) ranked.emplace_back(count, locate);
    }
    std::sort(ranked.begin(), ranked.end(), std::greater<>());
    if (n && ranked.size() > n) ranked.resize(n);

    std::vector<std::uint16_t> out;
    out.reserve(ranked.size());
    for (const auto& [count, locate] : ranked) out.push_back(locate);
    if (!ranked.empty()) busiest = c.names[ranked.front().second];
    for (const std::uint16_t locate : out) names[locate] = c.names[locate];
    return out;
}

std::FILE* open_or_die(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "w");
    if (!f) {
        std::fprintf(stderr, "cannot write %s\n", path.c_str());
        std::exit(1);
    }
    return f;
}

QueueSimulator run_once(const MappedFile& mf, const Options& opt,
                        const std::vector<std::uint16_t>& universe, bool rule4,
                        std::uint64_t seed) {
    QueueSimConfig cfg;
    cfg.universe = universe;
    cfg.seed = seed;
    cfg.mean_interarrival_ns = opt.interarrival_ms * 1000000ULL;
    cfg.max_life_ns = opt.max_life_s * 1000000000ULL;
    cfg.rule4_non_displayed_fills = rule4;

    QueueSimulator sim(cfg);
    Parser<QueueSimulator> parser(sim);
    parser.run(mf.bytes());
    sim.finish(cfg.end_ns);
    return sim;
}

void print_summary(const QueueSimulator& sim, bool rule4) {
    const auto& r = sim.results();
    std::printf("\n=== rule 4 (non-displayed print fills) %s ===\n", rule4 ? "ON" : "OFF");
    std::printf("placements %llu   skipped (no inside) %llu   schedule overruns %llu\n",
                (unsigned long long)sim.placements(),
                (unsigned long long)sim.skipped_no_inside(),
                (unsigned long long)sim.schedule_overruns());
    std::printf("\n%-14s %10s %10s %12s %14s\n", "model", "placed", "filled", "fill rate",
                "mean t-to-fill");
    for (std::size_t m = 0; m < 4; ++m) {
        const double mean_ttf = r[m].filled ? static_cast<double>(r[m].sum_time_to_fill_ns) /
                                                  static_cast<double>(r[m].filled) / 1e9
                                            : 0.0;
        std::printf("%-14s %10llu %10llu %11.3f%% %12.3f s\n", kQueueModelName[m],
                    (unsigned long long)r[m].placed, (unsigned long long)r[m].filled,
                    r[m].fill_rate() * 100.0, mean_ttf);
    }

    // The bias, which is the result: each approximation against exact.
    const double exact_rate = r[0].fill_rate();
    std::printf("\n%-14s %16s %16s\n", "model", "fill-rate bias", "relative");
    for (std::size_t m = 1; m < 4; ++m) {
        const double d = r[m].fill_rate() - exact_rate;
        std::printf("%-14s %15.3fpp %15.1f%%\n", kQueueModelName[m], d * 100.0,
                    exact_rate > 0 ? 100.0 * d / exact_rate : 0.0);
    }

    std::printf("\nfill rate by queue depth at entry (shares ahead)\n");
    std::printf("%-14s", "depth");
    for (std::size_t m = 0; m < 4; ++m) std::printf(" %13s", kQueueModelName[m]);
    std::printf(" %12s\n", "n placed");
    for (std::size_t d = 0; d < kDepthBucketEdges.size(); ++d) {
        if (r[0].placed_by_depth[d] == 0) continue;
        char label[32];
        if (d + 1 < kDepthBucketEdges.size()) {
            std::snprintf(label, sizeof label, "%llu-%llu",
                          (unsigned long long)kDepthBucketEdges[d],
                          (unsigned long long)kDepthBucketEdges[d + 1] - 1);
        } else {
            std::snprintf(label, sizeof label, "%llu+",
                          (unsigned long long)kDepthBucketEdges[d]);
        }
        std::printf("%-14s", label);
        for (std::size_t m = 0; m < 4; ++m) {
            const double rate = r[m].placed_by_depth[d]
                                    ? static_cast<double>(r[m].filled_by_depth[d]) /
                                          static_cast<double>(r[m].placed_by_depth[d])
                                    : 0.0;
            std::printf(" %12.2f%%", rate * 100.0);
        }
        std::printf(" %12llu\n", (unsigned long long)r[0].placed_by_depth[d]);
    }

    // Adverse selection at every horizon, so that a headline cannot quietly
    // pick the most favourable one. The horizon used in any headline is named
    // beside it.
    std::printf("\nrealised value per filled order, in half-spreads at entry\n");
    std::printf("%-14s %12s %12s %12s %14s\n", "model", "1 s", "10 s", "60 s", "fills valued");
    for (std::size_t m = 0; m < 4; ++m) {
        double hs[3] = {0, 0, 0};
        std::uint64_t n[3] = {0, 0, 0};
        for (std::size_t d = 0; d < kDepthBucketEdges.size(); ++d) {
            for (std::size_t h = 0; h < 3; ++h) {
                hs[h] += r[m].value_halfspreads[d][h];
                n[h] += r[m].valued_by_depth[d][h];
            }
        }
        std::printf("%-14s", kQueueModelName[m]);
        for (std::size_t h = 0; h < 3; ++h) {
            std::printf(" %12.3f", n[h] ? hs[h] / static_cast<double>(n[h]) : 0.0);
        }
        std::printf(" %14llu\n", (unsigned long long)n[0]);
    }
    std::printf("  A value of 0 means the mid moved exactly far enough to give back the\n");
    std::printf("  half spread the order earned by resting at the inside; -1 means twice\n");
    std::printf("  that far. Unfilled orders are worth zero and are carried by the fill\n");
    std::printf("  rate, not by this number.\n");

    // Step 3(e): the proportional model's error, measured directly. For every
    // cancel at a live synthetic order's price the exact model knows whether
    // the cancelled order was ahead; the proportional model only assumed a
    // fraction. Assumed above actual means it advanced the queue too fast.
    std::printf("\ncancel attribution: what proportional assumed vs what happened\n");
    std::printf("%-14s %14s %14s %14s %14s\n", "depth", "cancel shares", "actually ahead",
                "assumed ahead", "assumed-actual");
    {
        const ModelResults& e = r[static_cast<std::size_t>(QueueModel::Exact)];
        double tot = 0, act = 0, asm_ = 0;
        for (std::size_t d = 0; d < kDepthBucketEdges.size(); ++d) {
            if (e.cancel_shares_total[d] == 0) continue;
            char label[32];
            if (d + 1 < kDepthBucketEdges.size()) {
                std::snprintf(label, sizeof label, "%llu-%llu",
                              (unsigned long long)kDepthBucketEdges[d],
                              (unsigned long long)kDepthBucketEdges[d + 1] - 1);
            } else {
                std::snprintf(label, sizeof label, "%llu+",
                              (unsigned long long)kDepthBucketEdges[d]);
            }
            const double a = e.cancel_shares_actually_ahead[d] / e.cancel_shares_total[d];
            const double b = e.cancel_shares_assumed_ahead[d] / e.cancel_shares_total[d];
            std::printf("%-14s %14.0f %13.2f%% %13.2f%% %13.2fpp\n", label,
                        e.cancel_shares_total[d], a * 100, b * 100, (b - a) * 100);
            tot += e.cancel_shares_total[d];
            act += e.cancel_shares_actually_ahead[d];
            asm_ += e.cancel_shares_assumed_ahead[d];
        }
        if (tot > 0) {
            std::printf("%-14s %14.0f %13.2f%% %13.2f%% %13.2fpp\n", "all", tot,
                        act / tot * 100, asm_ / tot * 100, (asm_ - act) / tot * 100);
        }
        std::printf("  Positive in the last column means the proportional model attributed\n");
        std::printf("  more of each cancel to the queue ahead than actually was there, so\n");
        std::printf("  it advanced the queue too fast and should over-estimate fills.\n");
    }

    std::printf("\nwhy orders filled\n");
    std::printf("%-14s %14s %14s %16s %12s\n", "model", "exec behind", "trade through",
                "non-displayed", "expired");
    for (std::size_t m = 0; m < 4; ++m) {
        std::printf("%-14s %14llu %14llu %16llu %12llu\n", kQueueModelName[m],
                    (unsigned long long)r[m].reasons[1], (unsigned long long)r[m].reasons[2],
                    (unsigned long long)r[m].reasons[3], (unsigned long long)r[m].reasons[4]);
    }
}

void write_csv(const QueueSimulator& sim, const Options& opt, bool rule4) {
    const auto& r = sim.results();
    const std::string suffix = rule4 ? "rule4on" : "rule4off";

    std::FILE* f = open_or_die(opt.out + "/queue_bias_" + suffix + ".csv");
    std::fprintf(f, "venue,date,rule4,model,depth_bucket,depth_low,depth_high,placed,filled,"
                    "fill_rate,value_1s,value_10s,value_60s,"
                    "value_hs_1s,value_hs_10s,value_hs_60s,"
                    "valued_1s,valued_10s,valued_60s\n");
    for (std::size_t m = 0; m < 4; ++m) {
        for (std::size_t d = 0; d < kDepthBucketEdges.size(); ++d) {
            if (r[m].placed_by_depth[d] == 0) continue;
            const double rate = static_cast<double>(r[m].filled_by_depth[d]) /
                                static_cast<double>(r[m].placed_by_depth[d]);
            const long long high = d + 1 < kDepthBucketEdges.size()
                                       ? static_cast<long long>(kDepthBucketEdges[d + 1]) - 1
                                       : -1;
            std::fprintf(f, "%s,%s,%d,%s,%zu,%llu,%lld,%llu,%llu,%.6f", opt.venue.c_str(),
                         opt.date.c_str(), rule4 ? 1 : 0, kQueueModelName[m], d,
                         (unsigned long long)kDepthBucketEdges[d], high,
                         (unsigned long long)r[m].placed_by_depth[d],
                         (unsigned long long)r[m].filled_by_depth[d], rate);
            for (std::size_t h = 0; h < 3; ++h) {
                // Mean value per filled order, in Price(4) units. Unfilled
                // orders are worth zero and are simply not in the numerator or
                // the denominator here; the fill rate carries that half.
                const double mean = r[m].valued_by_depth[d][h]
                                        ? r[m].value_by_depth[d][h] /
                                              static_cast<double>(r[m].valued_by_depth[d][h])
                                        : 0.0;
                std::fprintf(f, ",%.4f", mean);
            }
            for (std::size_t h = 0; h < 3; ++h) {
                const double mean = r[m].valued_by_depth[d][h]
                                        ? r[m].value_halfspreads[d][h] /
                                              static_cast<double>(r[m].valued_by_depth[d][h])
                                        : 0.0;
                std::fprintf(f, ",%.6f", mean);
            }
            for (std::size_t h = 0; h < 3; ++h) {
                std::fprintf(f, ",%llu", (unsigned long long)r[m].valued_by_depth[d][h]);
            }
            std::fprintf(f, "\n");
        }
    }
    std::fclose(f);

    f = open_or_die(opt.out + "/queue_time_to_fill_" + suffix + ".csv");
    std::fprintf(f, "venue,date,rule4,model,bucket,low_ns,high_ns,count\n");
    for (std::size_t m = 0; m < 4; ++m) {
        for (std::size_t b = 0; b < kFillTimeBuckets; ++b) {
            if (r[m].time_to_fill[b] == 0) continue;
            std::fprintf(f, "%s,%s,%d,%s,%zu,%.0f,%.0f,%llu\n", opt.venue.c_str(),
                         opt.date.c_str(), rule4 ? 1 : 0, kQueueModelName[m], b,
                         fill_time_bucket_low_ns(b), fill_time_bucket_low_ns(b + 1),
                         (unsigned long long)r[m].time_to_fill[b]);
        }
    }
    std::fclose(f);
}

// Spread of the fill rate across placement sequences. A single seed cannot
// separate a difference between models from a difference between the orders
// that happened to be placed; running several and reporting the range is what
// makes that separable.
void print_seed_spread(const std::vector<std::array<double, 4>>& rates, int seeds) {
    if (seeds <= 1) {
        std::printf("\nseed spread       not measured (one seed)\n");
        return;
    }
    std::printf("\nfill rate across %d placement sequences\n", seeds);
    std::printf("%-14s %10s %10s %10s %10s\n", "model", "mean", "min", "max", "range");
    for (std::size_t m = 0; m < 4; ++m) {
        double lo = rates[0][m], hi = rates[0][m], sum = 0.0;
        for (const auto& r : rates) {
            lo = r[m] < lo ? r[m] : lo;
            hi = r[m] > hi ? r[m] : hi;
            sum += r[m];
        }
        const double mean = sum / static_cast<double>(rates.size());
        std::printf("%-14s %9.3f%% %9.3f%% %9.3f%% %9.3f%%\n", kQueueModelName[m], mean * 100,
                    lo * 100, hi * 100, (hi - lo) * 100);
    }
}

void write_seed_csv(const std::vector<std::array<double, 4>>& rates, const Options& opt,
                    bool rule4) {
    const std::string suffix = rule4 ? "rule4on" : "rule4off";
    std::FILE* f = open_or_die(opt.out + "/queue_seeds_" + suffix + ".csv");
    std::fprintf(f, "venue,date,rule4,seed_index,model,fill_rate\n");
    for (std::size_t k = 0; k < rates.size(); ++k) {
        for (std::size_t m = 0; m < 4; ++m) {
            std::fprintf(f, "%s,%s,%d,%zu,%s,%.6f\n", opt.venue.c_str(), opt.date.c_str(),
                         rule4 ? 1 : 0, k, kQueueModelName[m], rates[k][m]);
        }
    }
    std::fclose(f);
}

// Per-symbol counts, which is what the cluster bootstrap resamples. Orders on
// one symbol share its book, its spread and its flow, so the symbol rather
// than the order is the unit of independent information.
void write_symbol_csv(const QueueSimulator& sim, const Options& opt, bool rule4,
                      const std::unordered_map<std::uint16_t, std::string>& names) {
    const std::string suffix = rule4 ? "rule4on" : "rule4off";
    std::FILE* f = open_or_die(opt.out + "/queue_by_symbol_" + suffix + ".csv");
    std::fprintf(f, "venue,date,rule4,symbol,locate,model,placed,filled\n");
    for (std::size_t m = 0; m < 4; ++m) {
        for (const auto& [locate, counts] : sim.results()[m].by_symbol) {
            const auto it = names.find(locate);
            std::fprintf(f, "%s,%s,%d,%s,%u,%s,%llu,%llu\n", opt.venue.c_str(),
                         opt.date.c_str(), rule4 ? 1 : 0,
                         it == names.end() ? "?" : it->second.c_str(), unsigned(locate),
                         kQueueModelName[m], (unsigned long long)counts.placed,
                         (unsigned long long)counts.filled);
        }
    }
    std::fclose(f);
}

} // namespace

int main(int argc, char** argv) {
    Options opt;
    for (int i = 1; i < argc; ++i) {
        const auto next = [&](const char* flag) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s needs a value\n", flag);
                std::exit(2);
            }
            return argv[++i];
        };
        if (std::strcmp(argv[i], "--seed") == 0)
            opt.seed = std::strtoull(next("--seed"), nullptr, 10);
        else if (std::strcmp(argv[i], "--interarrival-ms") == 0)
            opt.interarrival_ms = std::strtoull(next("--interarrival-ms"), nullptr, 10);
        else if (std::strcmp(argv[i], "--max-life-s") == 0)
            opt.max_life_s = std::strtoull(next("--max-life-s"), nullptr, 10);
        else if (std::strcmp(argv[i], "--universe") == 0)
            opt.universe = std::strtoull(next("--universe"), nullptr, 10);
        else if (std::strcmp(argv[i], "--seeds") == 0)
            opt.seeds = std::atoi(next("--seeds"));
        else if (std::strcmp(argv[i], "--out") == 0)
            opt.out = next("--out");
        else if (std::strcmp(argv[i], "--venue") == 0)
            opt.venue = next("--venue");
        else if (std::strcmp(argv[i], "--date") == 0)
            opt.date = next("--date");
        else
            opt.path = argv[i];
    }
    if (opt.path.empty()) {
        std::fprintf(stderr,
                     "usage: %s [--seed N] [--interarrival-ms N] [--max-life-s N]\n"
                     "          [--universe N] [--seeds N] [--out DIR] [--venue NAME] "
                     "[--date YYYY-MM-DD]\n          <session-file>\n",
                     argv[0]);
        return 2;
    }

    MappedFile mf(opt.path);
    std::printf("session        %s\n", opt.path.c_str());
    std::printf("venue / date   %s / %s\n", opt.venue.c_str(), opt.date.c_str());
    std::printf("seed           %llu\n", (unsigned long long)opt.seed);
    std::printf("interarrival   %llu ms of exchange time\n",
                (unsigned long long)opt.interarrival_ms);
    std::printf("max order life %llu s\n", (unsigned long long)opt.max_life_s);

    std::string busiest = "?";
    std::unordered_map<std::uint16_t, std::string> names;
    const std::vector<std::uint16_t> universe = pick_universe(mf, opt.universe, busiest, names);
    std::printf("universe       %zu busiest symbols (busiest %s)\n", universe.size(),
                busiest.c_str());

    // Both arms of rule 4, from the same seed, so the two runs place the same
    // orders and differ only in the rule.
    for (const bool rule4 : {false, true}) {
        // Every seed is a different placement sequence over the same session.
        // The spread across them is the only handle on how much of a reported
        // difference is the models and how much is which orders happened to be
        // placed; a single seed cannot distinguish the two.
        std::vector<std::array<double, 4>> rates_by_seed;
        QueueSimulator base = run_once(mf, opt, universe, rule4, opt.seed);
        for (int k = 0; k < opt.seeds; ++k) {
            const std::uint64_t seed = opt.seed + static_cast<std::uint64_t>(k);
            std::array<double, 4> rates{};
            if (k == 0) {
                for (std::size_t m = 0; m < 4; ++m) rates[m] = base.results()[m].fill_rate();
            } else {
                const QueueSimulator sim = run_once(mf, opt, universe, rule4, seed);
                for (std::size_t m = 0; m < 4; ++m) rates[m] = sim.results()[m].fill_rate();
            }
            rates_by_seed.push_back(rates);
        }
        print_summary(base, rule4);
        print_seed_spread(rates_by_seed, opt.seeds);
        write_csv(base, opt, rule4);
        write_symbol_csv(base, opt, rule4, names);
        write_seed_csv(rates_by_seed, opt, rule4);
    }
    std::printf("\nwrote %s/queue_{bias,time_to_fill}_rule4{off,on}.csv\n", opt.out.c_str());
    return 0;
}
