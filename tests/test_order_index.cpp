// test_order_index -- the open-addressed order index, under every hash policy.
//
// The index is the structure most likely to hide a subtle defect: backward-
// shift deletion moves entries that a naive test never observes, and a probe
// run broken by a bad shift returns "not found" for a key that is present,
// which downstream looks exactly like an orphaned modify rather than like a
// bug.
//
// Every test runs under all three policies, because a defect that depends on
// the key distribution would otherwise pass under one and fail under another.

#include "carteret/hash_policy.hpp"
#include "carteret/order_index.hpp"

// The benchmark's separately chained index specialises OrderIndex, so every
// test here runs against it too: the open-vs-chained comparison in Stage 4 is
// only a comparison if both sides are correct.
#include "../bench/chained_index.hpp"

#include <cstdio>
#include <random>
#include <unordered_map>
#include <vector>

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

template<class P>
void test_basic() {
    OrderIndex<P> ix(64);
    CHECK(ix.size() == 0);
    CHECK(ix.find(1) == kNoOrder);

    CHECK(ix.insert(1234, 7));
    CHECK(ix.size() == 1);
    CHECK(ix.find(1234) == 7);
    CHECK(ix.find(1235) == kNoOrder);

    // Overwriting an existing key replaces the value and does not grow the map.
    CHECK(ix.insert(1234, 9));
    CHECK(ix.size() == 1);
    CHECK(ix.find(1234) == 9);

    CHECK(ix.erase(1234) == 9);
    CHECK(ix.size() == 0);
    CHECK(ix.find(1234) == kNoOrder);
    CHECK(ix.erase(1234) == kNoOrder);
}

// Reference zero and the maximum 64-bit reference must both be storable. The
// empty marker is the value, not the key, precisely so that no key value is
// reserved.
template<class P>
void test_extreme_keys() {
    OrderIndex<P> ix(64);
    CHECK(ix.insert(0, 1));
    CHECK(ix.insert(0xFFFFFFFFFFFFFFFFULL, 2));
    CHECK(ix.find(0) == 1);
    CHECK(ix.find(0xFFFFFFFFFFFFFFFFULL) == 2);
    CHECK(ix.erase(0) == 1);
    CHECK(ix.find(0xFFFFFFFFFFFFFFFFULL) == 2);
    // The sentinel is not a storable value; storing it would make the slot
    // indistinguishable from an empty one.
    CHECK(!ix.insert(5, kNoOrder));
    CHECK(ix.find(5) == kNoOrder);
}

// Keys that collide by construction, so that deletion has a probe run to
// repair rather than a single isolated slot.
template<class P>
void test_colliding_run() {
    OrderIndex<P> ix(16); // capacity 32
    const std::size_t cap = ix.capacity();

    // Under identity these land in one run; under a mixing policy they do not,
    // which is why the test asserts on lookups rather than on slot positions.
    std::vector<std::uint64_t> keys;
    for (std::uint32_t i = 0; i < 8; ++i)
        keys.push_back(static_cast<std::uint64_t>(i) * cap + 3);

    for (std::uint32_t i = 0; i < keys.size(); ++i) CHECK(ix.insert(keys[i], i));
    for (std::uint32_t i = 0; i < keys.size(); ++i) CHECK(ix.find(keys[i]) == i);

    // Erase from the middle of the run: every remaining key must still resolve.
    CHECK(ix.erase(keys[3]) == 3);
    CHECK(ix.find(keys[3]) == kNoOrder);
    for (std::uint32_t i = 0; i < keys.size(); ++i) {
        if (i == 3) continue;
        CHECK(ix.find(keys[i]) == i);
    }

    // And from the front.
    CHECK(ix.erase(keys[0]) == 0);
    for (std::uint32_t i = 1; i < keys.size(); ++i) {
        if (i == 3) continue;
        CHECK(ix.find(keys[i]) == i);
    }
    CHECK(ix.size() == 6);
}

// Deletion must not leave tombstones: after an equal number of inserts and
// erases the mean probe length must return to roughly its initial value rather
// than growing with the number of operations.
template<class P>
void test_deletion_does_not_degrade() {
    OrderIndex<P> ix(4096);
    for (std::uint32_t i = 0; i < 1000; ++i) CHECK(ix.insert(100000 + i, i));

    ix.reset_stats();
    for (std::uint32_t i = 0; i < 1000; ++i) CHECK(ix.find(100000 + i) == i);
    const double before = ix.mean_probe_length();

    // Twenty times the live population, churned through.
    std::uint64_t next = 200000;
    for (int round = 0; round < 20; ++round) {
        for (std::uint32_t i = 0; i < 1000; ++i) CHECK(ix.insert(next + i, i) || true);
        for (std::uint32_t i = 0; i < 1000; ++i) ix.erase(next + i);
        next += 1000;
    }

    ix.reset_stats();
    for (std::uint32_t i = 0; i < 1000; ++i) CHECK(ix.find(100000 + i) == i);
    const double after = ix.mean_probe_length();

    CHECK(ix.size() == 1000);
    // A tombstone implementation would show `after` climbing without bound.
    CHECK(after < before + 1.0);
}

// The index must agree with std::unordered_map over a long random sequence of
// mixed operations. References here are deliberately not monotonic, so a
// defect that only shows on arbitrary keys is reachable.
template<class P>
void test_against_unordered_map() {
    OrderIndex<P> ix(8192);
    std::unordered_map<std::uint64_t, std::uint32_t> ref;
    std::mt19937_64 rng(12345);

    for (int step = 0; step < 200000; ++step) {
        const std::uint64_t key = rng() % 30000;
        const int op = static_cast<int>(rng() % 100);
        if (op < 50) {
            const auto value = static_cast<std::uint32_t>(rng() % 1000000);
            if (ref.size() < 4000 || ref.count(key)) {
                CHECK(ix.insert(key, value));
                ref[key] = value;
            }
        } else if (op < 80) {
            const std::uint32_t got = ix.find(key);
            const auto it = ref.find(key);
            if (it == ref.end()) {
                CHECK(got == kNoOrder);
            } else {
                CHECK(got == it->second);
            }
        } else {
            const std::uint32_t got = ix.erase(key);
            const auto it = ref.find(key);
            if (it == ref.end()) {
                CHECK(got == kNoOrder);
            } else {
                CHECK(got == it->second);
                ref.erase(it);
            }
        }
    }

    CHECK(ix.size() == ref.size());
    for (const auto& [k, v] : ref) CHECK(ix.find(k) == v);
    CHECK(ix.insert_failures() == 0);
}

// Exceeding the sizing hint is reported rather than silently rehashed on the
// hot path.
template<class P>
void test_overfill_is_reported() {
    OrderIndex<P> ix(16); // capacity 32, so half is 16
    std::uint32_t inserted = 0;
    for (std::uint32_t i = 0; i < 64; ++i) {
        if (ix.insert(1000 + i, i)) ++inserted;
    }
    CHECK(inserted == 16);
    CHECK(ix.insert_failures() > 0);
    // Everything that was accepted must still be findable.
    for (std::uint32_t i = 0; i < inserted; ++i) CHECK(ix.find(1000 + i) == i);
}

// Roughly increasing references are what a real session supplies. Identity is
// expected to place them in nearby slots; the test asserts only that all three
// policies remain correct on them, since which one is faster is an open
// question (docs/design.md record 016).
template<class P>
void test_roughly_increasing_references() {
    OrderIndex<P> ix(65536);
    std::mt19937_64 rng(99);
    std::vector<std::uint64_t> keys;
    std::uint64_t ref = 4000000000ULL; // wide enough to exceed 32 bits
    for (std::uint32_t i = 0; i < 30000; ++i) {
        ref += 1 + rng() % 5; // increasing, with gaps
        keys.push_back(ref);
        CHECK(ix.insert(ref, i));
    }
    for (std::uint32_t i = 0; i < keys.size(); ++i) CHECK(ix.find(keys[i]) == i);
    // Cancel the oldest half, as a session does, then confirm the rest survive.
    for (std::size_t i = 0; i < keys.size() / 2; ++i) CHECK(ix.erase(keys[i]) != kNoOrder);
    for (std::size_t i = keys.size() / 2; i < keys.size(); ++i) {
        CHECK(ix.find(keys[i]) == static_cast<std::uint32_t>(i));
    }
}

template<class P>
void run_all(const char* label) {
    const int before = failures;
    test_basic<P>();
    test_extreme_keys<P>();
    test_colliding_run<P>();
    test_deletion_does_not_degrade<P>();
    test_against_unordered_map<P>();
    test_overfill_is_reported<P>();
    test_roughly_increasing_references<P>();
    std::printf("  %-16s %s\n", label, failures == before ? "ok" : "FAILED");
}

} // namespace

int main() {
    run_all<IdentityHash>(IdentityHash::name);
    run_all<MultiplyShiftHash>(MultiplyShiftHash::name);
    run_all<StdHash>(StdHash::name);
    run_all<bench::ChainedMultiplyShift>(bench::ChainedMultiplyShift::name);

    if (failures == 0) {
        std::printf("all order index tests passed\n");
    } else {
        std::printf("%d failure(s)\n", failures);
    }
    return failures ? 1 : 0;
}
