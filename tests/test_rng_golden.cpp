// test_rng_golden -- pins every random draw the project makes.
//
// std::mt19937_64 is specified down to the exact sequence it produces; the
// distributions in <random> are not. Using one made the queue simulator's
// tests pass under Clang and fail under GCC, and it would have made the
// study's published numbers depend on which standard library produced them.
//
// Everything now goes through carteret::Sampler, which is arithmetic on raw
// engine output. This test asserts the exact values that arithmetic produces,
// so a change to a helper, or a platform that disagrees, fails here rather
// than silently shifting a result. CI runs it under both compilers on Linux
// and again on macOS, which is what puts a second math library under the
// exponential draw.
//
// The golden values below were generated once and cross-checked between Apple
// Clang 21.0.0 and GCC 16.2.0, which agreed on every one.

#include "carteret/sampling.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>

using namespace carteret;

namespace {

int failures = 0;

#define CHECK(cond)                                                                            \
    do {                                                                                       \
        if (!(cond)) {                                                                         \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);               \
            ++failures;                                                                        \
        }                                                                                      \
    } while (0)

template<class T>
void expect(const char* what, T got, T want) {
    if (got != want) {
        std::fprintf(stderr, "FAIL %s\n  got  %llu\n  want %llu\n", what,
                     static_cast<unsigned long long>(got),
                     static_cast<unsigned long long>(want));
        ++failures;
    }
}

// The standard fixes the 10,000th output of each engine from its default
// seed. If these fail, the standard library is not conforming and nothing
// below is meaningful.
void test_engines_are_the_specified_ones() {
    std::mt19937_64 a(5489u);
    for (int i = 0; i < 9999; ++i) a();
    expect("mt19937_64 10,000th", a(), std::uint64_t{9981545732273789042ULL});

    std::mt19937 b(5489u);
    for (int i = 0; i < 9999; ++i) b();
    expect("mt19937 10,000th", static_cast<std::uint64_t>(b()), std::uint64_t{4123659995ULL});
}

void test_raw_draws() {
    Sampler s(20190130);
    const std::uint64_t want[] = {20037040275179087ULL, 2545896184327772380ULL,
                                  16657957222919501401ULL, 4553885230644107868ULL};
    for (const std::uint64_t w : want) expect("raw", s.raw(), w);
}

// Compared as bit patterns, not as doubles: a comparison that printed and
// reparsed would hide a last-bit difference, which is exactly the class of
// difference this test exists to catch.
void test_uniform01_is_bit_exact() {
    Sampler s(20190130);
    const std::uint64_t want[] = {4562652082967736320ULL, 4594140473775550604ULL,
                                  4606308987720530078ULL, 4598062327381637196ULL};
    for (const std::uint64_t w : want) {
        const double u = s.uniform01();
        std::uint64_t bits = 0;
        std::memcpy(&bits, &u, sizeof bits);
        expect("uniform01 bits", bits, w);
        CHECK(u >= 0.0 && u < 1.0);
    }
}

// The one helper that depends on the platform's math library, through
// std::log1p. A failure here is most likely a libm difference rather than a
// defect in this code, and it is better to be told than to have the study
// diverge quietly. See sampling.hpp.
void test_exponential_gap() {
    Sampler s(20190130);
    const std::uint64_t want[] = {271701ULL,   37128862ULL,  583337558ULL,
                                  70878232ULL, 343619855ULL, 379242175ULL};
    for (const std::uint64_t w : want)
        expect("exponential_ns", s.exponential_ns(250000000ULL), w);
}

// The exponential is the only draw that touches the platform's math library,
// and the six values above are six chances to notice a libm that disagrees.
// This checksums a million of them instead. A libm differing by one ulp still
// produces identical integers, but the margin is 13.3x rather than the
// thirty-million-fold one a comparison against the quantization step would
// suggest: what decides the outcome is how close the closest of these million
// draws comes to an integer boundary, which is 1.75e-6 ns against 1.31e-7 ns
// for one ulp there. A libm differing by more than about thirteen ulp at the
// wrong point changes one of these draws and fails here. See sampling.hpp.
void test_exponential_sweep_checksum() {
    Sampler s(20190130);
    std::uint64_t h = 1469598103934665603ULL; // FNV-1a offset basis
    for (int i = 0; i < 1000000; ++i) {
        h ^= s.exponential_ns(250000000ULL);
        h *= 1099511628211ULL;
    }
    expect("exponential_ns sweep", h, std::uint64_t{3765140317619766613ULL});
}

// The Bernoulli-proportional model draws one of these per cancel at a live
// synthetic order's price, which is the most frequently taken draw in the
// project. It is integer arithmetic throughout, so it is reproducible by
// construction; these pin it anyway.
void test_bernoulli() {
    Sampler s(20190130);
    const char* want = "010100000000110001100010";
    for (const char* c = want; *c; ++c) {
        expect("bernoulli(1,4)", static_cast<std::uint64_t>(s.bernoulli(1, 4) ? 1 : 0),
               static_cast<std::uint64_t>(*c == '1' ? 1 : 0));
    }

    // The rate is what the model depends on, so it is checked as well as the
    // sequence: 37/100 over a million draws.
    Sampler t(20190130);
    std::uint64_t hits = 0;
    for (int i = 0; i < 1000000; ++i) hits += t.bernoulli(37, 100) ? 1u : 0u;
    expect("bernoulli(37,100) hits", hits, std::uint64_t{370493ULL});

    // Degenerate rates must not consume a draw differently by platform, and
    // must not divide by zero.
    Sampler d(1);
    CHECK(d.bernoulli(0, 10) == false);
    CHECK(d.bernoulli(10, 10) == true);
    CHECK(d.bernoulli(11, 10) == true);
    CHECK(d.bernoulli(1, 0) == false);
    expect("bernoulli consumed nothing for degenerate rates", d.raw(),
           std::uint64_t{2469588189546311528ULL});
}

void test_pick_and_coin() {
    Sampler s(7);
    const std::size_t want[] = {15, 0, 28, 46, 21, 28, 9, 18, 31, 40};
    for (const std::size_t w : want) {
        expect("pick(50)", static_cast<std::uint64_t>(s.pick(w ? 50 : 50)),
               static_cast<std::uint64_t>(w));
    }
    // pick(0) must not divide by zero.
    CHECK(s.pick(0) == 0);

    Sampler t(7);
    const char* want_coins = "1000101010";
    for (const char* c = want_coins; *c; ++c) {
        expect("coin", static_cast<std::uint64_t>(t.coin() ? 1 : 0),
               static_cast<std::uint64_t>(*c == '1' ? 1 : 0));
    }
}

// pick() must stay in range for sizes that are not powers of two, where a
// masking implementation would silently be wrong.
void test_pick_stays_in_range() {
    Sampler s(99);
    for (std::size_t n : {std::size_t{1}, std::size_t{3}, std::size_t{7}, std::size_t{50},
                          std::size_t{1000}, std::size_t{65537}}) {
        for (int i = 0; i < 2000; ++i) CHECK(s.pick(n) < n);
    }
}

} // namespace

int main() {
    test_engines_are_the_specified_ones();
    test_raw_draws();
    test_uniform01_is_bit_exact();
    test_exponential_gap();
    test_exponential_sweep_checksum();
    test_bernoulli();
    test_pick_and_coin();
    test_pick_stays_in_range();

    if (failures == 0) {
        std::printf("all RNG golden tests passed\n");
    } else {
        std::printf("%d failure(s)\n", failures);
    }
    return failures ? 1 : 0;
}
