// draw_checksum -- a checksum over the exact draws the study consumes.
//
// The golden test pins six exponential draws and checksums a million more, on
// whatever platform happens to run it. That is not the comparison that
// matters. std::log1p is not required to be correctly rounded, and
// research/ulp_margin.py shows the number of draws at risk of differing grows
// linearly with how many are taken: across Stage 7 and the planned study the
// expected count is 0.64, which is a coin flip rather than a negligible
// chance.
//
// So the comparison has to be made, not argued. This emits a checksum over a
// stated number of draws from a stated seed. bench/run_linux.sh runs it on the
// benchmark host -- x86-64, glibc -- and compares against the value recorded
// in docs/benchmarks.md from the development Mac. Equal means the two math
// libraries agree on every draw the study will take. Unequal names the draw
// that differs, which is the diagnosis rather than the end of it.
//
//   usage: draw_checksum [--seed N] [--draws N] [--mean-ns N] [--first N]
//
// The default draw count is the full Stage 7 run plus the planned study, the
// figure research/ulp_margin.py reports.

#include "carteret/sampling.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace carteret;

namespace {

struct Options {
    std::uint64_t seed = 20190130;
    std::uint64_t draws = 5'798'550;
    std::uint64_t mean_ns = 250'000'000ULL;
    std::uint64_t first = 0; // print this many individual draws as well
};

} // namespace

int main(int argc, char** argv) {
    Options opt;
    for (int i = 1; i < argc; ++i) {
        const auto next = [&](const char* what) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s needs a value\n", what);
                std::exit(2);
            }
            return argv[++i];
        };
        if (std::strcmp(argv[i], "--seed") == 0)
            opt.seed = std::strtoull(next("--seed"), nullptr, 10);
        else if (std::strcmp(argv[i], "--draws") == 0)
            opt.draws = std::strtoull(next("--draws"), nullptr, 10);
        else if (std::strcmp(argv[i], "--mean-ns") == 0)
            opt.mean_ns = std::strtoull(next("--mean-ns"), nullptr, 10);
        else if (std::strcmp(argv[i], "--first") == 0)
            opt.first = std::strtoull(next("--first"), nullptr, 10);
        else {
            std::fprintf(stderr, "unknown option %s\n", argv[i]);
            return 2;
        }
    }

    Sampler s(opt.seed);
    std::uint64_t h = 1469598103934665603ULL; // FNV-1a offset basis
    std::uint64_t sum = 0;
    for (std::uint64_t i = 0; i < opt.draws; ++i) {
        const std::uint64_t g = s.exponential_ns(opt.mean_ns);
        if (i < opt.first)
            std::printf("draw %llu %llu\n", (unsigned long long)i, (unsigned long long)g);
        h ^= g;
        h *= 1099511628211ULL;
        sum += g;
    }

    std::printf("seed              %llu\n", (unsigned long long)opt.seed);
    std::printf("mean ns           %llu\n", (unsigned long long)opt.mean_ns);
    std::printf("draws             %llu\n", (unsigned long long)opt.draws);
    std::printf("sum ns            %llu\n", (unsigned long long)sum);
    std::printf("draw checksum     %llu\n", (unsigned long long)h);
    return 0;
}
