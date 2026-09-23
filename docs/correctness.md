# Correctness

Five layers. Each proves something different, each names what it does not
prove, and none of them is sufficient alone. A claim that the reconstruction is
correct means the conjunction of all five, on a named session, at a named
commit.

| Layer | Proves | Does not prove |
|---|---|---|
| 1. Byte fixtures, tiling audit, fuzzing | Field offsets, field widths, framing | Anything about the book |
| 2. Message census against an independent counter | The framing loop walks the file correctly | Field decode |
| 3. LOBSTER row-by-row replay | *(not available — see below)* | — |
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
| 2026-09-22 | 601 s | 407,989,301 | 56 seeds → 550 | none |

Run on the macOS development host with Homebrew Clang 23.1.1 at
679k executions per second, under AddressSanitizer and
UndefinedBehaviorSanitizer. No crash, no timeout, no leak, and no artifact
written. The corpus grew from 56 seeded inputs to 550, adding 4,873 units over
the campaign.

A campaign that finds nothing bounds the defect rate rather than proving
absence, and it covers only what the seeded corpus and the coverage feedback
reached. It says nothing about whether a correctly-parsed field means what the
book thinks it means.

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

## Layer 3 — LOBSTER row-by-row replay — **not available, skipped**

LOBSTER publishes reconstructed message and orderbook files for NASDAQ
sessions, built by a separate group from the same ITCH feed. Diffing this
book's state against theirs row by row would constrain the book logic from
outside this repository, which no other layer does.

**It cannot be run, for two independent reasons.** Both were checked on
2026-09-22 and are recorded here so the layer is not quietly dropped.

**1. There is no free sample to diff against.** `lobsterdata.com` is now a
single-page application with no static sample files; the historical
`/info/DataSamples.php` page returns the application shell for any path, which
is why guessed sample URLs answer `200` with 457 bytes of HTML rather than a
zip. The application bundle shows sample data is gated behind a request flow:
a purchaser of the accompanying book submits an e-mail address and a purchase
proof (order confirmation or receipt), the request is reviewed by hand, and a
time-limited download link is sent. There is no unauthenticated download.

**2. Even with a sample, no session would overlap.** LOBSTER's sample data is
for 2012-06-21. The NASDAQ archive in `docs/data.md` offers no 2012 session:
its ITCH 5.0 sessions run 2018-2020 and 2022 onward, plus a 2010 test fixture
and a 2003 file in the ITCH 2.0 protocol. A row-by-row diff needs the same
session on both sides, and there is no date on which both exist.

**What is lost.** No layer now constrains the book logic against an
independently built reconstruction. Layers 1 and 2 constrain the parser; layer
4 shows the two books here agree; layer 5 shows a replay is internally
consistent and repeatable. A shared misunderstanding of the feed's semantics —
for instance, if `U` did not in fact lose queue priority at an unchanged price
— would pass every remaining layer. That gap is real and is stated in the
README rather than papered over.

**What would close it.** A purchased LOBSTER sample for a session in
`docs/data.md`; or a second independent ITCH reconstruction covering book
state rather than message counts, which `RITCH` does not (it counts messages,
and layer 2 already uses it for that). If either becomes available, the
adapter is a day's work: LOBSTER event type 5 is a hidden execution and has no
displayed-book effect, matching this project's treatment of `P`
(`docs/design.md` record 011), and the comparison restricts to the fixed
number of levels the orderbook file carries.

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

| Session | Crossed | Locked | Zero-share `Q` | Orphaned modifies | Post-`E` book messages | Removed at zero |
|---|---:|---:|---:|---:|---:|---:|
| `20190130.BX_ITCH_50` | 0 | 0 | 0 | 0 | 155 | 1,012,543 |
| `12302019.NASDAQ_ITCH50` | 8,580 | 70 | 12,189 | 0 | 51,200 | 4,270,459 |

The NASDAQ crossed and locked counts are **not** a contradiction of the row
above them, and every one of them is excused by the feed:

| | Crossed | Locked |
|---|---:|---:|
| Symbol not in trading state `T` | 7,945 | 62 |
| Inside a reopening window | 635 | 8 |
| **Unexplained — these fail the gate** | **0** | **0** |

See `docs/design.md` record 036. BX runs no auction and halted no symbol in
this session, so zero is what the same rule predicts there.

**Zero orphaned modifies over 74.5M book messages.** Every `E`, `C`, `X`, `D`
and `U` in the session named a live order. The condition is real — it is
documented at session boundaries — but it does not occur here.

**Crossed and locked books are a gate, and the NASDAQ session is what
calibrated it.** A venue cannot keep its own displayed book uncrossed in a
symbol it is **matching**, so an observation while the venue is matching is a
reconstruction error rather than a market statistic. Two conditions excuse
one, both read from the feed: the symbol is not in trading state `T`, or it
has resumed and its reopening cross has not yet run. **`replay` exits nonzero
on any observation that is neither**, and additionally requires the two books
to agree on the totals, since only the reference book carries trading state.

On BX the check is in a position to fire on 98.7% of book-affecting messages
across 6,678 two-sided symbols, and the minimum spread observed is one
Price(4) unit, so zero there is a result and not a dead counter. On NASDAQ it
fired 8,650 times and every one was excused — which is what turned a rule
about *trading hours* into a rule about *matching*. See `docs/design.md`
records 027 and 036.

**155 book-affecting messages after the `E` end-of-system-hours event on BX,
and 51,200 on NASDAQ**, out of 223 and 51,268 messages of all types. The specification permits this and the book applies
them; stopping at `E` would silently lose them.

---

## Every gate, and the test that proves it can fail

A gate that has only ever been seen to pass supports no claim: it may be
checking nothing. Each gate below has a test that constructs the defect it
exists to catch and requires it to be caught, and where the distinction
matters a positive control requires the clean case to still pass.

This is not a formality. The reopening-window cap failed
`20190130.BX_ITCH_50` on its first run — a session with **zero** crossed and
zero locked observations — because it charged the cap against windows that
were excusing nothing. A gate that fails on clean data is as broken as one
that passes on dirty data, and only running it against both finds that out.
Separately, `Differential` was silently dropping `'H'`, which disabled the
trading-state half of the crossing gate without changing any result.

| Gate | What it fails on | Negative test |
|---|---|---|
| Crossed or locked book | an observation while the venue is matching | `gate_negatives`: `crossing_while_trading_is_caught` |
| — its halt excuse | *(control)* a crossing while halted must pass | `crossing_while_halted_is_excused` |
| — its spec 1.2.2 default | *(control)* a crossing before any `'H'` must pass, counted apart | `crossing_before_any_trading_action_is_excused_and_counted_apart` |
| Reopening window | a crossing after the reopening cross has run | `crossing_after_the_reopening_cross_is_not_excused` |
| — its 100 ms cap | a window closed by timing out rather than by an event | `reopening_window_past_the_cap_fails` |
| — its clean-book case | *(control)* an open window on an uncrossed book must pass | `an_open_window_on_a_clean_book_does_not_trip_the_cap` |
| Operational halt `'h'` | a halt on another market must excuse nothing | `operational_halt_for_another_market_excuses_nothing` |
| Trading-state tracking | a stream whose `'H'` messages never arrive | `stripping_trading_actions_is_an_error_not_a_quiet_pass` |
| Differential comparison | a fast book that drops one removal | `the_differential_detects_a_book_that_is_wrong` |
| Census — unknown type | a type outside ITCH 5.0 | `census_rejects_an_unknown_message_type` |
| Census — length mismatch | a prefix disagreeing with the known body size | `census_rejects_a_length_mismatch` |
| Census — truncation | a body running past the end of the buffer | `census_rejects_a_truncated_body` |
| Census — trailing bytes | bytes after the last whole message | `census_rejects_trailing_bytes` |
| Census — final `'C'` | a session that is a well-formed prefix | `a_session_without_the_final_c_is_a_well_formed_prefix` |
| Determinism | two sessions that differ by one share | `determinism_hash_separates_two_different_sessions` |
| Integrity — length | a short file, the shape of a dropped connection | `integrity_negative`, exit 2 |
| Integrity — gzip stream | a truncated stream | `integrity_negative`, exit 3 |
| Integrity — trailing garbage | appended bytes from a bad continued transfer | `integrity_negative`, exit 4 |
| Integrity — SHA-256 | a wrong digest on an otherwise perfect file | `integrity_negative`, exit 5 |
| Integrity — *(control)* | a good archive must pass | `integrity_negative`, exit 0 |

The differential test substitutes a deliberately wrong book through
`Differential`'s book template parameter, so what is exercised is the
**production** comparator rather than a reimplementation of it in the test.
The integrity checks live in `tools/verify_archive.sh`, which
`tools/fetch_data.sh` calls, so the tested code is the code that runs.

**Not covered this way.** The fast book's structural failure counters — pool
exhaustion, index insert failure, symbol table overflow — are gated in
`replay` but have no negative test yet; provoking them needs a configuration
small enough that the harness itself becomes the subject. They are listed
here as a gap rather than left to be assumed.

---

## What the five layers together do not establish

- **Nothing about latency.** Correctness and performance are measured by
  different machinery on different hardware; see `docs/benchmarks.md`.
- **Nothing about hidden liquidity.** Non-displayed interest never enters the
  book by construction, so no layer can detect an error in modelling it.
- **Nothing about a session the archive no longer offers.** See
  `docs/data.md`, *Provenance*.
