# Carteret

A NASDAQ TotalView-ITCH 5.0 feed handler and market-by-order limit order book
in C++20, with a nanosecond-resolution benchmark harness and a layered
correctness argument.

Named for the New Jersey data centre the ITCH 5.0 specification names as the
origin of the TotalView feed.

> **Status: scaffold.** The wire layer, framing, spec tables and test harness
> are here and pass. The order book, order index and queue simulator are not
> written yet. See *What is deliberately missing* below.

---

## Results

*(Leave this section empty until the numbers are real. Then lead with the
result, the machine, and the correctness claim, in that order, in the first
five lines. A screener who opens this repo reads exactly that far.)*

| | |
|---|---|
| Messages replayed | — |
| Throughput | — |
| Book update p50 / p99.9 | — |
| Verified against | — |
| Machine | — |

## What this does not do

Stated up front, because it is what makes everything above it credible.

- **No wire path.** No NIC, no kernel network stack, no kernel bypass. The
  benchmark measures book-update cost, not wire-to-book latency.
- **Single-threaded.** Sharding by stock locate is the obvious parallelisation
  and is not done here.
- **Hidden liquidity is invisible.** `P` messages report non-displayed
  executions after the fact; those orders never appear in the book, so fills
  against hidden liquidity are unmodelled.
- **Replay cannot react.** Any simulated order assumes zero market impact,
  which is defensible only for small orders.

## Design notes

[`docs/design.md`](docs/design.md) carries the hot-path rules, the correct-but-surprising
behaviors not to "fix", the queue-simulator fill rules, and the claims that are
still hypotheses until measured.

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Sanitizer build (run this over a full session before believing anything):

```sh
cmake -S . -B build-san -DCMAKE_BUILD_TYPE=Debug -DCARTERET_ASAN=ON
cmake --build build-san -j && ctest --test-dir build-san --output-on-failure
```

### macOS

Everything except benchmarking works on a Mac. Two differences:

- **Sanitizers** work with Apple Clang as-is.
- **libFuzzer** does not ship with Apple Clang. Use Homebrew LLVM for the
  fuzzer only: `brew install llvm`, then configure with
  `-DCMAKE_CXX_COMPILER=$(brew --prefix llvm)/bin/clang++`.
- **Latency numbers** need x86 Linux with core isolation and `perf`. Run
  `tools/machine_check.sh` to see why.

## Data

Free, large, and redistribution-restricted, so it is gitignored and fetched:

```sh
tools/fetch_data.sh                 # BX session, ~54M messages
./build/census data/20170130.BX_ITCH_50
```

Start with BX. Same protocol, a fifth of the messages of a NASDAQ day, so the
edit-run loop is seconds rather than minutes.

## Layout

```
include/carteret/spec.hpp          message types, lengths, field offsets
include/carteret/wire.hpp          BinaryFILE framing, big-endian decode
include/carteret/mapped_file.hpp   read-only mmap
src/census.cpp                     per-type message census (week 1 gate)
tools/gen_synthetic.cpp            deterministic test session writer
tools/machine_check.sh             refuses to let you publish a bad number
tests/test_wire.cpp                byte fixtures + framing tests
docs/design.md                     hot-path rules, fill rules, open hypotheses
```

## Wire format notes

NASDAQ BinaryFILE: each message sits behind a **2-byte big-endian length
prefix**, and a **zero-length prefix marks end of session**.

`FrameReader` treats the prefix as authoritative for advancing, and
additionally checks it against the spec length for known types in one table
lookup. A frame whose length disagrees, or whose type is unknown, is skipped by
its prefix length and counted — never parsed. That one rule is what stops a
single malformed frame from desynchronising the rest of the session.

### Why `memcpy` and not `reinterpret_cast`

The 64-bit order reference sits at offset 11 in every order message, which is
not 8-byte aligned. Casting a pointer into the buffer to `const uint64_t*` is
undefined behaviour twice over: an unaligned load, and a strict-aliasing
violation. It happens to work on x86 and it is still wrong.

`memcpy` into a local plus `__builtin_bswap64` costs nothing. Verified on
GCC 13.3, `-O2`:

```asm
probe(unsigned char const*):
        mov     rax, QWORD PTR 11[rdi]
        bswap   rax
        ret
```

Two instructions. Re-verify this on your own compiler rather than trusting the
listing above.

## The seven spec traps

Where reconstructions silently go wrong. All seven are documented inline in
`spec.hpp` at the relevant field.

1. `E`/`C`/`X` are **cumulative deductions**, not absolute sizes. When displayed
   shares reach zero the order is dead and must be removed even without a `D`.
2. `U` carries **no side, stock or attribution** — retain them from the original
   Add. It mints a new reference and loses queue priority.
3. `E` has **no price field**; use the resting order's price. `C` carries its own
   price plus a Printable flag.
4. `P` (Trade) has **no book effect**. Its order reference has been zero since
   Dec 2010 and its side hardcoded `'B'` since 14 July 2014.
5. `B` (Broken Trade) has no book effect either, and `E`/`C`/`D` can arrive
   *after* the end-of-system-hours event.
6. Order references are **day-unique but not sequential** — a flat array indexed
   by reference is unsafe; you need a hash.
7. Locate codes are **day-scoped**. Cross-session tooling keys on the symbol.

## Correctness

Five layers, each proving something different, each naming what it does *not*
prove.

| Layer | Proves | Does not prove |
|---|---|---|
| 1. Byte fixtures + fuzz | Field offsets and framing | Anything about the book |
| 2. Message census vs published counts | The parser walks the file correctly | Field decode |
| 3. LOBSTER row-by-row replay | Book logic against an independent system | The ITCH parser; queue composition within a level |
| 4. Differential vs `std::map` reference | The fast book matches the obvious one | That the obvious one is right |
| 5. Continuous invariants | Internal consistency per message | Agreement with reality |

Plus a determinism gate: hash the reconstructed event stream and the
end-of-session book state, commit the hashes, and fail CI on any unexplained
change. That is what makes "replay-exact" a claim with teeth.

## What is deliberately missing

The order book, the order index, the prefetch experiment, and the queue
position simulator are **not** in this scaffold, and that is on purpose.

Those are the parts an interviewer will push on until they find the edge of
what you actually understand, and there is no version of this project that
works if the answer is "someone else wrote that part." The scaffold covers what
nobody interviews on: build files, the spec transcription, framing, and a test
harness. Everything that is actually yours to defend is still yours to write.

Suggested order:

1. Census matches a published third-party count, exactly. **Stop here if it
   doesn't.**
2. Naive `std::map` reference book. Slow on purpose.
3. Fast book: flat level array, two-level bitmap for BBO, intrusive FIFO,
   pooled orders, open-addressed order index.
4. Differential test, fast against naive, every message of every session.
5. Benchmark harness, then the optimization log.
6. Queue position simulator and the market-by-price comparison.
