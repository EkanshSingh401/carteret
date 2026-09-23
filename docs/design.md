# Design decision records

Numbered records, each with the context that forced a choice, the choice, the
alternatives rejected, the cost and failure modes accepted, and the evidence —
a measurement or a specification citation — that justifies it. Records are
append-only: a superseded record is marked superseded and keeps its number.

`docs/preregistration.md` cites record 020 (queue-simulator fill rules) by
number. A pre-registration is only meaningful against a committed, versioned
definition, so any change to record 020 after the registration commit must be
reported in the study writeup.

Status values: **in force** (implemented), **committed** (decided, not yet
implemented), **hypothesis** (stated as a prediction, awaiting measurement).

---

## 001 — BinaryFILE framing: prefix authoritative, spec length cross-checked

**Status:** in force.

**Context.** NASDAQ BinaryFILE precedes each message with a 2-byte big-endian
length and terminates the session with a zero length. The message type byte
also implies a length, since all 23 ITCH 5.0 types are fixed-width. Two
independent sources of truth for the same quantity can disagree, and the
handling of that disagreement determines whether a single corrupt frame costs
one message or the remainder of the session.

**Decision.** The prefix alone advances the cursor. For a known type the prefix
is additionally compared against `kMsgLen[type]` in one table lookup. A frame
whose type is unknown, or whose length disagrees with the table, is consumed by
its prefix length, counted, and never decoded.

**Alternatives considered.**
- *Type-implied length advances the cursor.* An unknown type byte then leaves
  no way to resynchronise, because the reader does not know how far to skip.
- *No cross-check.* A frame of the wrong length would be decoded, reading
  fields from the wrong offsets and producing plausible-looking garbage that no
  downstream layer can distinguish from valid data.
- *Abort on disagreement.* Loses the remainder of a session over one frame and
  removes the ability to quantify how often the condition occurs.

**Consequences.** Cost is one array load and one comparison per message on the
hot path. The table is 256 bytes and stays resident. Failure mode: a corrupt
*prefix* still desynchronises the reader, because nothing else is authoritative
for the cursor; the reader will then report a run of unknown types or a
truncation rather than silently misparsing. The two counters
(`FrameReader::unknown()` and `::mismatch()`) are the observable signal, and the
census reports both.

**Evidence.** Specification section 1 (BinaryFILE framing) for the prefix and
the zero terminator; sections 1.1-1.8 for the fixed widths. Checked by
`tests/test_wire.cpp::test_framing`, which interleaves a wrong-length `D`, an
unknown type byte, and a valid `D` and asserts that both counters increment and
the valid message is still decoded.

---

## 002 — Field decode by memcpy and byte swap, not by pointer cast

**Status:** in force.

**Context.** The 64-bit order reference sits at offset 11 of every order
message. Message bodies are not themselves aligned in the mapped file, and
offset 11 is not a multiple of 8 in any case.

**Decision.** Every multi-byte field is read with `std::memcpy` into a local of
the target type followed by `__builtin_bswap{16,32,64}`. The timestamp and
tracking number are read together: the 2-byte tracking number at offset 3 and
the 6-byte timestamp at offset 5 are exactly 8 contiguous bytes, so one 8-byte
load at offset 3 plus one swap yields the timestamp in the low 48 bits and the
tracking number in the high 16.

**Alternatives considered.**
- *`reinterpret_cast<const std::uint64_t*>(p + 11)`.* Undefined behaviour twice
  over: an unaligned load, and an access to an object of one type through a
  glvalue of another. It executes correctly on x86 and on ARMv8 for ordinary
  loads, which is what makes it durable — the defect surfaces only under
  optimisation, under a sanitizer, or on a target with strict alignment.
- *A packed struct overlaid on the buffer.* Still an aliasing violation, and
  it makes the field layout implicit rather than checkable.
- *Copying six timestamp bytes into a zeroed `uint64_t`.* Costs a second load;
  see the evidence below.

**Consequences.** No portability or correctness cost. The layout is expressed
as named offsets in `spec.hpp` rather than as a struct, so the tiling audit
(record 003) can check it at compile time. Failure mode: an incorrect offset
constant is not caught by the type system, which is why every type has a
byte-level fixture.

**Evidence.** Measured on this repository's development host, Apple Clang
21.0.0, `-O2`, arm64:

```
clang++ -std=c++20 -O2 -Iinclude -S probe.cpp

probe_ref:                  probe_ts:                          probe_tr:
  ldur x8, [x0, #11]          ldur x8, [x0, #3]                  ldur x8, [x0, #3]
  rev  x0, x8                 and  x8, x8, #0xffffffffffff0000   rev  x8, x8
  ret                         rev  x0, x8                        lsr  x0, x8, #48
                              ret                                ret
```

One load and one byte-reversal for the 64-bit reference. For the timestamp the
compiler folds the 48-bit mask to a pre-swap clear of the low 16 bits, giving
three instructions and a single load. The rejected copy-six-bytes form compiles
to five instructions and two loads on the same compiler:

```
ts_copy6:
  ldurh w8, [x0, #9]
  ldur  w9, [x0, #5]
  orr   x8, x9, x8, lsl #32
  rev   x8, x8
  lsr   x0, x8, #16
  ret
```

The x86-64 listing has not been reproduced on this host and is not quoted here;
it is regenerated and recorded when the benchmark host is available. Whenever a
decode helper changes, the generated assembly is regenerated and this record
updated.

---

## 003 — Field layout audited at compile time by a tiling check

**Status:** in force.

**Context.** `spec.hpp` transcribes 23 tables of offsets and lengths by hand.
A single wrong integer produces field values that are wrong but well-formed,
and byte-level fixtures only catch the fields a fixture happens to assert.

**Decision.** Each message type declares a `Field{offset, length}` array
covering every field of its body. A `constexpr` function `tiles()` returns true
only if the fields, in order, cover `[start, end)` with no gap and no overlap.
`static_assert` applies it to the common header over `[0, 11)` and to each
message body over `[11, kMsgLen[type])`.

**Alternatives considered.**
- *Fixtures alone.* They check the fields the author remembered to assert; a
  gap between two correctly-asserted fields is invisible.
- *A runtime check at startup.* Catches the same errors later, and costs a test
  run rather than a compile.
- *Generating the offsets from a table.* Removes the transcription risk but
  moves the specification out of the header, where it is read alongside the
  decode code.

**Consequences.** The build fails on any offset that does not tile, which
includes most single-digit transcription errors. It does not catch two
compensating errors, a field with the right extent and the wrong name, or a
wrong *type* (integer versus Price(4)). Those remain the fixtures' job.

**Evidence.** The check was verified to be load-bearing by perturbing one
offset and confirming the build fails before reverting; see
`docs/correctness.md`.

---

## 004 — Integer ticks for money throughout

**Status:** in force.

**Context.** ITCH Price(4) is an unsigned 32-bit integer with four implied
decimal places; Price(8) is the 64-bit form. A reconstruction that converts to
floating point for arithmetic accumulates representation error in quantities
that are exact on the wire.

**Decision.** Prices stay integral end to end in C++: Price(4) as `uint32_t`,
Price(8) as `uint64_t`, and the level axis indexed in cents. No floating-point
type appears in any price or notional computation. Conversion to a decimal
representation happens once, at the boundary where data is exported for
analysis.

**Alternatives considered.**
- *`double` for prices.* Exact for the integers in range, but arithmetic on
  derived quantities (mid, spread, notional) is not, and the error is
  systematically signed in ways that flatter a backtest.
- *A fixed-point wrapper type.* Equivalent arithmetic with added API surface;
  the raw integer is already the specification's representation.

**Consequences.** Any quantity that is genuinely fractional — a mid price at a
half-tick, a per-share fee — is represented in a finer integer unit and the
unit is named at the definition site. Failure mode: mixing units silently. The
axis unit is fixed by record 005 and asserted rather than inferred.

**Evidence.** Specification, Data Types: "Price(4) ... an unsigned integer with
4 implied decimal places", maximum 200000.0000 (`0x77359400`), encoded in
`kMaxPrice4`.

---

## 005 — The price axis indexes in cents, not half-cents

**Status:** in force.

**Context.** The 2024 Reg NMS Rule 612 amendments introduce a half-penny
minimum increment for tick-constrained NMS stocks. Compliance was set for
3 November 2025 and has been delayed to November 2026.

**Decision.** The flat level array indexes in whole cents. This is correct for
the 2017-2020 sessions this project uses, in which the minimum increment is one
cent for stocks priced at or above $1.00.

**Alternatives considered.**
- *Index in half-cents now.* Doubles the level array for no gain on this data,
  and doubles the cache footprint of the structure the BBO search walks.
- *Index in the raw Price(4) unit (hundredths of a cent).* A 10000x larger
  axis; unusable as a flat array.

**Consequences.** Sub-penny prices that do occur — retail price improvement,
midpoint prints at a half cent — do not land on an index. Those arrive as `P`
messages, which have no book effect (record 007), so the displayed book is
unaffected. Failure mode: applying this code to post-amendment data without
changing the axis would round half-cent displayed quotes. The axis unit is
therefore a deliberate, documented property and not an implementation detail to
be modernised in passing.

**Evidence.** Rule 612 as amended, adopting release 34-99929 (2024); compliance
date extension. Session dates are recorded in `docs/data.md`.

---

## 006 — The parser is templated on its handler

**Status:** committed.

**Context.** Dispatch runs once per message, on the order of 2.7 x 10^8 times
for a NASDAQ session. The handler is known at compile time in every use in this
repository: census, reference book, fast book, differential harness, queue
simulator.

**Decision.** `Parser<Handler>` takes the handler as a template parameter and
dispatches on the type byte with a `switch`. The handler supplies an overload
per message type; unhandled types are absorbed by a defaulted catch-all.

**Alternatives considered.**
- *An abstract base class with virtual `on_*` methods.* An indirect call per
  message that the inliner cannot see through and the indirect branch predictor
  must learn; it also forces the typed view to be materialised even when the
  handler ignores the field.
- *`std::function` callbacks.* Adds an allocation at registration, a second
  indirection, and defeats inlining identically.
- *A function-pointer table indexed by type byte.* Removes the switch's range
  check but keeps the indirect call.

**Consequences.** Every handler instantiates its own copy of the parser, so
translation units grow and compile time rises. Handlers cannot be selected at
runtime, which this repository never needs. Failure mode: none for correctness.

**Evidence.** Pending. The dispatch cost is measured against a virtual-call
variant in `docs/benchmarks.md` when the benchmark host is available; the
claim that it matters is a hypothesis until then.

---

## 007 — Feed conditions that look like defects are counted, never repaired

**Status:** in force for what exists; committed for the book.

**Context.** Several conditions in a correctly reconstructed ITCH book violate
textbook invariants. Asserting on them turns a valid session into a crash;
silently repairing them destroys the evidence that they occurred.

**Decision.** Each of the following is counted, attributed to a symbol and a
timestamp, and reported at end of session. None is corrected and none is
asserted against.

- **Crossed and locked books** around auctions and halts.
- **Cross Trade (`Q`) with zero shares**, which the specification permits when
  order interest was insufficient.
- **Orphaned modifies** — `E`, `C`, `X`, `D` or `U` naming a reference with no
  live Add — at session boundaries.
- **Absence of hidden liquidity.** Non-displayed interest never enters the
  book; `P` reports its executions after the fact.
- **`B` and `D` arriving after the `E` end-of-system-hours event.**

**Alternatives considered.**
- *Assert.* Loses sessions and produces no measurement of frequency.
- *Repair — synthesise the missing Add, uncross the book.* Produces a book that
  is self-consistent and not the venue's, which is the failure the differential
  layer exists to detect.

**Consequences.** Downstream analysis must handle a crossed book rather than
assume it away. The counts themselves are a result: `docs/correctness.md`
reports them per session, and a change in their rate between sessions is a
signal that something in the reconstruction changed.

**Evidence.** Specification sections 1.4.4 (Cross Trade, zero-share case),
1.4.1 (Trade, non-displayable), and the System Event Message table.

---

## 008 — `E`, `C` and `X` deduct cumulatively; zero shares removes the order

**Status:** committed.

**Context.** Order Executed, Order Executed With Price and Order Cancel carry a
share count that is a *decrement*, not a new total. Order Replace, by contrast,
carries a new total.

**Decision.** `E`, `C` and `X` subtract their share count from the resting
order's displayed quantity. An order reaching zero displayed shares is removed
from the book immediately, without waiting for a `D`, because no `D` is
guaranteed to follow.

**Alternatives considered.**
- *Treat the count as a new total.* Produces monotonically wrong depth on every
  partially-executed order.
- *Remove only on `D`.* Leaves zero-quantity orders resting at the front of the
  queue, which corrupts both depth and every queue-position calculation behind
  them.

**Consequences.** The book must tolerate a later `D` or `X` naming an already
removed reference; that is one of the orphan classes in record 007.

**Evidence.** Specification sections 1.4.1 (Order Executed: "the number of
shares executed"), 1.4.3 (Order Cancel: "the number of shares being removed"),
and 1.4.5 (Order Replace carries the new total).

---

## 009 — `U` retains side, stock and attribution, and loses queue priority

**Status:** committed.

**Context.** Order Replace carries an old reference, a new reference, a new
total quantity and a new price. It carries no side, no stock symbol and no
attribution.

**Decision.** The three absent fields are retained from the original Add. The
new reference supersedes the old for every later message. The order is placed
at the **back** of the FIFO queue at its new price, even when the price is
unchanged.

**Alternatives considered.**
- *Retain queue position when the price is unchanged.* Incorrect: the venue
  treats a replace as a cancel and a new entry, so priority is lost
  unconditionally. Assuming otherwise inflates every simulated fill rate.
- *Look the side up from the trading state or the BBO.* Guesswork, wrong for
  orders away from the inside.

**Consequences.** The order index must resolve the old reference before the new
one is inserted, so a replace is two index operations rather than one. This is
visible per-type in the benchmark breakdown.

**Evidence.** Specification section 1.4.5.

---

## 010 — `E` uses the resting order's price; `C` carries its own

**Status:** committed.

**Context.** Order Executed has no price field. Order Executed With Price has
an execution price and a Printable flag.

**Decision.** An `E` executes at the price of the order it names, read from the
book. A `C` executes at its own `ExecPrice`; when Printable is `N` the
execution is not published to the consolidated tape but the book effect is
identical.

**Alternatives considered.**
- *Use the BBO as the execution price for `E`.* Wrong for any execution away
  from the inside, which is exactly the population a queue study cares about.

**Consequences.** Trade reconstruction depends on the book being correct at the
moment of the execution, so an error in the book propagates into the trade
record rather than being caught independently.

**Evidence.** Specification sections 1.4.1 and 1.4.2.

---

## 011 — `P`, `Q` and `B` have no book effect

**Status:** committed.

**Context.** Trade (non-cross), Cross Trade and Broken Trade are time-and-sales
messages. Applying them to the book double-counts every execution already
reported by `E` and `C`.

**Decision.** None of the three modifies the book. `P`'s order reference has
been zero since December 2010 and its side field has been hardcoded `'B'`
regardless of the resting side since 14 July 2014, so neither field is used for
anything, including trade signing.

**Alternatives considered.**
- *Sign trades from `P.Side`.* Produces a trade-sign series that is constant,
  and an imbalance feature built on it is noise.
- *Infer the aggressor side by comparing the print to the prevailing BBO.* A
  defensible estimator, but an estimator; if used it is named as one and its
  error rate reported.

**Consequences.** Executions against non-displayed liquidity are observable in
time and sales and absent from the book, which is the limitation stated in
README.md and in record 020 rule 4.

**Evidence.** Specification sections 1.4.1 (Trade), 1.4.4 (Cross Trade), 1.4.6
(Broken Trade), and the field notes on `P`.

---

## 012 — Order references are day-unique and not sequential

**Status:** committed.

**Context.** The specification's guarantee that order reference numbers
increase was removed in February 2009. In practice the references in a session
are roughly increasing, but nothing enforces it.

**Decision.** The order index is a hash table. Correctness does not depend on
any ordering property of references: probing, wraparound and deletion are
correct for arbitrary 64-bit values. Performance work may exploit rough
monotonicity (record 016); correctness may not.

**Alternatives considered.**
- *A flat array indexed by reference.* Requires the references to be dense and
  bounded, which they are not.
- *A flat array indexed by `reference - session_minimum`.* Requires knowing the
  minimum before the first message and assumes density; a single outlying
  reference sizes the array for the session.

**Consequences.** Every order message costs a hash lookup. This is the
structure record 015 identifies as the most likely locality problem.

**Evidence.** Specification, Order Reference Number field note.

---

## 013 — Locate codes are session-scoped; cross-session tooling keys on symbol

**Status:** committed.

**Context.** Stock Locate is assigned per session by the venue. The same code
denotes different symbols on different days, and the same symbol takes
different codes.

**Decision.** Within a session, the locate code is the index into per-symbol
state, which is what it is for. Anything that spans sessions — the data
catalogue, the microstructure export, the study's feature files — keys on the
eight-character symbol resolved from that session's Stock Directory messages.

**Alternatives considered.**
- *Key everything on symbol.* Costs a string comparison or a symbol-table
  lookup on the hot path for no in-session benefit.

**Consequences.** Every session must be read from its own `R` messages before
its locate codes mean anything, which makes the Stock Directory pass mandatory
rather than optional.

**Evidence.** Specification section 1.2.1.

---

## 014 — The reference book is permanent

**Status:** committed.

**Context.** A differential test needs an oracle. An oracle that is deleted
once the fast implementation passes cannot detect a regression introduced
afterwards.

**Decision.** The `std::map` / `std::unordered_map` / `std::list`
implementation stays in the repository indefinitely. It is written for
transparency rather than speed, implements every semantic rule in records 007
through 013, and serves as both the differential oracle and the speedup
baseline.

**Alternatives considered.**
- *An external reference implementation.* Introduces a dependency whose
  semantics must themselves be verified, and whose disagreements are harder to
  attribute.
- *Delete it after the fast book passes.* Removes the regression detector at
  the moment it starts being useful.

**Consequences.** Two implementations of every semantic rule must be kept in
step, which doubles the cost of a semantic change and is the point: a change
applied to only one of them is caught by the differential harness.

**Evidence.** Structural; no measurement applies.

---

## 015 — Memory behaviour is attributed before it is optimised

**Status:** hypothesis.

**Context.** The intuitive account is that the order pool and index total on
the order of 100 MB, exceed L3, and therefore cost a last-level miss on every
`E`, `C`, `X`, `D` and `U`. That account ignores order lifetimes: a large
fraction of cancels name orders added milliseconds earlier, and with a LIFO
free list the pool slot reused by the next Add is the one just freed, so
short-lived orders may stay resident end to end.

**Decision.** No prefetch or layout change is written before last-level misses
are attributed three ways: by structure (pool, index, level arrays, bitmaps —
each mapped into its own region so sampled load addresses resolve), by message
type, and by the age of the referenced order (buckets: <1 ms, 1-10 ms,
10-100 ms, 100 ms-1 s, >1 s).

**Alternatives considered.**
- *Prefetch first, measure after.* Produces a change that cannot be attributed
  and a speedup that cannot be explained.

**Consequences.** The attribution infrastructure — separate `mmap` regions, a
`perf mem` script, a per-message `rdpmc` counter — costs more than the
optimisation it gates. A legitimate outcome is that a large share of the
predicted misses do not exist, in which case that finding replaces the
optimisation.

**Evidence.** None yet. This record is a prediction; `docs/benchmarks.md`
carries the result.

---

## 016 — Hash policy is a template parameter, chosen by measurement

**Status:** hypothesis.

**Context.** Order references are roughly increasing (record 012). Identity
modulo the table size maps nearby references to nearby slots, which distributes
worse than a mixing hash but may behave better in cache. A mixing hash
deliberately scatters recent references across the whole table, which is where
locality plausibly dies.

**Decision.** The open-addressed order index takes its hash as a template
policy. Three policies are implemented and compared: identity-mod,
multiply-shift, and `std::hash`. The comparison reports miss rate and mean
probe length, not wall time alone, so that a win can be attributed.

**Alternatives considered.**
- *Pick one and justify it in prose.* The direction of the effect is not
  predictable from first principles, which is the reason for the experiment.

**Consequences.** The index is templated, so the book is templated, so every
consumer names a policy. The default is recorded once the measurement exists.

**Evidence.** None yet. Prediction recorded in `docs/benchmarks.md` before the
experiment runs.

---

## 017 — Order struct size is a compile-time switch, 24 or 32 bytes

**Status:** hypothesis.

**Context.** A cache line is 64 bytes, which is not a multiple of 24. From a
64-byte-aligned pool base, 24-byte orders average 2.67 per line and two of
every eight — 25% — straddle a line boundary, so an access touching fields on
both sides can cost two misses. A 32-byte order packs exactly two per line and
never straddles.

**Decision.** The order record's size is a compile-time switch. Both variants
are built and measured; neither is assumed.

**Alternatives considered.**
- *Pick 24 for density.* Density helps the cold, old-order population; the
  straddle hurts the same population. The net is not predictable.
- *Pad to 64.* Wastes half the pool's capacity for one-per-line access.

**Consequences.** Field widths are constrained by the smaller variant, so the
24-byte layout dictates what an order can carry.

**Prediction, recorded before measurement.** 24 bytes wins on density for the
old-order population; 32 bytes wins on straddle; the straddle penalty appears
mainly on cold orders, because young orders under a LIFO free list have both
lines resident already.

**Evidence.** The 25% straddle figure is arithmetic: with stride 24 from an
aligned base, offsets modulo 64 cycle through 0, 24, 48, 8, 32, 56, 16, 40 with
period 8; the records starting at 48 and 56 cross a boundary. The performance
consequence is a hypothesis.

---

## 018 — Level storage is a flat cents-indexed array with an overflow map

**Status:** committed.

**Context.** Per-symbol price activity concentrates in a narrow band around the
inside, but a session contains orders far outside it, including stale limit
orders and auction-only interest.

**Decision.** A flat array of levels covers a per-symbol window in cents. Prices
outside the window go to an overflow map. Overflow hits are counted, and the
window size is swept against the resulting overflow rate as a logged
experiment.

**Alternatives considered.**
- *A flat array covering the full price range.* 20 million cents per symbol;
  unusable.
- *A map for everything.* The reference book's structure, kept as the baseline
  rather than the implementation.
- *A window with no overflow path.* Silently drops orders outside it, which is
  a correctness failure that the differential harness would catch and that
  should not be possible in the first place.

**Consequences.** Two code paths for level lookup, and the overflow path must
be semantically identical to the fast path or the differential harness
diverges.

**Evidence.** Window size against overflow rate is measured; see
`docs/benchmarks.md`.

---

## 019 — Best bid and offer are tracked with a two-level bitmap

**Status:** committed.

**Context.** Finding the best price after a level empties means scanning for
the nearest occupied index. A linear scan over a wide window is unbounded in
the worst case, which lands in the latency tail.

**Decision.** Occupancy is summarised by a two-level bitmap over the level
array: a word-level bitmap and a summary bitmap over those words. The best
price is found with a count-leading-zeros or count-trailing-zeros instruction
at each level, bounding the search to two word scans.

**Alternatives considered.**
- *Cache the BBO index and scan linearly from it.* Fast in the common case
  where the inside moves one tick, unbounded when a level empties and the next
  is far away.
- *A heap or ordered set of occupied prices.* Logarithmic with a pointer chase
  per step, and an allocation per level transition.

**Consequences.** Two bitmap words must be updated on every transition of a
level between empty and occupied. The bitmaps are their own `mmap` region so
that record 015's attribution can separate their misses from the level array's.

**Evidence.** Bounded search is structural. The cost against the cached-index
alternative is measured.

---

## 020 — Queue-simulator fill rules

**Status:** committed. **Cited by `docs/preregistration.md`; changes after the
registration commit must be reported in the study writeup.**

**Context.** Tracking the number of shares ahead of a synthetic order is not
sufficient to simulate it. The simulator must state when the synthetic order
*fills*. The synthetic order is not in the real book, so real order flow
continues exactly as if it were absent, and the fill condition has to be
inferred from the behaviour of the orders that are present.

**Decision.** For a synthetic bid of size *q* at price *p*:

1. **Execution of a real order ahead, at *p*.** Deduct its executed shares from
   the ahead-count. No fill.
2. **Execution of a real order behind, at *p*** — including any execution at
   *p* once the ahead-count has reached zero. The aggressor consumed everything
   in front of that order, which includes the synthetic order. **Fill.**
3. **Trade-through.** Any execution on the same side at a price worse than *p*
   — for a bid, a resting bid below *p* — means the aggressor walked through
   the synthetic order's level. **Fill.**
4. **Non-displayed print (`P`) at exactly *p*.** On NASDAQ, displayed interest
   has priority over non-displayed at the same price, which suggests displayed
   interest at *p* was exhausted. But `P`'s side field has been hardcoded `'B'`
   since July 2014 (record 011), so the aggressor side is unknown, and
   midpoint-pegged prints trade between ticks. **This is a modelling choice,
   not an inference from the specification.** Results are reported both with
   the rule enabled and with it disabled.
5. **No-impact accounting.** When the synthetic order fills, the real order
   that triggered the fill still executes in the replay, so liquidity at *p* is
   double counted by *q*. Negligible for one round lot; stated rather than
   corrected.

**Alternatives considered.**
- *Fill only on rule 3.* Understates fill rates badly, since most fills at a
  level occur without a trade-through.
- *Fill probabilistically on volume at *p*.* Discards the exact position that
  market-by-order data provides, which is the point of the comparison.
- *Remove rule 4 entirely.* Defensible, and is precisely the "disabled" arm
  that is reported alongside.

**Consequences.** The market-by-price approximations all observe the same
traded volume at *p*; they differ in whether they attribute a *cancel* to the
shares ahead of or behind the synthetic order, which moves the ahead-count at
different rates. Rules 1 through 3 are what convert that attribution difference
into a difference in fill time, so the comparison is meaningless unless they
are fixed in advance. That is why this record is versioned and cited by number.

**Evidence.** Rule 2 matches the fill condition used by HftBacktest's
`L3FIFOQueueModel` (record 021). Rules 1 and 3 follow from price-time priority.
Rule 4 is labelled a modelling choice and reported both ways. Each rule has a
unit test driven by a hand-written event script.

---

## 021 — Prior art: exact queue position is not novel; the bias table is the contribution

**Status:** in force.

**Context.** Reconstructing exact FIFO queue position from market-by-order data
is established practice. HftBacktest ships an `L3FIFOQueueModel`, and its
Level-3 tutorial constructs L2 data from L3 to compare the two backtests on
crypto futures.

**Decision.** The simulator is described as an implementation of a known model,
in README.md and here. The claimed contribution is narrower and is stated as
such: the quantified fill-rate and time-to-fill bias of each market-by-price
approximation against exact queue position, on US-equity ITCH sessions, with
the fill rules of record 020 made explicit, broken out by queue depth at entry
and by symbol.

**Alternatives considered.** None; this is a factual statement about the
literature.

**Consequences.** The result stands or falls on the bias measurement, not on
the simulator.

**Evidence.** HftBacktest documentation, `L3FIFOQueueModel` and the Level-3
tutorial. Synthetic order placement follows Moallemi and Yuan on the value of
queue position.

---

## 022 — BX and NASDAQ are never pooled in a cost-inclusive result

**Status:** in force.

**Context.** NASDAQ operates a maker-taker schedule: it pays a rebate to
liquidity providers and charges a fee to takers. BX operates taker-maker: it
pays takers and charges providers. Maker and taker P&L therefore change sign
between the two venues.

**Decision.** No result that includes fees or rebates pools sessions from the
two venues. The study names a single venue. BX remains the primary target for
engineering and correctness work, because it carries roughly 54M messages
against a NASDAQ session's ~270M.

**Alternatives considered.**
- *Pool and control for venue with a dummy variable.* Assumes a common slope
  and differing intercept, which is not what a sign flip in the fee is.

**Consequences.** The study's power is bounded by the number of sessions from
one venue; this is an input to the power analysis in
`docs/preregistration.md`.

**Evidence.** SR-BX-2017 fee filings covering the period of the free BX
session; NASDAQ price list archived for the session dates, cited by date in
`docs/preregistration.md`.

---

## 023 — The study is predictive, and is not comparable to a contemporaneous R²

**Status:** in force.

**Context.** Cont, Kukanov and Stoikov (2014) regress 10-second mid-price
changes on order flow imbalance measured over the *same* 10 seconds, across 50
US stocks, and report an average R² of approximately 65%. That is a
decomposition of a price move into the flow that constituted it.

**Decision.** This study asks whether a feature measured over interval *t*
predicts the mid-price change over interval *t+1*. The two quantities are never
compared, reported side by side as if commensurable, or cited in support of one
another. Predictive R² on this data is expected to be a small fraction of one
percent.

**Alternatives considered.** None; conflating the two would be an error rather
than a choice.

**Consequences.** The headline number this study can produce is small by
construction, and the interesting question is whether what remains survives
costs. The decision rules in `docs/preregistration.md` are written against that
expectation.

**Evidence.** Cont, Kukanov and Stoikov, "The Price Impact of Order Book
Events", *Journal of Financial Econometrics* 12(1), 2014.

---

## 024 — Hot-path constraints

**Status:** in force.

**Context.** The constraints below are applied uniformly rather than decided
per site, so that a violation is a review question rather than a judgement
call.

**Decision.** On any path that runs per message:

- No heap allocation after warmup. Pools and arenas are sized during
  initialisation.
- No `std::function`, `std::string`, `shared_ptr`, or virtual dispatch.
  `std::string_view` over the mapped buffer carries symbols.
- Integer ticks only (record 004).
- The parser is templated on its handler (record 006).
- `mmap` with sequential access, rather than buffered reads.
- Single-threaded. Sharding by stock locate is the obvious parallelisation and
  is out of scope.

**Alternatives considered.** Relaxing any one of these for a specific call site
is possible and would be recorded as its own decision; none has been needed.

**Consequences.** Error handling on the hot path cannot allocate or throw, so
it reports through counters (record 007). Warmup cost is paid once and excluded
from the timed region, which is stated in every benchmark entry.

**Evidence.** Allocation freedom is asserted in debug builds and checked under
the sanitizer preset over a full session.

---

## 025 — The zero-prefixed framing variant is detected, not accommodated

**Status:** in force.

**Context.** `ex20101224.TEST_ITCH_50`, the fixture bundled with the RITCH R
package and the target of the Stage 1 fast census gate, carries the 2-byte
length prefix field for all 12,012 of its messages and leaves every one of them
zero. The prefix is present and unfilled, so the length must come from the type
byte. Sessions from the NASDAQ archive are unaffected: `20190130.BX_ITCH_50`
begins `00 0C 53 …`, a correct prefix for a 12-byte System Event.

The two forms cannot be read under one rule. In the prefixed form a zero prefix
is the end-of-session marker; in this variant it introduces a message. The same
two bytes mean opposite things.

**Decision.** `Framing` names the two forms. `detect_framing()` walks up to 64
messages under each hypothesis and returns the one that stays consistent;
ambiguity resolves to `LengthPrefixed`. `FrameReader` takes the form
explicitly, and the census reports which form a file used.

**Alternatives considered.**
- *Fall back to the type-implied length whenever the prefix is zero.* Makes
  end-of-session unrepresentable in the prefixed form, so a truncated session
  would be read past its terminator into whatever follows.
- *Convert the fixture to prefixed form before censusing it.* Moves the problem
  into a preprocessing step that the gate then depends on, and makes the gate a
  test of the converter.
- *Refuse the variant and choose a different fast gate.* The RITCH fixture is
  the only independently-counted file small enough to gate on in under a
  second.

**Consequences.** The zero-prefixed reader has no recovery path. Without an
independent length, an unknown type byte leaves no safe distance to skip, so
the reader counts it and reports a truncation rather than guessing. That
asymmetry is a property of a format that does not carry its own lengths, and it
is why every result in this repository is produced from the prefixed form. The
detector costs one pass over at most 64 messages, once per file.

**Evidence.** Both forms and the detector are covered in
`tests/test_wire.cpp`. The fixture's framing was confirmed by walking it under
each hypothesis: the zero-prefixed walk consumes exactly 465,048 bytes in
12,012 messages, matching the file size and RITCH's count exactly. The real
session's prefixes were confirmed by inspection of its first bytes.

---

## 026 — Typed views hold one pointer and decode on access

**Status:** in force.

**Context.** A handler for Add Order typically reads four fields; a handler
counting messages reads none. Materialising a decoded struct per message pays
for every field regardless.

**Decision.** Each message type is a struct holding a single `const unsigned
char*` into the mapped buffer, with one accessor per field that decodes on
call. Views are passed by value, are trivially copyable, and are asserted to be
pointer-sized. They are never stored: a view is valid only while the buffer it
points into lives.

**Alternatives considered.**
- *Decode into a struct of native-endian fields.* Costs every field on every
  message, and the struct has to be laid out and kept in step with the wire
  format by hand — a second transcription with no audit.
- *A variant over all 23 types.* Adds a discriminator the type byte already
  carries, and a visit the switch already performs.
- *Return fields through a generic `get<Field>()`.* Equivalent code generation,
  worse call sites, and it loses the per-type documentation the accessors
  carry.

**Consequences.** A handler that reads the same field twice decodes it twice,
unless it binds the result to a local; that is the caller's choice to make, and
the decodes are one load and one byte-reversal (record 002). Views must not
outlive the buffer, which is enforced by convention rather than by the type
system — the parser never hands a view to anything that could store it.

**Evidence.** `static_assert(sizeof(AddOrder) == sizeof(const unsigned char*))`
and the trivially-copyable assertions in `messages.hpp`. Per-field decode cost
is record 002.

---

## 027 — Crossed and locked counters are reconstruction-error detectors, not market statistics

**Status:** in force. Amends the framing of record 007; that record's decision
stands unchanged.

**Context.** Record 007 treats crossed and locked books as legitimate
conditions to be counted rather than repaired, on the expectation that they
occur around halts and auctions. Replaying `20190130.BX_ITCH_50` produced
**zero** of each over 74,182,680 book-affecting messages.

That is not a dead counter. Instrumenting the check shows it is in a position
to fire on 98.7% of those messages — both sides populated, across 6,678
distinct symbols — and the smallest spread observed is a single Price(4) unit,
one hundredth of a cent, so the book does reach the point of nearly touching.

**Decision.** The counters stay, their documented purpose changes, and a
nonzero count now **fails the correctness gate**.

A single venue's own displayed book cannot lock or cross itself during
continuous trading: an incoming order that would cross executes against the
resting side instead of resting. Locked and crossed markets are an inter-venue
phenomenon, visible in the consolidated quote and not in one venue's book.

So a nonzero count here is evidence of a **reconstruction error** — a missed
removal, a misapplied replace, a stale level — rather than a market condition.
`replay` exits nonzero on any such observation in either book, and says why.
That is a stronger check than the one the counter was introduced for: it turns
a class of book errors that no other layer detects into an immediate failure.

**Alternatives considered.**
- *Remove the counters, since they never fire.* Discards a cheap invariant
  that catches a whole class of book errors nothing else detects.
- *Keep counting without failing.* This was the previous decision, and it was
  too weak. A counter nobody is forced to look at is a counter that drifts
  upward unnoticed.
- *Assert inside the book.* The hot path does not throw (record 024), and an
  assertion would lose the session rather than report which symbol and when.
  Counting during the replay and failing at the end keeps the diagnosis.
- *Keep describing them as expected around auctions.* The BX session contains
  no auction messages at all (`I` and `Q` both zero), so it cannot speak to
  the auction case either way, and claiming it as confirmation would overstate
  it. A NASDAQ session does run an opening and a closing cross, so it is the
  real test of this decision; if a cross legitimately produces a locked book
  in the displayed data, this record is revised with that evidence rather than
  the gate being quietly relaxed.

**Consequences.** The claim "this reconstruction never crossed" is meaningful
only for single-venue displayed books on sessions with the same
characteristics. A NASDAQ session, which does run opening and closing crosses,
has not been replayed, and the expectation there is explicitly untested.

**Evidence.** Full-session replay of `20190130.BX_ITCH_50`: 74,182,680 checks,
73,201,236 with both sides populated (98.7%), 6,678 symbols two-sided at some
point, zero crossed, zero locked, minimum spread 1 Price(4) unit. Both the
reference and fast books report the same, though they implement the same rule
twice and so agreement between them is not independent confirmation.

---

## 028 — Correctness layer 3 is skipped, and the gap is stated rather than filled

**Status:** in force.

**Context.** Layer 3 of the correctness argument was a row-by-row diff against
LOBSTER's reconstructed orderbook files. It is the only layer that would
constrain this project's *book semantics* against a reconstruction built by
someone else: layers 1 and 2 constrain the parser, layer 4 shows the two books
here agree with each other, and layer 5 shows a replay is internally consistent
and repeatable.

Checked on 2026-09-22, it cannot be run. LOBSTER's sample data is gated behind
a manual request flow requiring proof of purchase of an accompanying book, with
a time-limited link sent by hand; there is no unauthenticated download. And its
sample session is 2012-06-21, for which the NASDAQ archive offers no ITCH 5.0
session at all — the archive runs 2018-2020 and 2022 onward.

**Decision.** The layer is skipped. The correctness tables in README.md and
`docs/correctness.md` say so in the row where the layer used to make a claim,
rather than omitting the row, and both name what the absence costs.

**Alternatives considered.**
- *Build the adapter anyway against a session only LOBSTER covers.* There is
  nothing to diff it against, so it would be untested code asserting a
  capability the project does not have.
- *Substitute a different external reconstruction.* `RITCH` reconstructs book
  state as well as counting messages, but layer 2 already uses it and a second
  use of the same implementation is not a second opinion. No other freely
  available ITCH 5.0 reconstruction covering book state was found.
- *Drop the row from the tables.* A four-layer argument presented as though it
  were the whole plan would overstate what has been verified. The gap is the
  point of recording it.

**Consequences.** A shared misunderstanding of the feed's semantics passes
every remaining layer. The concrete example worth naming is record 009: if `U`
did not in fact move an order to the back of the queue at an unchanged price,
the reference book and the fast book would both be wrong in the same way, the
differential would agree, the determinism hashes would be stable, and every
queue-position result would be wrong. That risk is carried, not eliminated.

**Evidence.** `lobsterdata.com` serves its application shell for every path,
so any sample URL answers 200 with 457 bytes of HTML. Its application bundle
contains the request flow (`/book-download/...`, purchase-proof upload, manual
approve or reject, time-limited link) and no static sample path. Session dates
are in `docs/data.md`.

---

## 029 — Synthetic order value is reported in half-spreads, not ticks

**Status:** in force.

**Context.** The queue study values a passive fill as the mid at a horizon
minus the price paid. Pooled across symbols in Price(4) units, the first run
produced a mean of −739 units at one second, which is implausibly large until
the composition is examined: placements were spread uniformly over every
two-sided symbol, and BX has thousands of names whose inside is dollars wide.
A handful of them dominated the mean.

**Decision.** Two changes, together. Placements are restricted to a liquid
universe, selected by message count from the session itself. And value is
reported in **half-spreads at entry** alongside ticks: an order resting at the
inside begins half a spread better than the mid, so a value of 0 means the mid
moved exactly far enough to give that edge back, −1 that it moved twice as
far.

**Alternatives considered.**
- *Report ticks and note the caveat.* The number is then a weighted average
  over spreads differing by two orders of magnitude, and no reader can
  reconstruct what it means for a given symbol.
- *Report basis points of price.* Correct for comparing across price levels,
  but it does not answer the question a maker asks, which is whether the
  captured spread survives the subsequent move.
- *Report per symbol only.* Necessary for the study and unwieldy as a headline;
  both are produced, and the pooled figure is the normalised one.

**Consequences.** The half-spread figure is undefined when the spread at entry
is zero, which a locked book would produce; those fills are excluded from the
normalised total and included in the tick total, and the counts are reported
separately so the exclusion is visible. Comparisons across venues are only
meaningful in half-spreads, since a tick means a different fraction of the
spread on each.

**Evidence.** With the liquid universe and the normalised scale, exact-model
value is −0.77 half-spreads at one second, decaying to −0.56 at sixty
seconds; the conservative model reports −1.28, and its fill-reason breakdown
(79% trade-throughs against exact's 49%) accounts for the gap.

---

## 030 — Conservative cancel attribution is biased twice, in the same direction

**Status:** in force. This is the study's main finding.

**Context.** A market-by-price backtest cannot tell whether a cancel at a
price came from ahead of a resting order or behind it. The cautious choice is
to assume every cancel came from behind, so the queue never advances except on
an execution. That is conservative in the sense of not overstating fills, and
it is what a backtest reaches for when it wants to avoid flattering itself.

**Decision.** Record it as the worst of the three approximations, and state
why, so that the cautious choice is not made by default on the grounds that
caution is free.

**Evidence.** **Scope: one venue, one session — BX, 2019-01-30 — 50 symbols,
one round lot.** 93,525 placements at the inside, against exact
market-by-order position:

- Fill rate 19.87% against exact 25.12%: **20.9% low** pooled, 95% symbol
  cluster-bootstrap interval [−24.4%, −17.7%], and the same sign in all ten
  placement sequences with a seed range of 1.01 percentage points — so the
  effect is twenty times the placement noise.
- Worse with queue depth: 6.2% low at the front, **56.7% low** with 10,000 or
  more shares ahead. Deep in the queue almost every fill arrives through
  cancellation of the orders in front, which this model assumes never happens.
  That deepest bucket holds 669 placements, so its percentage moves by 0.15
  points per order; the magnitude is solid, the third digit is not.
- Value −1.28 half-spreads at one second against exact −0.77: **66% more
  pessimistic**. The one-second horizon is quoted because adverse selection is
  largest there; at ten and sixty seconds the gap is −1.04 against −0.61 and
  −1.06 against −0.56.

The two are the same mechanism. Refusing to advance the queue on cancels means
the model only ever fills when the market runs through the level: 79% of its
fills are trade-throughs, against 49% for exact. Trade-through fills are
precisely the adversely selected ones, so the surviving sample is the worst of
the real one. The model under-reports how often a passive order fills and
over-reports how badly it does when it fills.

Proportional attribution — a cancel of *c* from a level of *d* with *a* ahead
removes *c·a/d* from in front — tracks exact within about 2% at every depth,
at no extra data cost. Its pooled bias of −0.5% is consistently signed across
the symbol bootstrap and all ten placement sequences, but its seed range is
0.36 percentage points, so the effect is barely larger than the placement
noise. It is a real but negligible bias, not a finding of the same kind as
conservative's, and record 035 measures where it comes from.

**Consequences.** A maker strategy evaluated under conservative attribution
will be rejected on two counts that are both artifacts. Anything in this
repository that reports a maker P&L states its queue model, and the
pre-registration's strategy section commits to exact position
(`docs/preregistration.md` section 6).

**Alternatives considered.** None: this is a measurement, not a choice. What
it settles is which approximation the pre-registered study should use if
market-by-order data were ever unavailable, and the answer is proportional.

---

## 031 — Half-spread-normalised micro-price deviation *is* queue imbalance

**Status:** in force.

**Context.** `docs/preregistration.md` section 5 listed micro-price deviation
and queue imbalance as two of five secondary features, to be tested under a
Holm–Bonferroni correction across that family. The first feature export made
them numerically identical in every one of 174,216 rows, to the last digit
printed.

That is not a property of the data. With best bid *(P_b, q_b)* and best offer
*(P_a, q_a)*, micro price *M = (q_a P_b + q_b P_a)/(q_b + q_a)*, mid
*m = (P_b + P_a)/2* and spread *s = P_a − P_b*:

```
M - m = (q_a P_b + q_b P_a)/(q_b + q_a) - (P_b + P_a)/2
      = [2 q_a P_b + 2 q_b P_a - (q_b + q_a)(P_b + P_a)] / [2(q_b + q_a)]
      = [q_a(P_b - P_a) + q_b(P_a - P_b)] / [2(q_b + q_a)]
      = (s/2) * (q_b - q_a)/(q_b + q_a)
```

So *(M − m)/(s/2)* equals *(q_b − q_a)/(q_b + q_a)* identically, for every
input. Micro-price deviation measured in half-spreads **is** queue imbalance,
by algebra rather than by approximation.

**Decision.** Micro-price deviation is emitted in **ticks**, not half-spreads.
In ticks it equals queue imbalance multiplied by half the spread, so it
carries the spread as well as the imbalance and is genuinely a different
feature; measured on the BX session its correlation with queue imbalance is
0.22 rather than 1.

**Alternatives considered.**
- *Drop micro-price from the family.* Defensible, and it discards the spread
  interaction, which is the part of the feature that is not already present.
- *Keep both normalised and note the identity.* Would put the same feature in
  the Holm–Bonferroni family twice, shrinking every other feature's corrected
  significance threshold for no added evidence, and would present one result
  as two.

**Consequences, and a second identity the first fix did not remove.**
Expressing micro-price deviation in ticks separates it from queue imbalance in
*magnitude* — their correlation is 0.22 — but **not in sign**. Since
*micro − mid = (s/2) · QI* and the spread *s* is strictly positive whenever
the book is two-sided, the two always carry the same sign, confirmed on all
174,216 rows.

So the two features are distinct for a metric that uses magnitude, such as an
out-of-sample R², and **identical for any metric that uses only the sign**,
such as directional accuracy. On the development dry run both produced a
directional accuracy of 0.39824 to five decimal places.

The size of the Holm–Bonferroni family therefore depends on the metric the
primary hypothesis uses, which is stated in `docs/preregistration.md` section
3 rather than left to be noticed later:

- Primary metric uses magnitude (candidate B): the family is five distinct
  features.
- Primary metric uses only sign (candidates A and C): micro-price deviation is
  not distinct from queue imbalance, and the family is four.

Any future feature added to that family is checked for an algebraic
relationship to the existing ones — in magnitude *and* in sign — before it is
registered, not after.

**Evidence.** The derivation above, confirmed in exact rational arithmetic on
constructed inputs and numerically across all 174,216 rows of a feature
export, where the maximum absolute difference was exactly zero. The sign
identity was confirmed on the same rows: the two features share a sign in
100% of them, and the tick form reproduces *QI × s/2* to within rounding.

---

## 032 — A session is complete when its last message is End of Messages

**Status:** in force. Corrects a factual error in records 001 and 025 and in
the code comments they describe; those records' decisions are unchanged.

**Context.** The framing code, the README and `docs/correctness.md` all stated
that the ITCH 5.0 specification describes a zero-length length-prefix
terminating a session file. **It does not.** The specification guarantees one
thing about the end of a session: System Event `'C'`, End of Messages, is the
last message of the day. The zero-length prefix is a third-party description
of how a BinaryFILE is packaged, and NASDAQ's published sessions do not write
one.

The error had a consequence beyond the wording. Treating "the file ended" as
the terminator makes a truncated download indistinguishable from a complete
session: every message in a prefix of a valid file parses, the framing
consumes every byte, and the reader reaches the end cleanly. A session lost to
a dropped connection would have been accepted silently — and connections to
this archive do drop, repeatedly.

**Decision.** Separate the two questions and answer them in different places.

*Did the framing understand every byte?* A property of the bytes, decided by
the reader. `FrameStatus` and `ParseEnd` report three distinct outcomes — an
explicit zero-length prefix, clean exhaustion at a message boundary, and a
truncation part-way through a message — and `ParseStats::trailing` carries the
number of bytes left unread. None of the three is treated as evidence of
completeness.

*Is the session complete?* A property of the content, decided by the census:
the last message must be System Event `'C'`. The census reports the final
message and its event code, and its **exit status is nonzero unless the
framing consumed everything and the last message is `'C'`**.

**Alternatives considered.**
- *Keep accepting a clean exhaustion as the terminator.* This is what was
  wrong. It cannot distinguish a complete file from a prefix of one.
- *Require a zero-length prefix.* Rejects every session NASDAQ publishes.
- *Check only the `'C'` message and ignore trailing bytes.* Would accept a
  file with a valid session followed by appended garbage, which is exactly the
  corruption a bad continued transfer produced here (`docs/data.md`).

**Consequences.** A file that legitimately lacks a closing `'C'` — an intraday
capture, a deliberately partial extract — is now rejected by the census. That
is the intended trade: such a file is not a session, and anything deriving a
result from one should have to say so explicitly.

**Evidence.** `20190130.BX_ITCH_50`: final message System Event `'C'`, framing
exhausted at a message boundary, zero trailing bytes, SHA-256
`d670c9dd0e2391a4007fa407668bfaaa9ded346f0804bb5d7b2a6c381bdcadd3`.

The check is tested rather than trusted: `tests/roundtrip.cmake` generates a
session with no final System Event via `gen_synthetic --no-end` — a
well-formed prefix of a valid file — and fails if the census accepts it.
`tools/fetch_data.sh` adds the byte-level checks that must hold before the
content check is meaningful: the transfer length must equal the advertised
`content-length`, and `gzip -t` must pass **with no output at all**, since
macOS gzip exits 2 on trailing garbage while GNU gzip exits 0 with a warning.

---

## 033 — The flat window slides, anchored per side on that side's own best price

**Status:** in force. Supersedes the window geometry of record 018; that
record's decision to use a flat array with an overflow map stands.

**Context.** Record 018 placed a per-symbol window at the symbol's first
whole-cent price and never moved it. Measured on `20190130.BX_ITCH_50`, that
put **33.8% of all orders into the overflow map** at the default 256-tick
width, and the rate fell only slowly with width — 43.8% at 64 ticks, 26.7% at
512 — reaching single digits only at 2,048 ticks, which costs about 640 MB of
level arrays and removes the locality the flat array was chosen for.

The shape of that curve was the diagnosis. A thin tail of stale far-from-market
orders would have fallen away quickly with width. A broad, slowly-falling
distribution says the window was in the wrong *place*, not that it was too
small: with a fixed origin the overflow rate measures a symbol's intraday
**range**, not any order's distance from the inside.

**Decision.** The window slides. Three parts, which only work together:

1. **The origin belongs to the side, not the symbol.** One origin per symbol
   must span the spread, and on a thin venue the best bid and offer can sit
   further apart than the window is wide — so the trigger fires permanently
   and the window is rebuilt on every tick of the mid. Per side the spread
   never enters the decision.
2. **A window is anchored asymmetrically.** A side's resting interest is
   almost all on one side of its own best price: bids at or below the best
   bid, offers at or above the best offer. The best bid is placed
   `kRecenterMargin` below the top of its window and the best offer the same
   distance above the bottom of its own, so the bulk of the window covers
   prices that side can actually occupy. Centring would spend half of it on
   prices that cannot be.
3. **The order record caches nothing about where its level lives.** The origin
   moves, so a cached flag would have to be rewritten for every order of every
   level that crossed the boundary during a rebuild — the one operation
   recentering exists to keep cheap. The location is derived from the price and
   the current origin, two comparisons, and moving the origin moves every level
   at once without touching a single order.

A rebuild is triggered when a side's best price comes within `kRecenterMargin`
of a window edge, with hysteresis of the same size so an inside oscillating
around the trigger does not rebuild on every tick. The rebuild is driven by the
occupancy bitmap rather than by a scan of the window, so its cost is
proportional to what is in the window rather than to its width.

**Alternatives considered.**
- *Ring-index the window with a moving origin.* Avoids the rebuild, but every
  slot's meaning changes as the origin moves, so slots leaving the window must
  still be evicted, and the arithmetic at every access gains a modulo. The
  rebuild turned out to cost about 0.13 levels moved per book message, which
  did not justify that.
- *Widen the window instead.* Measured, and rejected: 1.5% overflow needs 4,096
  ticks and 1.3 GB of level arrays.
- *Keep one origin per symbol and recenter on the mid.* Implemented and
  measured first. It works, but the spread enters the decision, and on this
  session it produced 4.0M rebuilds moving 26.8M levels against 3.2M moving
  9.3M for the per-side version.

**Consequences.** Emptied overflow levels and emptied flat levels are handled
by the same code path, since which one an order was in is no longer recorded.
A recenter is O(occupied levels + overflow entries) for one side, and its
frequency is bounded by the hysteresis rather than by the message rate.

**Evidence.** `20190130.BX_ITCH_50`, 74,508,064 book-affecting messages, every
row verified identical to the reference book.

| Window | Overflow, fixed origin | Overflow, sliding | Recenters | Levels moved |
|---:|---:|---:|---:|---:|
| 64 | 43.8% | **3.51%** | 4,774,566 | 15,561,237 |
| 128 | 39.5% | **2.99%** | 4,098,163 | 12,577,697 |
| 256 | 33.8% | **2.37%** | 3,206,627 | 9,318,071 |
| 512 | 26.7% | **1.43%** | 1,833,926 | 4,943,986 |
| 1024 | 16.7% | **0.62%** | 646,340 | 1,554,702 |
| 2048 | 6.3% | **0.29%** | 157,772 | 340,009 |

At the default 256 ticks the overflow rate falls by a factor of **14**, and
the replay is no slower than it was with the fixed origin — the saved overflow
map lookups pay for the rebuilds. Sub-cent prices are 131,243 of the remaining
overflow hits at every width and cannot be indexed on a cents axis
(record 005), so they are a floor of 0.18%.

The path is covered by a differential test that walks the inside up and back
down across sixteen window widths. The design it replaces could not have
failed such a test, because it never moved.

---

## 034 — The Retail Price Improvement Indicator is not obsolete on BX

**Status:** in force. Corrects a prior expectation recorded in `spec.hpp`.

**Context.** ITCH's `N` message, Retail Price Improvement Indicator, is
commonly described as effectively dead on the grounds that NASDAQ's Retail
Price Improvement program ended on 31 December 2014. This project's
specification header said so, and told the reader to expect zero or near-zero
counts in the sample sessions.

**Decision.** Record the measurement and drop the expectation. The note in
`spec.hpp` now states what the data shows.

**Alternatives considered.** None: this is a measurement correcting a belief.
What it changes is that a nonzero `N` count is no longer treated as a signal
that something is wrong with the decode.

**Consequences.** `N` is 10% of a BX session's messages, so any per-message
cost attributed across "the message stream" on BX is spread over a population
that is one-tenth a message type the book ignores entirely. Benchmarks that
quote a per-message figure state which types they include.

**Evidence.** `20190130.BX_ITCH_50`:

| | |
|---|---|
| `N` messages | 8,301,264 — **10.0%** of 82,841,542 |
| Distinct symbols | 7,217 |
| Time span | 08:00 to 19:00 |
| `InterestFlag` values | `'A'` 530,735 · `'B'` 2,121,573 · `'N'` 3,620,049 · `'S'` 2,028,907 |
| Concentration | QQQ, TVIX, AMZN, FFEU, QID, NFLX, SCO, TSLA |

All four documented flag values occur, the messages span the whole session,
and they concentrate in liquid names — this is a live feed of a live program,
not residue. `RITCH::count_messages()` independently reports the same total,
so it is the feed's content and not an artifact of this project's decode. BX
operates its own retail program.

**Untested.** Whether NASDAQ-venue sessions carry `N` in volume. No NASDAQ
session has been censused yet, so the original claim may well hold for the
venue it was made about.


---

## 035 — Proportional attribution over-advances the queue, and that is not why its fill rate is low

**Status:** in force. Measured, replacing an inference.

**Context.** Record 030 established that proportional cancel attribution
tracks exact queue position closely while conservative does not. It did not
establish *why*, and the obvious argument runs the wrong way.

The Stage 6 lifetime data says cancels concentrate sharply among
recently-added orders: 66.6% of cancelled-untouched orders on
`20190130.BX_ITCH_50` are cancelled within one second of being added, 45%
within 100 ms. A synthetic order placed at time *T* has every later arrival
*behind* it, so the orders most likely to be cancelled are disproportionately
behind. A model that assumes cancels are drawn uniformly from the level should
therefore attribute too many of them to the queue ahead, advance too fast, and
**over**-estimate fills.

Proportional's pooled fill-rate bias is **negative**.

**Decision.** Measure the attribution error directly rather than infer it from
the fill rate. For every cancel at a live synthetic order's price, the exact
model already knows whether the cancelled order was ahead; the proportional
model's assumed fraction is *a/d* at that moment. Both are accumulated, per
depth bucket at entry.

**Evidence.** 538,121,624 cancelled shares at live synthetic orders' prices:

| Shares ahead at entry | Cancelled shares | Actually ahead | Assumed ahead | Error |
|---|---:|---:|---:|---:|
| 0–99 | 1,114,909 | 1.57% | 1.70% | +0.13 pp |
| 100–499 | 213,248,543 | 4.37% | 4.55% | +0.18 pp |
| 500–1,999 | 138,361,218 | 11.52% | 12.16% | +0.63 pp |
| 2,000–9,999 | 152,819,508 | 12.09% | 13.66% | +1.57 pp |
| 10,000–49,999 | 32,577,446 | 21.26% | 25.26% | +4.00 pp |
| All | 538,121,624 | 9.42% | 10.34% | +0.92 pp |

The prediction from the lifetime data **holds**: the error is positive
everywhere and grows monotonically with queue depth. Proportional does
over-attribute cancels to the queue ahead.

**What does not follow, and why.** A positive attribution error does not imply
an over-estimated fill rate, and here it does not produce one except in the
deepest bucket (+2.2% at 10,000+ shares ahead, where the attribution error is
+4.00 pp; negative in every shallower bucket).

The mean error is not the whole of the difference between the models. Exact
removes a cancel from the queue ahead **all or nothing**; proportional always
removes a fraction. The two therefore differ in the *variance* of the
ahead-count as well as its mean, and fill probability is not linear in the
ahead-count — an order needs the count to reach zero, which a fractional
process approaches differently from a jump process with the same mean. The
attribution error dominates only where the queue is deep enough that reaching
zero is the binding constraint on filling at all.

**Consequences.** The practical recommendation in record 030 is unchanged:
proportional is the approximation to use when market-by-order data is
unavailable. What changes is the explanation offered for it. Proportional is
not nearly unbiased because its assumption is nearly right — the assumption is
measurably wrong, by 0.92 percentage points pooled and 4.00 at depth — but
because that error is small in absolute terms and partly offset by a
difference in how the two processes approach zero.

**Alternatives considered.**
- *Infer the mechanism from the fill-rate bias alone.* That is what produced
  the wrong prediction, and it is not recoverable without the direct
  measurement.
- *Correct proportional for the measured bias.* A model tuned to one session's
  measured attribution error is fitted to that session, and the whole point of
  the comparison is to evaluate approximations a practitioner could apply
  without market-by-order data — which is exactly what they would not have.

**Scope.** One venue, one session, 50 symbols. The direction of the
attribution error follows from cancels concentrating among young orders, which
is a general feature of modern equity markets; its magnitude is not.
