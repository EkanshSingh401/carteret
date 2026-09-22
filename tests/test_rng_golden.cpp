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
// than silently shifting a result. CI runs it under both compilers.
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
    test_pick_and_coin();
    test_pick_stays_in_range();

    if (failures == 0) {
        std::printf("all RNG golden tests passed\n");
    } else {
        std::printf("%d failure(s)\n", failures);
    }
    return failures ? 1 : 0;
}
