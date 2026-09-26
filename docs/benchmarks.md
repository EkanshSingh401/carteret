# Benchmarks

A chronological experiment log. One entry per change: the prediction recorded
before the run, what changed, the before and after distributions, the `perf`
delta, and an interpretation.

Every entry stays, including changes that made things worse or changed nothing.
A log containing only improvements is a log that was edited, and the
experiments that failed are generally the more informative half. If the SPSC
ring turns out slower than direct processing, that is the result.

## Method

1. `tools/machine_check.sh` runs first and its output is embedded in the entry.
   An entry without the machine state is not interpretable.
2. Median of at least five runs, with the minimum and maximum. Never a single
   best run.
3. Full distribution: p50, p90, p99, p99.9, p99.99, max. Never a bare mean.
4. Broken out per message type. `A`, `E`, `C`, `X`, `D` and `U` cost
   differently, and a blended figure hides that a delete is cheap while a
   replace is two index operations.
5. `perf stat` alongside wall time: cycles, instructions, IPC, LLC-load-misses,
   branch-misses and dTLB-load-misses, each per message. On the Zen 3
   benchmark host `LLC-load-misses` is not supported and prints nothing, so
   the last-level miss is the demand-fill-from-DRAM event validated in
   `docs/design.md` record 040, and the dTLB miss is `ls_l1_d_tlb_miss.all`.
6. The timer is fenced: `lfence; rdtsc` opens a region and `rdtscp; lfence`
   closes it. `rdtscp` alone waits for earlier instructions but permits later
   ones to begin before the counter is read.
7. Last-level misses are attributed by structure (pool, index, levels,
   bitmaps), by message type, and by order age before any claim is made about
   where the bottleneck is. `perf mem record` supplies the structure
   attribution; `rdpmc` around each message supplies exact per-message counts.
8. Batch-timed and per-message-timed results are reported separately, with the
   gap between them stated. `rdtscp` costs on the order of 25-30 cycles against
   a book update on the order of 300, so the instrument perturbs the
   measurement by close to 10%. That figure is itself measured and reported,
   not assumed.
9. The prediction is written into this file **before** the experiment runs, and
   is not edited afterwards.

## Provenance

Latency figures come from the x86_64 Linux benchmark host and from nowhere
else. The macOS development host runs the harness on a fallback timer as a
smoke test only; no figure it produces appears in this log. CI produces no
timing at all, for the reasons in `.github/workflows/ci.yml`.

## Scope

Stated in every writeup that cites these numbers.

- **Throughput, not responsiveness.** Replaying from a file means messages
  arrive as fast as the code consumes them. There is no queueing, so the
  numbers do not suffer coordinated omission, and equally they say nothing
  about behaviour under a real arrival process.
- **No wire-to-book path.** No NIC, no kernel network stack, no MoldUDP64
  receive. Adding a socket would measure the kernel's network stack, which is a
  different question requiring a different methodology.
- **No order entry, strategy or risk layer.**
- **No kernel bypass.**

## Structural measurements

Counts that are deterministic properties of the code and the input, not of the
machine: overflow rates, probe lengths, memory footprints, message inventories.
Running them on the development host is legitimate because a different machine
would produce the same numbers. They are kept in a separate section so that no
reader mistakes one for a timing.

Peak resident set size is reported because it is a property of the
configuration rather than of the host's speed. It is not a latency figure and
none of the wall-clock columns below are either: they run both book
implementations at once, on an unpinned core.

### S-001 — flat window width against overflow rate

*Process note: no numeric prediction was recorded before this run. Record 018
committed to sweeping window width against overflow rate but named no expected
value, so there is nothing to score the result against. That is a lapse in the
method this file sets out, and it is recorded rather than backfilled. The
remaining experiments carry their predictions.*

- Date: 2026-09-22
- Session: `20190130.BX_ITCH_50`, 74,508,064 book-affecting messages
- Build: Apple Clang 21.0.0, `-O2`, 24-byte order records, multiply-shift hash
- Host: macOS development host. **Structural counts only. No timing here is
  publishable**, and the wall-clock column is present only to show that the
  configuration does not change the work done.
- Verification: every row reported `RESULT: identical` against the reference
  book, so the overflow path is semantically equivalent to the flat path at
  every width.

| Window (ticks) | Price span | Overflow hits | Overflow rate | Peak RSS |
|---:|---:|---:|---:|---:|
| 64 | $0.64 | 32,604,059 | 43.8% | 2,736 MB |
| 128 | $1.28 | 29,452,557 | 39.5% | 2,758 MB |
| 256 | $2.56 | 25,174,798 | 33.8% | 2,797 MB |
| 512 | $5.12 | 19,877,475 | 26.7% | 2,889 MB |
| 1024 | $10.24 | 12,479,804 | 16.7% | 3,065 MB |
| 2048 | $20.48 | 4,709,891 | 6.3% | 3,374 MB |
| 4096 | $40.96 | 1,117,049 | 1.5% | 4,036 MB |

Sub-cent prices account for 131,243 of the overflow hits at every width, since
no cents-indexed window can hold them (record 005). They are a floor of 0.18%
and not the explanation for anything above it.

**Interpretation, and it is not favourable to the design.** The overflow rate
at the default 256-tick window is 33.8%: a third of all orders miss the
structure the fast path exists for. The rate falls slowly up to 512 ticks and
then steeply, which says the miss is not a thin tail of stale far-from-market
orders but a broad distribution the window is simply too narrow to cover.

The cause is in record 018's design: the window origin is fixed at the
symbol's first whole-cent price and never moves. A symbol whose price drifts
over a session walks out of its own window, and a high-priced symbol has an
intraday range wider than 256 cents to begin with. The window does not follow
the price, so the overflow rate is a measure of intraday range rather than of
how far orders sit from the inside.

Reaching a single-digit overflow rate by widening alone costs 2,048 levels per
side per symbol — 98 KB per symbol, and about 640 MB of level arrays across
the 6,678 symbols that are ever two-sided. 4,096 ticks reaches 1.5% and costs
1.3 GB of level arrays, nearly half the process. That is a large price for a
structure whose purpose was to be small and hot: at 4,096 ticks the level
array is far larger than any cache, so the widening that removes the overflow
also removes the locality the flat array was chosen for.

**This is a design finding, not a tuning result.** The candidates are: recentre
the window on the inside when the price drifts; size the window per symbol from
its price level rather than using one constant; or index relative to a moving
reference price instead of an absolute base. Each is a change to record 018 and
gets its own record and its own prediction before it is measured. **The
latency consequence of an overflow hit has not been measured**, so the cost of
33.8% is currently unknown — it is a structural fact in search of a price.

### Timings from the development host are reported in ticks

The development Mac has no invariant TSC read through fenced rdtsc, so its
clock is `std::chrono::steady_clock`. That clock **reports nanoseconds and
resolves 41.667 of them**: it is backed by a 24 MHz counter, and because the
reported unit is finer than the counter the step alternates between 41 and 42.

A percentile printed as "42.0 ns" from that clock is therefore **one tick and
nothing finer**. Two operations differing by 30 ns print identically. Quoting
it in nanoseconds states a precision the instrument does not have, so
`bench_book` prints ticks on any host it cannot publish from, labels the block
resolution-limited, and states the quantum it measured by spinning on the
clock. The quantum is taken as the **median** step rather than the largest: a
spin preempted between two reads returns several quanta, and using the maximum
reported 125 against a true 41.667.

**No nanosecond-denominated latency from this host appears anywhere in this
document.** What the Mac runs are for is structural counts, which are
deterministic and do not depend on the clock at all.

### S-002 — the window slides: overflow falls by a factor of 14

**Prediction, recorded before the run.** S-001 diagnosed the fixed origin as
the cause of the 33.8% overflow rate, on the grounds that the rate was
measuring intraday range rather than distance from the inside. The prediction
was that anchoring the window on the inside would bring the rate into low
single digits at the default width, and that the rebuild cost would be small
because rebuilds are triggered by price movement rather than by message rate.
Both held.

A third observation — that the replay was no slower overall — is recorded here
as a **smoke test only**. It was taken on the development Mac, which this
document does not accept timings from, and it is the kind of claim most likely
to be wrong for a reason a wall clock cannot see: the rebuilds could be cheap
on average and still own the tail. It is carried forward to Stage 4 as a
prediction to be tested on the benchmark host, where bench_book tags the
messages that trigger a recenter and reports their distribution separately
from the rest.

- Date: 2026-09-22
- Session: `20190130.BX_ITCH_50`, 74,508,064 book-affecting messages
- Build: Apple Clang 21.0.0, `-O2`, 24-byte order records, multiply-shift hash
- Host: macOS development host. **Structural counts only. No timing here is
  publishable.**
- Verification: every row reported `RESULT: identical`, so the sliding window
  is semantically equivalent to the fixed one at every width.

| Window | Overflow (fixed) | Overflow (sliding) | Recenters | Levels moved |
|---:|---:|---:|---:|---:|
| 64 | 43.8% | 3.51% | 4,774,566 | 15,561,237 |
| 128 | 39.5% | 2.99% | 4,098,163 | 12,577,697 |
| 256 | 33.8% | **2.37%** | 3,206,627 | 9,318,071 |
| 512 | 26.7% | 1.43% | 1,833,926 | 4,943,986 |
| 1024 | 16.7% | 0.62% | 646,340 | 1,554,702 |
| 2048 | 6.3% | 0.29% | 157,772 | 340,009 |

**Interpretation.** At the default width the overflow rate falls from 33.8% to
2.37%, a factor of 14, for a rebuild cost of 0.13 levels moved per book
message. The remaining 2.37% is dominated by orders genuinely far from the
inside, plus a floor of 0.18% from sub-cent prices that no cents-indexed
window can hold (`docs/design.md` record 005).

The design change that mattered most was giving each side its own origin. A
single origin per symbol must span the spread, and a per-symbol version
measured first produced 4.0M rebuilds moving 26.8M levels against 3.2M moving
9.3M — the spread was entering a decision it has no business in.

**What this does not show.** The latency cost of an overflow hit is still
unmeasured, so the value of removing 31 percentage points of them is unknown.
The wall-clock column runs both book implementations on an unpinned core and
is not a timing. Record 033 has the design reasoning.

## Log

### Stage 4 predictions — recorded 2026-09-26, before any run on market data

Host: AMD Ryzen 9 5950X, boost, PBO and SMT off, cpu8 on the isolated second
CCD (`docs/design.md` record 041). Sessions: `12302019.NASDAQ_ITCH50` for
the full matrix; `20190130.BX_ITCH_50` for the baseline, because S-002's
recenter prediction was made on it. GCC 13.4.0, Release, 5 runs.

**What was seen before writing these.** Nothing from market data. The harness
was smoke-tested on a 3,000,000-message synthetic session from
`gen_synthetic`, whose shape is nothing like a real one (53.8% overflow, six
level windows), and on the pointer chase of record 040. The synthetic run put
the baseline at 378 ns per message and the chained index about 5% slower; it
is mentioned so that no prediction below can be suspected of borrowing from a
run that was not disclosed, and it is not a result.

**The arithmetic the predictions rest on.** The baseline reserves 516 MB
against one CCD's 32 MB (record 041). The index is 256 MB and multiply-shift
scatters references across all of it, so an index probe is expected to fill
from DRAM on nearly every message that performs one. Record 040 puts a
dependent DRAM fill at about 96 ns on this host.

1. **Baseline, NASDAQ.** Batch between 110 and 200 ns per book message.
   DRAM fills per book message between 1.0 and 2.0 overall; at least 0.8 on
   `A`, whose index insert lands on a random slot; `U` the most expensive
   type, at least 1.5× `D`, since it is a remove and an add.
2. **Mode gap.** Per-message p50 is **above** batch mean by more than the
   20 ns instrument, because batch mode lets the core start the next
   message's index miss before the current one retires and the fence forbids
   it.
3. **Attribution by age (record 015).** Messages naming an order younger
   than 1 ms take at most 0.4 DRAM fills; those naming an order older than
   1 s take at least 1.2. The hypothesis that short-lived orders stay
   resident is predicted to hold for the pool and fail for the index, since
   the index is scattered regardless of age.
4. **Attribution by structure (perf mem).** The index owns the largest share
   of DRAM-served load samples, at least 40%; the pool is second; bitmaps
   and symbol headers together under 5%.
5. **Hash policy (record 016).** Identity beats multiply-shift by at least
   15% in batch, because roughly increasing references put recent orders in
   neighbouring slots, and cuts DRAM fills on `A` below 0.4 — while its mean
   probe length is **higher**. `std::hash` is the identity on libstdc++ and
   lands within 2% of identity: the control.
6. **Open addressing against chaining.** Chaining loses by 10-40% in batch:
   a head and a node are two dependent lines where a slot is one, and the
   32 MB head array alone is the size of the L3. It takes at least 0.3 more
   DRAM fills per message that probes.
7. **24 against 32-byte orders (record 017).** Within 5% overall. The 32-byte
   build takes fewer DRAM fills on messages naming orders older than 100 ms,
   by 0.05-0.25 per message — the 25% of records that straddle a line — and
   the difference on orders younger than 10 ms is under 0.05.
8. **SPSC against direct.** Batch throughput within 5% of direct: the parse
   is a small fraction of a message and moving it off the core saves little,
   while every message's bytes now arrive through the L3. The consumer's
   per-message p50 is higher than direct's by 0-15 ns.
9. **Window size.** 512 ticks within 3% of 256 in batch. At 2,048 the
   recenter p50 is at least 4× the 256-tick figure, because a rebuild clears
   the whole window, while recenters are rarer; batch within 5% of 256.
10. **Recenters, separated (S-002 carried forward, NASDAQ and BX).** Under
    0.1% of timed messages. Their p50 is more than 10× the ordinary p50, they
    set the maximum, and removing them moves the all-types p99.9 by less than
    5%. The S-002 claim that sliding is not a throughput regression is not
    testable by this run without a fixed-window build, and is not claimed
    either way.

**The prefetch sweep runs only if attribution warrants it,** by a rule fixed
here: the index or the pool owns at least 40% of DRAM-served load samples,
**and** that structure's address for a message is computable from the message
alone before the book is called — true of the index slot, which is a hash of
the reference, and not of the pool slot, which is read from the index. If
the rule is not met the sweep is not run and the entry says why. If it is
met, the sweep prefetches the index slot of the message *d* ahead, for *d* in
1, 2, 4, 8 and 16, through an `OrderIndex` specialisation in `bench/` that
adds a prefetch to the baseline index and changes nothing else, with the
prediction that some *d* between 4 and 16 cuts batch time by at least 10%.


### 2026-09-26 — Stage 4 on the 5950X: results, scored against the predictions above

- Host: AMD Ryzen 9 5950X, cpu8 on the isolated second CCD, boost, PBO and SMT
  off (`docs/design.md` record 041). GCC 13.4.0, Release, `taskset -c 8`
  (no `chrt`; record 041).
- Sessions: `12302019.NASDAQ_ITCH50` (264,478,375 book messages timed per
  run) and `20190130.BX_ITCH_50`, both SHA-256-matched to `docs/data.md`.
- Every configuration: `--verify` first — each replayed the whole session
  against the reference book, **RESULT: identical** in every case — then 5
  batch and 5 per-message runs, warmup 2,000,000 messages.
- Machine check: **clean before every one of the 24 experiments.** Network
  1.7-14 KB/s at each start, no fetch running. Zero major page faults in any
  timed region. Draw checksum agrees (record 043).
- Raw output: `bench/run_linux.sh` into `results/stage4-nasdaq`,
  `results/stage4-nasdaq-layout` and `results/stage4-bx` on the host.

<details><summary>machine check (baseline; the other 23 are identical but for
the probe's MHz reading)</summary>

```
=== machine ===
  cpu        AMD Ryzen 9 5950X 16-Core Processor
  microcode  0xa201009
  kernel     6.8.0-124-generic
  compiler   g++-13 (Ubuntu 13.4.0-6ubuntu1~22~ppa2) 13.4.0
  cmdline    BOOT_IMAGE=/boot/vmlinuz-6.8.0-124-generic root=UUID=5f217358-fb6e-42fd-92ed-12dbc8acb6bd ro quiet splash isolcpus=8-15 nohz_full=8-15 rcu_nocbs=8-15 vt.handoff=7
  bench core 8
  L3 domain  8-15

=== timing ===
  OK    constant_tsc
  OK    nonstop_tsc
  OK    rdtscp
  OK    lfence always dispatch-serializing (CPUID 8000_0021 EAX[2])

=== isolation ===
  OK    isolcpus covers the whole L3 domain 8-15
  OK    nohz_full covers 8-15
  OK    rcu_nocbs covers 8-15
  OK    no user task has run on the L3 domain
  OK    device irqs routed to the domain but silent (0 delivered): 73 74 75 76 77 78 79 80

=== SMT ===
  OK    SMT control: notsupported
  OK    bench core 8 has no sibling thread

=== frequency ===
  OK    governor=performance on 8-15
  driver     acpi-cpufreq
  OK    AMD boost disabled (every cpufreq boost/cpb control reads 0)
  OK    CPB not advertised: Core Performance Boost is off in firmware
  OK    core 3399.9 MHz against TSC 3400.0 MHz on cpu8 (ratio 1.0000)

=== counters ===
  perf_event_paranoid 1
  OK    user-mode rdpmc granted on a self-monitoring event

=== memory ===
  THP        always [madvise] never
  hugepages  0

Clean. Measurements from this host are interpretable.
```

</details>

#### Failed experiment, kept: the first layout run measured the baseline four times

The first NASDAQ run's `order-32`, `window-512`, `window-1024` and
`window-2048` builds passed their switches to CMake as cache variables, which
never reached the compiler. All four binaries printed `order bytes 24`,
`window ticks 256`, and reported the baseline's 703,924 recenters at every
"width" — which is how it was caught. Their numbers (170.72, 166.57, 167.61
and 167.31 ns) are **four more baseline runs and nothing else**, spread 2.5%
around 167.78, which is a fair statement of run-to-run variation across
separate processes on this host. The build was fixed
(`CMakeLists.txt` forwards both switches) and the four were rerun; the rows
below are the rerun, and each printed its own configuration.

#### Timing, NASDAQ

Batch is the median of 5 runs [min, max]. Per-message is the median across
the 5 runs of each run's all-types percentile. DRAM fills are exact per
message, from the attribution run without the age stream.

| Configuration | Batch ns/msg | p50 | p99 | p99.9 | DRAM fills/msg |
|---|---:|---:|---:|---:|---:|
| baseline: multiply-shift, 24 B, 256 ticks | **167.78** [165.46, 168.92] | 120 | 660 | 1140 | 0.983 |
| hash: identity | **142.36** [139.59, 143.39] | 100 | 620 | 1080 | 0.623 |
| hash: `std::hash` | **142.91** [141.46, 143.11] | 100 | 640 | 1100 | — |
| index: chained | **179.84** [179.19, 180.64] | 130 | 700 | 1190 | 0.944 |
| order: 32 bytes | **171.16** [168.51, 173.59] | 130 | 680 | 1140 | 0.979 |
| window: 512 | **168.23** [166.01, 170.54] | 130 | 580* | 1050 | — |
| window: 1024 | **161.72** [158.17, 165.72] | 130 | 520* | 1130 | — |
| window: 2048 | **155.89** [154.36, 156.01] | 120 | 480* | 760 | — |
| SPSC (book on cpu8, parse on cpu9) | **219.93** [219.12, 224.68] | 130 | 690* | 1180 | — |

*Pooled across runs; the run-by-run median was not extracted for these rows.
Per-message samples have the 20 ns instrument subtracted.

Per type, baseline, pooled (ns): `A` p50 120 / p99.9 900; `D` 130 / 1070;
`E` 130 / 850; `X` 100 / 550; `U` **320** / 1390. `U` is a remove and an
add and costs 2.5× `D` at the median.

BX baseline: **140.54** ns [140.28, 144.02]; p50 120, p99.9 780. Overflow
2.37% and 3,206,627 recenters, reproducing S-002 exactly.

#### Where the misses are (baseline, NASDAQ)

By type (exact, per message): `A` 1.16 DRAM fills, `D` 0.58, `E` 0.79, `U`
2.30. `A` is 44% of messages and 52% of all DRAM fills: the insert of a new
reference into a scattered 256 MB index misses on 91% of adds.

By order age, `E X D U` pooled, with the age stream:

| Age of named order | Messages | DRAM fills/msg | ≥1 DRAM fill |
|---|---:|---:|---:|
| < 1 ms | 16.9 M | **0.19** | 17.7% |
| 1-10 ms | 9.5 M | 0.15 | 13.2% |
| 10-100 ms | 14.7 M | 0.36 | 30.4% |
| 100 ms - 1 s | 24.1 M | 0.33 | 25.8% |
| > 1 s | 78.2 M | **1.39** | 67.3% |

Record 015's hypothesis holds: a message naming an order under 1 ms old
takes a seventh of the DRAM fills of one naming an order over a second old.
But 55% of order-referencing messages name orders older than a second, and
those carry 41% of all DRAM fills. The age stream's own cost: 1.014 against
0.983 fills per message, **3.2%**.

By structure (`perf mem`, IBS, 38,017 user-mode load samples of which **418
were served from DRAM** — a small sample; shares are ±5 points or so):

| Structure | Share of DRAM-served loads | Share of load latency |
|---|---:|---:|
| overflow-map nodes | **48.3%** | 45.3% |
| order index | 26.8% | 16.1% |
| order pool | 15.6% | 16.1% |
| level windows | 5.5% | 5.6% |
| everything else | 3.8% | 16.9% |
| bitmaps | 0.0% | 1.8% |

The overflow map holds 4.9% of orders on this session and owns half the
DRAM-served loads: a `std::map` walk is a chain of dependent node loads, each
a likely miss. The allocation log filled during this pass (more than 4 M
node allocations), so some late nodes resolve to "everything else"; that
bucket took 2.4% of DRAM samples, which bounds the undercount.

`perf stat` (whole process, structures pass plus one batch pass): IPC 1.20
baseline, 1.42 identity, 1.13 chained, 1.20 order-32. DRAM fills fall 28%
from multiply-shift to identity (564 M to 405 M); chained and order-32 are
within 3% of the baseline.

#### The predictions, scored

| # | Prediction | Result | |
|---|---|---|---|
| 1 | batch 110-200 ns; DRAM 1.0-2.0/msg; `A` ≥ 0.8; `U` ≥ 1.5× `D` | 167.78; **0.98**; 1.16; 2.5× | held, but for DRAM, just under the range |
| 2 | per-message p50 above batch mean | 120 vs 167.78 | **failed** — and badly posed: a median against a mean that carries the parse and the tail |
| 3 | < 1 ms ≤ 0.4; > 1 s ≥ 1.2; holds for pool, fails for index | 0.19; 1.39; young orders' index slots are resident too | held for the numbers; **failed** for the index — a slot written under a millisecond ago is still cached, whatever the hash |
| 4 | index ≥ 40% of DRAM loads; pool second; bitmaps+headers < 5% | 26.8%; pool third; 1.2% | **failed**: the overflow map owns 48% |
| 5 | identity ≥ 15% faster; `A` < 0.4 DRAM; longer probes; `std::hash` within 2% | 15.2%; 0.59; 1.131 vs 1.105; 0.4% | held, except `A` stays at 0.59 |
| 6 | chaining 10-40% slower, ≥ 0.3 more DRAM/msg | 7.2% slower, 0.04 **fewer** | **failed** on magnitude and on mechanism: chaining loses on instructions (IPC 1.13), not misses |
| 7 | order-32 within 5%; 0.05-0.25 fewer fills > 100 ms; < 0.05 difference < 10 ms | +2.0%; > 1 s 1.38 vs 1.39; < 1 ms 0.20 vs 0.19 | first and last held; **the straddle saving did not appear** |
| 8 | SPSC within 5%; consumer p50 +0-15 ns | **31% slower**; +10 ns | **failed** on throughput |
| 9 | 512 within 3%; 2048 recenter p50 ≥ 4×; 2048 within 5% | +0.3%; 2.6×; **−7.1%** | first held; wider was faster, and its rebuilds cheaper than predicted |
| 10 | recenters < 0.1%, p50 > 10×, set the max, move p99.9 < 5% | NASDAQ 0.25%, 6.8×, no, 8.8%; BX 4.29%, 3.1×, no, 36% | **failed** on every count |

#### Interpretation

**The contradictions first.** SPSC is the largest: moving the parse to a
second core made the replay 31% slower (219.93 against 167.78 ns). The
consumer's book call is only 10 ns slower at the median, so the loss is in
the handoff — the ring, the cross-core transfer of each message line, and
the consumer waiting on it — not in the book. At this message rate a
single-threaded replay is the faster design.

The overflow map, not the index, is the largest single owner of DRAM-served
loads, from 4.9% of orders. The window experiment says the same thing from
the other side: widening to 2,048 ticks cut overflow from 4.90% to 1.03% and
batch time by 7.1%, although it quadrupled the level arrays to 833 MB. That
is the opposite of S-001's worry that width would cost locality; on this
session the map lookups it removes cost more than the width adds.

The recenter tail matters more than S-002 predicted. On BX, where 4.3% of
messages trigger one, the recenter-free p99.9 is 500 ns against 780 with
them. The claim that sliding is not a throughput regression is still
untested: it needs a fixed-window build, which this run did not include.

**What held.** Identity hashing is 15.2% faster than multiply-shift and cuts
DRAM fills by 37%, because roughly increasing references put recent orders
in neighbouring slots; `std::hash` matches it to 0.4%, as a control should.
Record 016 said the default would be chosen by measurement: the measurement
now favours identity. The default in `include/` is not changed here, because
this stage's scope excludes it; `docs/design.md` record 044 says what
changing it involves.

**The prefetch sweep does not run.** The rule fixed before the run required
the index or the pool to own at least 40% of DRAM-served loads. They own
26.8% and 15.6%. The structure that does qualify on share, the overflow map,
is a pointer chase whose next address is not computable from the message.

**The unexplained tail.** The all-types maximum is about 1.1 ms in every
baseline run, on an `F` message that triggers no recenter, with no page fault
and 151 timer ticks across the whole experiment. It recurs at the same size
across runs, so it is a property of some message rather than noise, and it is
**not yet attributed**. Until it is, the maximum is reported and not
explained.

**What these numbers do not show.** Replay from a file: throughput, not
responsiveness under a real arrival process (Scope, above). One core with a
CCD's L3 to itself. A book 20× that L3.
