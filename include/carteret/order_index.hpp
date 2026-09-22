// carteret/order_index.hpp -- open-addressed map from order reference to pool index.
//
// Order references are day-unique 64-bit values that are roughly but not
// reliably increasing (docs/design.md record 012), so a flat array indexed by
// reference is unsafe and a hash table is required. The hash is a template
// policy because the direction of its effect on cache behaviour is not
// predictable from first principles; see hash_policy.hpp and record 016.
//
// Linear probing with backward-shift deletion rather than tombstones. A
// session performs on the order of 10^7 deletions against a table that never
// grows past its reserved size, and tombstones accumulated over that many
// deletions lengthen every probe for the rest of the session.
//
// Correctness depends on no ordering property of the keys. Probing, wraparound
// and deletion are correct for arbitrary 64-bit values under every policy.

#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace carteret {

// Marks an unoccupied slot. Pool indices are 32-bit and the pool is capped
// well below this value, so it cannot collide with a live entry. Using the
// value rather than the key as the sentinel means any 64-bit reference,
// including zero, is storable.
inline constexpr std::uint32_t kNoOrder = 0xFFFFFFFFu;

template<class HashPolicy>
class OrderIndex {
public:
    struct Slot {
        std::uint64_t key = 0;
        std::uint32_t value = kNoOrder;
        std::uint32_t pad = 0; // keeps the slot 16 bytes: four per cache line
    };
    static_assert(sizeof(Slot) == 16);

    // capacity_hint is rounded up to a power of two and then doubled, giving a
    // load factor at or below 0.5 at the hinted occupancy. Linear probing
    // degrades sharply above that, and the table never rehashes during a
    // session, so the headroom is bought once at startup rather than paid for
    // per insertion.
    explicit OrderIndex(std::size_t capacity_hint) {
        std::size_t cap = 1;
        while (cap < capacity_hint) cap <<= 1;
        cap <<= 1;
        if (cap < 16) cap = 16;
        slots_.assign(cap, Slot{});
        mask_ = cap - 1;
        shift_ = 64u - static_cast<unsigned>(std::countr_one(mask_));
    }

    [[nodiscard]] std::size_t capacity() const noexcept { return slots_.size(); }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] double load_factor() const noexcept {
        return static_cast<double>(size_) / static_cast<double>(slots_.size());
    }

    // Probe statistics, for the hash-policy comparison. Reported alongside
    // timing so a win can be attributed to probe length rather than guessed at.
    [[nodiscard]] std::uint64_t probes() const noexcept { return probes_; }
    [[nodiscard]] std::uint64_t lookups() const noexcept { return lookups_; }
    [[nodiscard]] std::uint64_t insert_failures() const noexcept { return insert_failures_; }
    [[nodiscard]] double mean_probe_length() const noexcept {
        return lookups_ ? static_cast<double>(probes_) / static_cast<double>(lookups_) : 0.0;
    }

    // Returns kNoOrder if the reference is not present.
    [[nodiscard]] std::uint32_t find(std::uint64_t key) const noexcept {
        ++lookups_;
        std::size_t i = home(key);
        for (;;) {
            ++probes_;
            const Slot& s = slots_[i];
            if (s.value == kNoOrder) return kNoOrder;
            if (s.key == key) return s.value;
            i = (i + 1) & static_cast<std::size_t>(mask_);
        }
    }

    // Inserts, or overwrites an existing entry with the same reference. A
    // duplicate live reference means the feed or the reconstruction is wrong;
    // the caller counts it.
    bool insert(std::uint64_t key, std::uint32_t value) noexcept {
        if (value == kNoOrder) return false; // the sentinel is not a storable value
        if (size_ + 1 > slots_.size() / 2) {
            // The table is sized once at startup from a session-wide hint.
            // Exceeding it is a sizing error, reported rather than papered
            // over with a rehash on the hot path.
            ++insert_failures_;
            return false;
        }
        ++lookups_;
        std::size_t i = home(key);
        for (;;) {
            ++probes_;
            Slot& s = slots_[i];
            if (s.value == kNoOrder) {
                s.key = key;
                s.value = value;
                ++size_;
                return true;
            }
            if (s.key == key) {
                s.value = value; // overwrite; size is unchanged
                return true;
            }
            i = (i + 1) & static_cast<std::size_t>(mask_);
        }
    }

    // Returns the removed value, or kNoOrder if the reference was not present.
    std::uint32_t erase(std::uint64_t key) noexcept {
        ++lookups_;
        std::size_t i = home(key);
        for (;;) {
            ++probes_;
            const Slot& s = slots_[i];
            if (s.value == kNoOrder) return kNoOrder;
            if (s.key == key) break;
            i = (i + 1) & static_cast<std::size_t>(mask_);
        }
        const std::uint32_t removed = slots_[i].value;
        erase_at(i);
        --size_;
        return removed;
    }

    void clear() noexcept {
        slots_.assign(slots_.size(), Slot{});
        size_ = 0;
    }

    void reset_stats() noexcept {
        probes_ = 0;
        lookups_ = 0;
    }

    [[nodiscard]] static const char* policy_name() noexcept { return HashPolicy::name; }

private:
    // The policy alone decides the slot. The index applies no mixing of its
    // own: folding the policy's output here would make identity and
    // multiply-shift behave alike and leave the experiment in record 016
    // measuring nothing.
    [[nodiscard]] std::size_t home(std::uint64_t key) const noexcept {
        return HashPolicy::index(key, mask_, shift_);
    }

    // Backward-shift deletion for linear probing. After removing the entry at
    // i, later entries in the same probe run are pulled back when their home
    // position allows it, so no tombstone is left behind and probe runs do not
    // lengthen over a session's worth of deletions.
    void erase_at(std::size_t i) noexcept {
        std::size_t j = i;
        for (;;) {
            slots_[i].value = kNoOrder;
            std::size_t k;
            for (;;) {
                j = (j + 1) & static_cast<std::size_t>(mask_);
                if (slots_[j].value == kNoOrder) return; // run ended; done
                k = home(slots_[j].key);
                // k lies cyclically in (i, j] means slot j is already at or
                // after its home relative to the hole and must not move back.
                const bool cannot_move = (i <= j) ? (i < k && k <= j) : (i < k || k <= j);
                if (!cannot_move) break;
            }
            slots_[i] = slots_[j];
            i = j;
        }
    }

    std::vector<Slot> slots_;
    std::uint64_t mask_ = 0;
    unsigned shift_ = 64;
    std::size_t size_ = 0;
    mutable std::uint64_t probes_ = 0;
    mutable std::uint64_t lookups_ = 0;
    std::uint64_t insert_failures_ = 0;
};

} // namespace carteret
