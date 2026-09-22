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

## Log

*(Empty. The first entry is written from a run on the Linux benchmark host, per
Stage 4. No entry is written from the development host.)*
