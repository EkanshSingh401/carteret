// attribution.hpp -- demand-fill counts per message, by type, by recenter and
// by the age of the order the message names.
//
// docs/design.md record 015 requires last-level misses to be attributed three
// ways before any claim is made about where the bottleneck is. Two of the
// three are made here, from exact counts: each book message is bracketed by
// user-mode rdpmc reads of the fill events validated in record 040, and the
// counts are filed under the message's type and, for a message that names a
// resting order, under that order's age. The third -- by structure -- comes
// from perf mem, because a counter says how many lines were filled but not
// which structure they belonged to.
//
// Order age needs the time each order was added, which the book does not
// expose, so the harness keeps its own table. Kept alongside the book, that
// table doubled the DRAM fills it was measuring -- 2.33 per message against
// 1.12 without it on a synthetic session -- because its own lookups evicted
// the book's lines. So ages are computed in a separate pass that is not
// counted, and written as one byte per message; the counted pass reads them
// as a sequential stream, one line per 64 messages, which the prefetcher
// hides. The counted pass is run again without the stream, and the difference
// between the two is what the stream still costs, reported rather than
// assumed.

#pragma once

#include "pmc.hpp"

#include "carteret/fast_book.hpp"
#include "carteret/messages.hpp"
#include "carteret/order_index.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace carteret::bench {

// Record 015's buckets, plus one for a message whose order the tracker never
// saw added (an orphan, or an order added during a window the tracker did not
// cover).
inline constexpr int kAgeBuckets = 6;
inline constexpr const char* kAgeLabels[kAgeBuckets] = {"<1ms",     "1-10ms", "10-100ms",
                                                        "100ms-1s", ">1s",    "unseen"};

struct FillTotals {
    std::uint64_t messages = 0;
    std::array<std::uint64_t, kFillEventCount> fills{};
    std::array<std::uint64_t, 5> dram_hist{}; // messages with 0, 1, 2, 3, 4+ DRAM fills

    void add(const std::uint64_t* d) noexcept {
        ++messages;
        for (int e = 0; e < kFillEventCount; ++e) fills[static_cast<std::size_t>(e)] += d[e];
        const std::uint64_t dram = d[0];
        ++dram_hist[dram < 4 ? dram : 4];
    }
    [[nodiscard]] double per_message(int e) const noexcept {
        return messages ? static_cast<double>(fills[static_cast<std::size_t>(e)]) /
                              static_cast<double>(messages)
                        : 0.0;
    }
};

// Add time and remaining shares per live order, keyed by reference.
class AgeTracker {
public:
    explicit AgeTracker(std::size_t capacity) : ix_(capacity) {
        slots_.resize(capacity);
        for (std::size_t i = capacity; i-- > 0;) {
            slots_[i].next = free_;
            free_ = static_cast<std::uint32_t>(i);
        }
    }

    void add(Ref ref, std::uint64_t ts, std::uint32_t shares) noexcept {
        erase(ref);
        if (free_ == kNoOrder) return;
        const std::uint32_t s = free_;
        free_ = slots_[s].next;
        slots_[s].ts = ts;
        slots_[s].shares = shares;
        if (!ix_.insert(ref, s)) release(s);
    }

    [[nodiscard]] int bucket(Ref ref, std::uint64_t now) const noexcept {
        const std::uint32_t s = ix_.find(ref);
        if (s == kNoOrder) return kAgeBuckets - 1;
        const std::uint64_t then = slots_[s].ts;
        const std::uint64_t age = now > then ? now - then : 0;
        if (age < 1000000ULL) return 0;
        if (age < 10000000ULL) return 1;
        if (age < 100000000ULL) return 2;
        if (age < 1000000000ULL) return 3;
        return 4;
    }

    void reduce(Ref ref, std::uint32_t qty) noexcept {
        const std::uint32_t s = ix_.find(ref);
        if (s == kNoOrder) return;
        if (qty >= slots_[s].shares) {
            ix_.erase(ref);
            release(s);
        } else {
            slots_[s].shares -= qty;
        }
    }

    void erase(Ref ref) noexcept {
        const std::uint32_t s = ix_.erase(ref);
        if (s != kNoOrder) release(s);
    }

private:
    struct Slot {
        std::uint64_t ts = 0;
        std::uint32_t shares = 0;
        std::uint32_t next = kNoOrder;
    };
    void release(std::uint32_t s) noexcept {
        slots_[s].next = free_;
        free_ = s;
    }
    OrderIndex<MultiplyShiftHash> ix_;
    std::vector<Slot> slots_;
    std::uint32_t free_ = kNoOrder;
};

inline constexpr std::uint8_t kNoAge = 0xFF; // a message that names no resting order

// The uncounted pass: replays the session through the age table alone and
// records, for every message the book would count, the age bucket of the
// order it names.
struct AgeLabeller {
    AgeTracker ages;
    std::vector<std::uint8_t> out;

    explicit AgeLabeller(std::size_t capacity) : ages(capacity) {}

    void label(int bucket) { out.push_back(static_cast<std::uint8_t>(bucket)); }

    void on(AddOrder v) {
        out.push_back(kNoAge);
        ages.add(v.order_ref(), v.ts(), v.shares());
    }
    void on(AddOrderMpid v) {
        out.push_back(kNoAge);
        ages.add(v.order_ref(), v.ts(), v.shares());
    }
    void on(OrderExecuted v) {
        label(ages.bucket(v.order_ref(), v.ts()));
        ages.reduce(v.order_ref(), v.executed_shares());
    }
    void on(OrderExecutedPrice v) {
        label(ages.bucket(v.order_ref(), v.ts()));
        ages.reduce(v.order_ref(), v.executed_shares());
    }
    void on(OrderCancel v) {
        label(ages.bucket(v.order_ref(), v.ts()));
        ages.reduce(v.order_ref(), v.cancelled_shares());
    }
    void on(OrderDelete v) {
        label(ages.bucket(v.order_ref(), v.ts()));
        ages.erase(v.order_ref());
    }
    void on(OrderReplace v) {
        label(ages.bucket(v.old_order_ref(), v.ts()));
        ages.erase(v.old_order_ref());
        ages.add(v.new_order_ref(), v.ts(), v.shares());
    }
    void on(Trade) { out.push_back(kNoAge); }
    void on(CrossTrade) { out.push_back(kNoAge); }
    void on(BrokenTrade) { out.push_back(kNoAge); }
};

// Applies each message to the book between two reads of every fill counter.
template<class Book>
struct AttributedHandler {
    Book book;
    const PmcSet* pmc = nullptr;
    const std::uint8_t* ages = nullptr; // from AgeLabeller; null for no age attribution
    std::size_t cursor = 0;
    std::uint64_t warmup_left = 0;
    std::size_t peak_live = 0;

    std::array<FillTotals, 256> by_type{};
    std::array<std::array<FillTotals, kAgeBuckets>, 256> by_type_age{};
    FillTotals recenter, ordinary, all;

    AttributedHandler(FastBookConfig cfg, std::uint64_t warmup)
        : book(cfg), warmup_left(warmup) {}

    template<class V>
    [[gnu::always_inline]] void measured(V v) {
        const std::uint8_t age = ages ? ages[cursor] : kNoAge;
        ++cursor;
        if (warmup_left) {
            --warmup_left;
            book.on(v);
            return;
        }
        std::uint64_t a[kFillEventCount], b[kFillEventCount];
        const std::uint64_t r0 = book.counters().recenters;
        for (int e = 0; e < kFillEventCount; ++e) a[e] = pmc->read(e);
        book.on(v);
        for (int e = 0; e < kFillEventCount; ++e) b[e] = pmc->read(e);
        const std::uint64_t r1 = book.counters().recenters;
        std::uint64_t d[kFillEventCount];
        for (int e = 0; e < kFillEventCount; ++e) d[e] = b[e] - a[e];
        const unsigned char t = v.type();
        by_type[t].add(d);
        if (age != kNoAge) by_type_age[t][age].add(d);
        (r1 != r0 ? recenter : ordinary).add(d);
        all.add(d);
        const std::size_t live = book.live_orders();
        if (live > peak_live) peak_live = live;
    }

    void on(SystemEvent v) { book.on(v); }
    void on(AddOrder v) { measured(v); }
    void on(AddOrderMpid v) { measured(v); }
    void on(OrderExecuted v) { measured(v); }
    void on(OrderExecutedPrice v) { measured(v); }
    void on(OrderCancel v) { measured(v); }
    void on(OrderDelete v) { measured(v); }
    void on(OrderReplace v) { measured(v); }
    void on(Trade v) { measured(v); }
    void on(CrossTrade v) { measured(v); }
    void on(BrokenTrade v) { measured(v); }
};

} // namespace carteret::bench
