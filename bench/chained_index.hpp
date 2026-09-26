// chained_index.hpp -- a separately chained order index, for the comparison
// against open addressing.
//
// FastBook names its index as OrderIndex<HashPolicy>, so a book with a
// different index is obtained by specialising OrderIndex for a policy type of
// its own. Nothing in include/ changes, the book is the same code line for
// line, and the differential harness drives it exactly as it drives the
// default: Differential<ChainedMultiplyShift> replays a session against the
// reference book through this index.
//
// The comparison is about memory, not about probe counts. Open addressing
// with linear probing finds a key in the line its hash names, almost always;
// chaining reads a bucket head and then follows a pointer to a node, which is
// a second, DEPENDENT load to an unrelated line. Against that, the chained
// table is smaller at the same capacity -- 4-byte heads plus 16-byte nodes
// sized to the live population, against 16-byte slots at a load factor of at
// most 0.5 -- so more of it can stay resident. Which effect wins against a
// 32 MB L3 is the question, and it is measured rather than argued.
//
// Nodes come from a pool with a LIFO free list, as orders do in the book, so
// the node a delete frees is the one the next insert takes. Inserts go to the
// head of the chain, so a recent reference is found first.

#pragma once

#include "carteret/hash_policy.hpp"
#include "carteret/order_index.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace carteret {

namespace bench {

// The bucket is chosen by multiply-shift, the default policy, so the only
// difference from the baseline book is how collisions are resolved.
struct ChainedMultiplyShift {
    static constexpr const char* name = "chained (multiply-shift)";
    [[gnu::always_inline]] static std::size_t bucket(std::uint64_t ref,
                                                     unsigned shift) noexcept {
        return static_cast<std::size_t>((ref * MultiplyShiftHash::kPhi) >> shift);
    }
};

} // namespace bench

template<>
class OrderIndex<bench::ChainedMultiplyShift> {
public:
    struct Node {
        std::uint64_t key = 0;
        std::uint32_t value = kNoOrder;
        std::uint32_t next = kNoOrder;
    };
    static_assert(sizeof(Node) == 16);

    // The same capacity as the open-addressed index at the same hint: that
    // table refuses an insert beyond half of twice the rounded hint, so both
    // hold exactly bit_ceil(hint) entries. Buckets equal nodes, a load factor
    // of at most one.
    explicit OrderIndex(std::size_t capacity_hint) {
        std::size_t cap = std::bit_ceil(capacity_hint < 16 ? std::size_t{16} : capacity_hint);
        heads_.assign(cap, kNoOrder);
        nodes_.resize(cap);
        shift_ = 64u - static_cast<unsigned>(std::countr_zero(cap));
        free_ = kNoOrder;
        for (std::size_t i = cap; i-- > 0;) {
            nodes_[i].next = free_;
            free_ = static_cast<std::uint32_t>(i);
        }
    }

    [[nodiscard]] std::size_t capacity() const noexcept { return nodes_.size(); }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] double load_factor() const noexcept {
        return static_cast<double>(size_) / static_cast<double>(heads_.size());
    }
    // A probe is a line read: the bucket head, then each node visited.
    [[nodiscard]] std::uint64_t probes() const noexcept { return probes_; }
    [[nodiscard]] std::uint64_t lookups() const noexcept { return lookups_; }
    [[nodiscard]] std::uint64_t insert_failures() const noexcept { return insert_failures_; }
    [[nodiscard]] double mean_probe_length() const noexcept {
        return lookups_ ? static_cast<double>(probes_) / static_cast<double>(lookups_) : 0.0;
    }

    [[nodiscard]] std::uint32_t find(std::uint64_t key) const noexcept {
        ++lookups_;
        ++probes_;
        for (std::uint32_t i = heads_[bench::ChainedMultiplyShift::bucket(key, shift_)];
             i != kNoOrder; i = nodes_[i].next) {
            ++probes_;
            if (nodes_[i].key == key) return nodes_[i].value;
        }
        return kNoOrder;
    }

    bool insert(std::uint64_t key, std::uint32_t value) noexcept {
        if (value == kNoOrder) return false;
        ++lookups_;
        ++probes_;
        std::uint32_t& head = heads_[bench::ChainedMultiplyShift::bucket(key, shift_)];
        for (std::uint32_t i = head; i != kNoOrder; i = nodes_[i].next) {
            ++probes_;
            if (nodes_[i].key == key) {
                nodes_[i].value = value; // overwrite; size is unchanged
                return true;
            }
        }
        if (free_ == kNoOrder) {
            ++insert_failures_;
            return false;
        }
        const std::uint32_t n = free_;
        free_ = nodes_[n].next;
        nodes_[n].key = key;
        nodes_[n].value = value;
        nodes_[n].next = head;
        head = n;
        ++size_;
        return true;
    }

    std::uint32_t erase(std::uint64_t key) noexcept {
        ++lookups_;
        ++probes_;
        std::uint32_t* link = &heads_[bench::ChainedMultiplyShift::bucket(key, shift_)];
        while (*link != kNoOrder) {
            ++probes_;
            Node& nd = nodes_[*link];
            if (nd.key == key) {
                const std::uint32_t n = *link;
                const std::uint32_t removed = nd.value;
                *link = nd.next;
                nd.value = kNoOrder;
                nd.next = free_;
                free_ = n;
                --size_;
                return removed;
            }
            link = &nd.next;
        }
        return kNoOrder;
    }

    void clear() noexcept {
        heads_.assign(heads_.size(), kNoOrder);
        free_ = kNoOrder;
        for (std::size_t i = nodes_.size(); i-- > 0;) {
            nodes_[i] = Node{};
            nodes_[i].next = free_;
            free_ = static_cast<std::uint32_t>(i);
        }
        size_ = 0;
    }

    void reset_stats() noexcept {
        probes_ = 0;
        lookups_ = 0;
    }

    [[nodiscard]] static const char* policy_name() noexcept {
        return bench::ChainedMultiplyShift::name;
    }

private:
    std::vector<std::uint32_t> heads_;
    std::vector<Node> nodes_;
    unsigned shift_ = 64;
    std::uint32_t free_ = kNoOrder;
    std::size_t size_ = 0;
    mutable std::uint64_t probes_ = 0;
    mutable std::uint64_t lookups_ = 0;
    std::uint64_t insert_failures_ = 0;
};

} // namespace carteret
