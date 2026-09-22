# Benchmarks

A chronological log. One entry per change: what changed, the before and after
distribution, the `perf` delta, and one sentence of interpretation.

**Every entry stays, including the ones where the change made things worse or
did nothing.** A log that contains only wins reads as a log that was edited, and
the failed experiments are usually the more interesting half. If the SPSC ring
turns out slower than direct processing, that is a finding; write it down.

## Rules

1. Run `tools/machine_check.sh` first and paste its output. No machine, no entry.
2. Median of at least 5 runs, with min and max. Never a single best run.
3. Full distribution: p50, p90, p99, p99.9, p99.99, max. Never a bare mean.
4. Break out per message type. `A`, `E`, `C`, `X`, `D`, `U` cost differently and
   a blended number hides that a delete is cheap and a replace is two operations.
5. Report `perf stat` alongside wall time: cycles, instructions, IPC,
   LLC-load-misses, branch-misses, dTLB-load-misses, all per message.
6. Fence the timer: `lfence; rdtsc` to open a region, `rdtscp; lfence` to close
   it. `rdtscp` alone waits for earlier instructions but lets later ones start
   before it reads the counter.
7. Attribute LLC misses by structure (pool / index / levels / bitmap), by
   message type, and by order age before writing any claim about where the
   bottleneck is. `perf mem record` for structure; `rdpmc` per message for
   exact counts.
8. Report batch-timed and per-message-timed numbers separately, and state the
   gap between them. `rdtscp` costs ~25-30 cycles against a ~300-cycle book
   update, so the instrument perturbs the measurement by close to 10%. Naming
   that is worth more than any optimization below it.

## What this benchmark does NOT measure

State this in every writeup. It is what makes everything above it credible.

- **Throughput, not responsiveness.** Replaying from a file means messages
  arrive as fast as the code consumes them. There is no queueing, so the numbers
  do not suffer from coordinated omission, and they also say nothing about
  behaviour under a real arrival process.
- **No wire-to-book path.** No NIC, no kernel network stack, no MoldUDP64
  receive. Adding a socket would measure the kernel's network stack, which is a
  different question with a different methodology.
- **No order entry, strategy or risk.** Different system.
- **No kernel bypass.** No hardware for it, and it would not change what is
  being measured here.

## Log

### 0000 - baseline: naive reference implementation
- date:
- machine: *(paste machine_check.sh output)*
- change: `std::map<Price, Level>` + `std::unordered_map<uint64_t, Order>` +
  `std::list` FIFO. Deliberately the textbook structure.
- result:
- interpretation: this is the number every later speedup is measured against,
  and the implementation stays in the repo so the comparison is checkable.

### 0001 - *(next entry)*
