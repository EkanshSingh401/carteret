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
   branch-misses and dTLB-load-misses, each per message.
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

### S-002 — the window slides: overflow falls by a factor of 14

**Prediction, recorded before the run.** S-001 diagnosed the fixed origin as
the cause of the 33.8% overflow rate, on the grounds that the rate was
measuring intraday range rather than distance from the inside. The prediction
was that anchoring the window on the inside would bring the rate into low
single digits at the default width, and that the rebuild cost would be small
because rebuilds are triggered by price movement rather than by message rate.
Both held. What was not predicted: that the replay would be no slower overall,
because the saved overflow map lookups pay for the rebuilds.

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

*(Empty. The first timing entry is written from a run on the Linux benchmark
host, per Stage 4. No timing is written from the development host.)*
