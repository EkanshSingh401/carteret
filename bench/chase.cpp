// chase -- validates the miss counters against a workload whose answer is known.
//
// A random pointer chase over a buffer of known size is the one access pattern
// whose cache behaviour is predictable from the buffer size alone: every load
// depends on the previous one, so nothing overlaps, and the order is a single
// random cycle over every cache line, so no prefetcher can follow it. A buffer
// that fits in a level stays there after one lap; one that does not misses it
// on almost every load.
//
// The book's per-message attribution rests on the claim that event 0x43 umask
// 0x08 counts demand fills from DRAM for this core alone. This checks that
// claim at the two ends the instructions fix -- a chase far larger than one
// CCD's 32 MB L3 must show a DRAM fill on nearly every load, and one under
// 1 MB must show essentially none -- and at the size that decides the
// placement question: between 32 and 64 MB, where the lines would fit in the
// two CCDs' L3 together but not in one.
//
// Every region is counted twice, through read() on the event descriptors and
// through user-mode rdpmc, so the fast path the book uses is checked against
// the kernel's own count rather than trusted.
//
//   usage: chase [--steps N] <size>[K|M|G] ...
//   run pinned to the bench core: taskset -c 8 build/bench/chase 768K 48M 1G

#include "pmc.hpp"

#include "carteret/bench/timer.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include <sys/mman.h>

using namespace carteret::bench;

namespace {

constexpr std::size_t kLine = 64;

std::size_t parse_size(const char* s) {
    char* end = nullptr;
    const double v = std::strtod(s, &end);
    double mult = 1;
    if (end && (*end == 'K' || *end == 'k')) mult = 1024.0;
    if (end && (*end == 'M' || *end == 'm')) mult = 1024.0 * 1024.0;
    if (end && (*end == 'G' || *end == 'g')) mult = 1024.0 * 1024.0 * 1024.0;
    return static_cast<std::size_t>(v * mult);
}

// One pointer per cache line, linked into a single random cycle (Sattolo's
// algorithm, so the permutation is one cycle rather than several, and a
// short cycle cannot keep a large buffer's working set artificially small).
struct Buffer {
    std::size_t* base = nullptr;
    std::size_t bytes = 0;

    explicit Buffer(std::size_t n_bytes, std::uint64_t seed) : bytes(n_bytes) {
        void* p =
            mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p == MAP_FAILED) {
            std::perror("mmap");
            std::exit(1);
        }
        // 4 KB pages, as the book's structures get from the allocator. A
        // huge-page buffer would take fewer TLB misses than the book does.
        madvise(p, bytes, MADV_NOHUGEPAGE);
        base = static_cast<std::size_t*>(p);
        const std::size_t lines = bytes / kLine;
        std::vector<std::size_t> order(lines);
        for (std::size_t i = 0; i < lines; ++i) order[i] = i;
        std::mt19937_64 rng(seed);
        for (std::size_t i = lines - 1; i > 0; --i) {
            const std::size_t j = static_cast<std::size_t>(rng() % i);
            std::swap(order[i], order[j]);
        }
        constexpr std::size_t kStride = kLine / sizeof(std::size_t);
        for (std::size_t i = 0; i < lines; ++i) {
            base[order[i] * kStride] = order[(i + 1) % lines] * kStride;
        }
    }
    ~Buffer() { munmap(base, bytes); }
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
};

[[gnu::noinline]] std::size_t chase(const std::size_t* base, std::size_t start,
                                    std::uint64_t steps) {
    std::size_t i = start;
    for (std::uint64_t k = 0; k < steps; ++k) i = base[i];
    return i;
}

} // namespace

int main(int argc, char** argv) {
    std::uint64_t steps = 20000000;
    std::vector<std::size_t> sizes;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--steps") == 0 && i + 1 < argc) {
            steps = std::strtoull(argv[++i], nullptr, 10);
        } else {
            sizes.push_back(parse_size(argv[i]));
        }
    }
    if (sizes.empty()) {
        std::fprintf(stderr, "usage: %s [--steps N] <size>[K|M|G] ...\n", argv[0]);
        return 2;
    }

    PmcSet pmc;
    std::string why;
    if (!pmc.open(kFillEvents, kFillEventCount, why)) {
        std::fprintf(stderr, "counters unavailable: %s\n", why.c_str());
        return 1;
    }
    const ClockInfo ci = probe_clock();

    std::printf("pointer chase, %llu dependent loads per size, one random cycle over every "
                "line, 4 KB pages\n",
                static_cast<unsigned long long>(steps));
    std::printf("fills per load, by source (rdpmc; read() in brackets where they differ)\n");
    std::printf("%10s %9s %9s %9s %9s %10s %9s\n", "size", "dram", "l3", "xccx", "l2",
                "ns/load", "rdpmc=rd");

    std::size_t sink = 0;
    for (const std::size_t bytes : sizes) {
        Buffer buf(bytes, 20190130u + bytes);
        // One full lap first, twice over for small buffers, so the measured
        // chase starts from whatever steady state the size allows rather than
        // from a cold cache.
        const std::uint64_t lines = bytes / kLine;
        sink += chase(buf.base, 0, 2 * lines);

        std::uint64_t r0[kFillEventCount], s0[kFillEventCount];
        std::uint64_t r1[kFillEventCount], s1[kFillEventCount];
        for (int e = 0; e < kFillEventCount; ++e) s0[e] = pmc.read_syscall(e);
        for (int e = 0; e < kFillEventCount; ++e) r0[e] = pmc.read(e);
        const std::uint64_t t0 = tick_begin();
        sink += chase(buf.base, 0, steps);
        const std::uint64_t t1 = tick_end();
        for (int e = 0; e < kFillEventCount; ++e) r1[e] = pmc.read(e);
        for (int e = 0; e < kFillEventCount; ++e) s1[e] = pmc.read_syscall(e);

        char label[32];
        if (bytes >= (1u << 30))
            std::snprintf(label, sizeof label, "%zuG", bytes >> 30);
        else if (bytes >= (1u << 20))
            std::snprintf(label, sizeof label, "%zuM", bytes >> 20);
        else
            std::snprintf(label, sizeof label, "%zuK", bytes >> 10);
        std::printf("%10s", label);
        // The rdpmc and read() windows differ by the handful of loads between
        // the two reads, so agreement is judged to within 0.1% of the steps.
        bool agree = true;
        for (int e = 0; e < kFillEventCount; ++e) {
            const double via_rdpmc = static_cast<double>(r1[e] - r0[e]);
            const double via_read = static_cast<double>(s1[e] - s0[e]);
            if (via_read - via_rdpmc > 0.001 * static_cast<double>(steps) ||
                via_rdpmc > via_read + 64)
                agree = false;
            std::printf(" %9.4f", via_rdpmc / static_cast<double>(steps));
        }
        const double ns =
            static_cast<double>(t1 - t0) * ci.ns_per_tick / static_cast<double>(steps);
        std::printf(" %10.2f %9s\n", ns, agree ? "yes" : "NO");
        if (!agree) {
            std::printf("          read():");
            for (int e = 0; e < kFillEventCount; ++e)
                std::printf(" %9.4f",
                            static_cast<double>(s1[e] - s0[e]) / static_cast<double>(steps));
            std::printf("\n");
        }
    }
    std::printf("(checksum %zu)\n", sink);
    return 0;
}
