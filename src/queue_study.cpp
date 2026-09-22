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
//                      [--out DIR] [--venue NAME] [--date YYYY-MM-DD]
//                      <session-file>

#include "carteret/mapped_file.hpp"
#include "carteret/parser.hpp"
#include "carteret/queue_sim.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

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
};

std::FILE* open_or_die(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "w");
    if (!f) {
        std::fprintf(stderr, "cannot write %s\n", path.c_str());
        std::exit(1);
    }
    return f;
}

QueueSimulator run_once(const MappedFile& mf, const Options& opt, bool rule4) {
    QueueSimConfig cfg;
    cfg.seed = opt.seed;
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
    std::printf(" %12s\n", "placements");
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
                    "fill_rate,value_1s,value_10s,value_60s,valued_1s,valued_10s,"
                    "valued_60s\n");
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
                     "          [--out DIR] [--venue NAME] [--date YYYY-MM-DD] "
                     "<session-file>\n",
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

    // Both arms of rule 4, from the same seed, so the two runs place the same
    // orders and differ only in the rule.
    for (const bool rule4 : {false, true}) {
        const QueueSimulator sim = run_once(mf, opt, rule4);
        print_summary(sim, rule4);
        write_csv(sim, opt, rule4);
    }
    std::printf("\nwrote %s/queue_{bias,time_to_fill}_rule4{off,on}.csv\n", opt.out.c_str());
    return 0;
}
