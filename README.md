# Carteret

[![ci](https://github.com/EkanshSingh401/carteret/actions/workflows/ci.yml/badge.svg)](https://github.com/EkanshSingh401/carteret/actions/workflows/ci.yml)

A NASDAQ TotalView-ITCH 5.0 feed handler and market-by-order limit order book
in C++20, with a benchmark harness, a five-layer correctness argument, a
queue-position bias study, and a pre-registered predictive study.

Named for the New Jersey data centre the ITCH 5.0 specification names as the
origin of the TotalView feed.

## Results

| | |
|---|---|
| Messages replayed | — |
| Throughput | — |
| Book update p50 / p99.9 | — |
| Verified against | — |
| Machine | — |

*This table stays empty until the numbers exist. Latency figures come from the
x86_64 Linux benchmark host only; see `docs/benchmarks.md`.*

**Status: in progress.** The wire layer, framing, specification tables, field
layout audit and test harness are implemented and passing. The order book,
order index, benchmark harness and queue simulator are not yet written.

## Limitations

Stated before the results, because they are what makes the results meaningful.

- **No wire path.** No NIC, no kernel network stack, no kernel bypass. The
  benchmark measures book-update cost, not wire-to-book latency.
- **Throughput, not responsiveness.** Replay from a file has no arrival
  process and no queueing, so the numbers say nothing about behaviour under
  load.
- **Single-threaded.** Sharding by stock locate is the obvious parallelisation
  and is out of scope.
- **Hidden liquidity is invisible.** `P` messages report non-displayed
  executions after the fact; those orders never enter the book, so fills
  against hidden liquidity are unmodelled.
- **Replay cannot react.** Any simulated order assumes zero market impact,
  which is defensible only for small orders.
- **The price axis is in cents**, correct for the 2017-2020 sessions used here
  and not for post-amendment tick sizes. See `docs/design.md` record 005.
- **Out of scope:** matching engine, strategy and order-entry layer, web
  interface, containerisation.

## Prior art

Reconstructing exact FIFO queue position from market-by-order data is **not
novel**. HftBacktest ships an `L3FIFOQueueModel`, and its Level-3 tutorial
builds L2 data from L3 to compare the two backtests on crypto futures. This
project implements a known model.

The contribution claimed here is narrower: **the quantified fill-rate and
time-to-fill bias of each market-by-price approximation against exact queue
position, on US-equity ITCH sessions, with the fill rules stated explicitly**,
broken out by queue depth at entry and by symbol. See `docs/design.md` records
020 and 021.

The pre-registered study is **predictive** (interval *t* predicts interval
*t+1*). Cont, Kukanov and Stoikov (2014) report a **contemporaneous** R² of
approximately 65% over 10-second windows. The two are not comparable and are
never presented as though they were; see `docs/design.md` record 023.

## Correctness

Five layers, each proving something different and each naming what it does not
prove. `docs/correctness.md` carries the detail and the per-session results.

| Layer | Proves | Does not prove |
|---|---|---|
| 1. Byte fixtures, tiling audit, fuzz | Field offsets and framing | Anything about the book |
| 2. Census against an independent counter | The framing loop walks the file correctly | Field decode |
| 3. LOBSTER row-by-row replay | Book logic against an independent system | The ITCH parser; queue composition within a level |
| 4. Differential replay against the reference book | The fast book matches the obvious one | That the obvious one is right |
| 5. Continuous invariants and determinism hashes | Internal consistency; that a rerun is the same run | Agreement with the venue |

The reference book — `std::map`, `std::unordered_map`, `std::list` — is
permanent, not a stepping stone. It is both the differential oracle and the
speedup baseline.

### Wire format

NASDAQ BinaryFILE precedes each message with a 2-byte big-endian length; a zero
length marks end of session. `FrameReader` treats the prefix as authoritative
for advancing and additionally checks it against the specification length for
known types in one table lookup. A frame whose length disagrees, or whose type
is unknown, is skipped by its prefix length and counted, never parsed, so that
one malformed frame cannot desynchronise the rest of the session
(`docs/design.md` record 001).

Multi-byte fields are decoded with `memcpy` plus `__builtin_bswap`, never a
pointer cast: the 64-bit order reference at offset 11 is unaligned, and the
cast is both an unaligned load and a strict-aliasing violation. Timestamp and
tracking number come from a single 8-byte load at offset 3. Generated assembly
and the command that produced it are in `docs/design.md` record 002.

### Specification behaviour that reconstructions get wrong

All of the following are documented inline in `spec.hpp` at the relevant field.

1. `E`, `C` and `X` carry **cumulative deductions**, not absolute sizes. An
   order reaching zero displayed shares is removed even without a `D`.
2. `U` carries **no side, stock or attribution**; all three are retained from
   the original Add. It mints a new reference and loses queue priority
   unconditionally, including when the price is unchanged.
3. `E` has **no price field**; the resting order's price applies. `C` carries
   its own price and a Printable flag.
4. `P` has **no book effect**. Its order reference has been zero since December
   2010 and its side field hardcoded `'B'` since 14 July 2014, so trade sign
   cannot be read from it.
5. `B` and `Q` have no book effect either, and **`B` and `D` can arrive after
   the end-of-system-hours event**.
6. Order references are **day-unique but not sequential** — the guarantee that
   they increase was removed in February 2009 — so a flat array indexed by
   reference is unsafe.
7. Stock Locate codes are **session-scoped**. Cross-session tooling keys on the
   symbol.
8. `V` (MWCB Decline Level) uses **Price(8)**, not Price(4).
9. `K` (IPO Quoting Period Update) carries `ReleaseTime` in **seconds** since
   midnight, not nanoseconds, and its Stock Locate is documented as always 0.
10. `Q` (Cross Trade) may legitimately report **zero shares**.

## Data

Free, large, and redistribution-restricted, so `data/` is gitignored and
sessions are fetched, never committed. Checksums are listed in the archive but
not served, so `fetch_data.sh` reports integrity as unverified rather than
skipping the check silently; see `docs/data.md`.

```sh
tools/fetch_data.sh                             # BX 2019-01-30, ~1.1 GB packed
./build/release/census data/20190130.BX_ITCH_50
tools/census_vs_ritch.sh data/20190130.BX_ITCH_50
```

`docs/data.md` lists every available session with its venue, date and size, and
records which the study treats as development and which as held out. It also
records that `20170130.BX_ITCH_50` is no longer served — sessions are withdrawn
from the archive over time, which is why every result names its session and
every fetch verifies a checksum.

BX carries roughly a fifth of a NASDAQ session's messages, which makes it the
right target for correctness iteration. BX is taker-maker and NASDAQ is
maker-taker, so the two are never pooled in a cost-inclusive result
(`docs/design.md` record 022).

## Build

```sh
cmake --preset release
cmake --build --preset release
ctest --preset release
```

Sanitizers, over a full session before any result is believed:

```sh
cmake --preset asan && cmake --build --preset asan && ctest --preset asan
```

Presets: `release`, `asan`, `fuzz`, `bench`. Everything builds warning-free
under GCC 13+ and Clang with libc++, at
`-Wall -Wextra -Wpedantic -Wshadow -Wconversion`. `-march=native` stays off for
anything published.

### macOS

Correctness work runs on macOS unchanged. Two differences:

- **libFuzzer** does not ship with Apple Clang. `brew install llvm`, then
  configure the `fuzz` preset with
  `-DCMAKE_CXX_COMPILER=$(brew --prefix llvm)/bin/clang++`.
- **Latency numbers** require x86_64 Linux with core isolation and `perf`.
  `tools/machine_check.sh` reports whether a host qualifies and refuses to
  pretend otherwise.

## Layout

```
include/carteret/     public headers
src/                  library and tool sources
tools/                census comparison, data fetch, machine check, fuzz driver
tests/                unit, fixture, differential and fuzz targets
bench/                benchmark harness and run scripts
research/             Python analysis scripts
docs/design.md        numbered design decision records
docs/benchmarks.md    chronological experiment log
docs/correctness.md   the five verification layers
docs/data.md          available sessions, venues, provenance
docs/preregistration.md
docs/figures/         committed figures; never market data
```
