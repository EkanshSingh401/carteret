// pmc.hpp -- per-message hardware event counts, read with user-mode rdpmc.
//
// `perf stat` counts a whole run, which says how many misses there were but
// not which messages took them. Attribution by message type and by order age
// needs a count per message, and a read() system call per message would cost
// more than the message. So each event is opened on the calling thread with
// perf_event_open, its control page is mapped, and the counter is read from
// user space with rdpmc under the kernel's sequence lock -- a few tens of
// cycles, with no transition into the kernel.
//
// The event encodings are Zen 3's (PPR for AMD Family 19h, event 0x43,
// LsDmndFillsFromSys): demand data-cache fills, split by where the line came
// from. They were chosen and validated with bench/chase.cpp before any book
// measurement used them; see docs/design.md record 040.
//
//   0x0843  from DRAM or IO on the local node   (the last-level miss)
//   0x0243  from the L3, or an L2 in the same CCX
//   0x0443  from a cache in a different CCX
//   0x0143  from the core's own L2
//
// Zen 3's L3 is per CCD, and its L3 PMU counts for the whole CCD rather than
// per thread, so an L3-side count cannot be attributed to one message. The
// core-side fill events can: they count fills into this core's L1D, caused by
// this core's demand loads.
//
// Linux x86-64 only. Everywhere else PmcSet::open() returns false and the
// caller reports that the counters are unavailable.

#pragma once

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#if defined(__x86_64__) && defined(__linux__)
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <x86intrin.h>
#define CARTERET_HAVE_PMC 1
#else
#define CARTERET_HAVE_PMC 0
#endif

namespace carteret::bench {

struct PmcEvent {
    const char* name;
    std::uint64_t config; // raw core PMU encoding: umask << 8 | event
};

// The validated set. DRAM first, because it is the count the attribution is
// about; the others say where the lines that did NOT come from DRAM came from.
inline constexpr PmcEvent kFillEvents[] = {
    {"dram", 0x0843}, // ls_dmnd_fills_from_sys.mem_io_local
    {"l3", 0x0243},   // ls_dmnd_fills_from_sys.int_cache
    {"xccx", 0x0443}, // ls_dmnd_fills_from_sys.ext_cache_local
    {"l2", 0x0143},   // ls_dmnd_fills_from_sys.lcl_l2
};
inline constexpr int kFillEventCount = sizeof(kFillEvents) / sizeof(kFillEvents[0]);

class PmcSet {
public:
    PmcSet() = default;
    PmcSet(const PmcSet&) = delete;
    PmcSet& operator=(const PmcSet&) = delete;
    ~PmcSet() { close_all(); }

    // Opens every event on the calling thread, user mode only. Returns false,
    // with a reason, if any event cannot be opened or rdpmc is not granted --
    // a partial set would attribute some sources and silently omit others.
    bool open(const PmcEvent* events, int n, std::string& why) {
#if CARTERET_HAVE_PMC
        for (int i = 0; i < n; ++i) {
            perf_event_attr pa;
            std::memset(&pa, 0, sizeof pa);
            pa.size = sizeof pa;
            pa.type = PERF_TYPE_RAW;
            pa.config = events[i].config;
            pa.exclude_kernel = 1;
            pa.exclude_hv = 1;
            // Pinned, so the event is never multiplexed off its counter: a
            // multiplexed count is an estimate, and a per-message delta taken
            // across a reschedule is not even that.
            pa.pinned = 1;
            const int fd = static_cast<int>(syscall(SYS_perf_event_open, &pa, 0, -1, -1, 0));
            if (fd < 0) {
                why = std::string("perf_event_open failed for ") + events[i].name + ": " +
                      std::strerror(errno);
                close_all();
                return false;
            }
            void* page = mmap(nullptr, static_cast<std::size_t>(sysconf(_SC_PAGESIZE)),
                              PROT_READ, MAP_SHARED, fd, 0);
            if (page == MAP_FAILED) {
                why = std::string("mmap of the control page failed for ") + events[i].name;
                ::close(fd);
                close_all();
                return false;
            }
            auto* pg = static_cast<perf_event_mmap_page*>(page);
            if (!pg->cap_user_rdpmc || pg->index == 0) {
                why = std::string("rdpmc not granted for ") + events[i].name;
                munmap(page, static_cast<std::size_t>(sysconf(_SC_PAGESIZE)));
                ::close(fd);
                close_all();
                return false;
            }
            fds_.push_back(fd);
            pages_.push_back(pg);
            names_.push_back(events[i].name);
        }
        return true;
#else
        (void)events;
        (void)n;
        why = "hardware counters are read only on x86-64 Linux";
        return false;
#endif
    }

    [[nodiscard]] int size() const noexcept { return static_cast<int>(pages_.size()); }
    [[nodiscard]] const char* name(int i) const noexcept {
        return names_[static_cast<std::size_t>(i)];
    }

    // Current value of counter i, read in user space. The sequence lock is
    // the kernel's protocol for the control page: if the event was moved to
    // another counter between reading its index and reading the counter, the
    // lock changes and the read is retried. The count is the kernel's base
    // offset plus the hardware counter, sign-extended from its width.
    [[gnu::always_inline]] std::uint64_t read(int i) const noexcept {
#if CARTERET_HAVE_PMC
        const perf_event_mmap_page* pg = pages_[static_cast<std::size_t>(i)];
        std::uint32_t seq;
        std::uint64_t count;
        do {
            seq = pg->lock;
            __asm__ volatile("" ::: "memory");
            const std::uint32_t idx = pg->index;
            count = static_cast<std::uint64_t>(pg->offset);
            if (idx) {
                const unsigned width = pg->pmc_width;
                std::int64_t pmc =
                    static_cast<std::int64_t>(__rdpmc(static_cast<int>(idx - 1)));
                pmc = static_cast<std::int64_t>(static_cast<std::uint64_t>(pmc)
                                                << (64 - width)) >>
                      (64 - width);
                count += static_cast<std::uint64_t>(pmc);
            }
            __asm__ volatile("" ::: "memory");
        } while (pg->lock != seq);
        return count;
#else
        (void)i;
        return 0;
#endif
    }

    // The same count through the kernel, for checking the rdpmc path against
    // it. A system call; never on a per-message path.
    [[nodiscard]] std::uint64_t read_syscall(int i) const noexcept {
#if CARTERET_HAVE_PMC
        std::uint64_t v = 0;
        if (::read(fds_[static_cast<std::size_t>(i)], &v, sizeof v) != sizeof v) return 0;
        return v;
#else
        (void)i;
        return 0;
#endif
    }

private:
    void close_all() noexcept {
#if CARTERET_HAVE_PMC
        for (auto* p : pages_) munmap(p, static_cast<std::size_t>(sysconf(_SC_PAGESIZE)));
        for (int fd : fds_) ::close(fd);
#endif
        pages_.clear();
        fds_.clear();
        names_.clear();
    }

#if CARTERET_HAVE_PMC
    std::vector<perf_event_mmap_page*> pages_;
#else
    std::vector<void*> pages_;
#endif
    std::vector<int> fds_;
    std::vector<const char*> names_;
};

} // namespace carteret::bench
