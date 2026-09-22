# Correctness

Five layers. Each proves something different, each names what it does not
prove, and none of them is sufficient alone. A claim that the reconstruction is
correct means the conjunction of all five, on a named session, at a named
commit.

| Layer | Proves | Does not prove |
|---|---|---|
| 1. Byte fixtures, tiling audit, fuzzing | Field offsets, field widths, framing | Anything about the book |
| 2. Message census against an independent counter | The framing loop walks the file correctly | Field decode |
| 3. LOBSTER row-by-row replay | Book logic against an independently built system | The ITCH parser; queue composition within a level |
| 4. Differential replay against the reference book | The fast book matches the obvious one | That the obvious one is right |
| 5. Continuous invariants and determinism hashes | Internal consistency per message; that a rerun is the same run | Agreement with the venue |

---

## Layer 1 — Byte fixtures, tiling audit, fuzzing

**Fixtures.** `tests/test_wire.cpp` builds one message of each of the 23 ITCH
5.0 types byte by byte from the specification tables, then decodes every field
and asserts its value. Fixtures are constructed rather than captured, so a
transcription error in an offset fails here instead of producing a plausible
wrong value downstream. Every fixture carries a nonzero tracking number, since
the tracking number shares its load with the timestamp
(`docs/design.md` record 002) and a zero would hide a masking error.

**Tiling audit.** `spec.hpp` declares, for each type, the full list of
`{offset, length}` pairs covering its body. A `constexpr` predicate asserts at
compile time that the common header tiles `[0, 11)` and that each body tiles
`[11, kMsgLen[type])` with no gap and no overlap. This catches the class of
transcription error that fixtures miss: a gap between two fields that are each
individually asserted correctly.

*Verification that the audit is load-bearing:* one offset was perturbed by a
single byte and the build was confirmed to fail with the corresponding
`static_assert`, then reverted. This is repeated whenever the audit changes.

**Fuzzing.** `tests/fuzz_frame.cpp` drives `FrameReader` plus handler dispatch
over arbitrary input under libFuzzer. The property is that no input causes a
read outside the buffer, an unterminated loop, or a crash; malformed input must
resolve to one of the counted `FrameStatus` outcomes. Apple Clang does not ship
libFuzzer, so the target builds with Homebrew LLVM (`--preset fuzz` with a
compiler override); `tools/fuzz.sh` runs an extended campaign, and CI runs a
60-second smoke campaign on Ubuntu Clang.

*Does not prove:* that the decoded values mean what the book thinks they mean.
A parser can read every field correctly and still be driving a wrong book.

*Campaign record.* Populated as campaigns run; each line records date,
duration, executions, corpus size, and any input that was added to the corpus.

| Date | Duration | Executions | Corpus | Findings |
|---|---|---:|---:|---|
| *(pending Stage 1)* | | | | |

---

## Layer 2 — Message census against an independent counter

`src/census.cpp` counts messages per type over a session. The count is compared
against `RITCH::count_messages()` from the RITCH R package, which is an
independently written implementation of the same framing.
`tools/census_vs_ritch.sh` runs both and diffs them in one command, sorting
under `LC_ALL=C` so the comparison is collation-independent.

Two gates:

- **Fast gate** — RITCH's bundled `ex20101224.TEST_ITCH_50` fixture, 12,012
  messages. Runs in under a second and catches gross framing errors.
- **Real gate** — a full BX session (`20190130.BX_ITCH_50`; see `docs/data.md`
  for why not the 2017 session). Covers every type that occurs in practice,
  at production message rates, including the session-boundary conditions.

An exact per-type match establishes that the framing loop and the length table
walk the file the same way a second implementation does. Any mismatch stops the
project until it is explained.

*Does not prove:* that any field inside a message is decoded correctly. A
census that agrees perfectly is consistent with every field offset being wrong.

*Results.* Recorded here per session as the gate runs.

| Session | Messages | Types matched | Unknown | Length mismatch | Date |
|---|---:|---|---:|---:|---|
| *(pending Stage 1)* | | | | | |

---

## Layer 3 — LOBSTER row-by-row replay

LOBSTER publishes reconstructed message and orderbook files for NASDAQ
sessions, built by a separate group from the same ITCH feed. Where free sample
files cover a session this project can also reconstruct, an adapter maps this
book's state to LOBSTER's format and diffs row by row.

Mapping notes that matter: LOBSTER event type 5 is an execution of a
**hidden** order and therefore has no effect on the displayed book, matching
this project's treatment of `P` (`docs/design.md` record 011). LOBSTER reports
a fixed number of levels; the comparison is restricted to those levels.

*Does not prove:* anything about the ITCH parser, since a shared
misunderstanding of the feed would agree. It also says nothing about the
composition of a level — LOBSTER's orderbook file reports aggregate depth, not
the FIFO order of the individual orders that make it up, which is precisely the
quantity the queue study depends on.

*Status.* Conditional on free sample availability for a session in
`docs/data.md`. If no overlapping free sample exists, that is documented here
and the layer is skipped rather than approximated.

---

## Layer 4 — Differential replay against the reference book

The reference book (`docs/design.md` record 014) is a deliberately simple
`std::map` / `std::unordered_map` / `std::list` implementation that stays in the
repository permanently. The differential harness replays a session through both
books and, after **every** message, compares:

- the touched level — aggregate share count, order count, and the full sequence
  of order references in FIFO order;
- the best bid and offer on both sides.

Every *N* messages, and again at end of session, it hashes the complete book
state on both sides and compares. On the first divergence it reports the
message index, the message bytes, the symbol, the price, and both books' view
of the level, then stops.

*Does not prove:* that the reference book is right. It proves the two agree.
Layers 2, 3 and 5 are what constrain the reference book itself.

*Results.* Recorded here per session as the gate runs.

| Session | Messages replayed | Divergences | Date |
|---|---:|---:|---|
| *(pending Stage 3)* | | | |

---

## Layer 5 — Continuous invariants and determinism

**Invariants**, checked on every message in debug builds and on a sampled
fraction in release builds:

- Every resting order has strictly positive displayed shares.
- A level's aggregate share count equals the sum of its orders' shares, and its
  order count equals the length of its FIFO list.
- The bitmap's occupancy bit for a level is set if and only if that level's
  order count is nonzero.
- Every order reference in a level's FIFO list resolves through the order index
  to that same order.
- The free list contains no index that is also live in the pool.
- The best bid derived from the bitmap equals the best bid found by a linear
  scan of the level array.

Conditions that are **counted and not asserted** are listed in
`docs/design.md` record 007: crossed and locked books, zero-share cross trades,
orphaned modifies, and `B` or `D` arriving after end of system hours. Their
per-session counts are reported below, because a change in their rate between
runs is itself a signal.

**Determinism.** A SHA-256 (vendored single-file implementation, no external
dependency) is taken over the reconstructed event stream and over the final
book state. The hashes for the synthetic session are committed, and CI fails on
any change that is not explained in the commit message. This is what gives the
phrase "replay-exact" something to fail.

**Feed robustness**, driven against a MoldUDP64 packetisation of the
synthetic stream:

- Sequence tracking across packets.
- Gap detection, which marks the affected symbols stale rather than guessing.
- A/B line arbitration: first arrival wins, duplicates are dropped.
- A loss-injection harness that drops packets at a controlled rate and asserts
  that every induced gap is detected.

GLIMPSE snapshot recovery is out of scope and is not modelled; a real gap in a
real feed is recovered by requesting a snapshot, and this project's gap
handling stops at detection.

*Does not prove:* agreement with the venue. A deterministic reconstruction that
is consistently wrong hashes consistently.

*Results.*

| Session | Crossed | Locked | Zero-share `Q` | Orphaned modifies | Post-`E` `B`/`D` |
|---|---:|---:|---:|---:|---:|
| *(pending Stage 5)* | | | | | |

---

## What the five layers together do not establish

- **Nothing about latency.** Correctness and performance are measured by
  different machinery on different hardware; see `docs/benchmarks.md`.
- **Nothing about hidden liquidity.** Non-displayed interest never enters the
  book by construction, so no layer can detect an error in modelling it.
- **Nothing about a session the archive no longer offers.** See
  `docs/data.md`, *Provenance*.
