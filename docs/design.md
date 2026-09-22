# Design notes

The rules and predictions this project is built against. Where the spec doc
explains *why* the project exists, this file is the working reference: what the
hot path may and may not do, what correct-but-surprising behavior looks like,
and which beliefs are still hypotheses.

`docs/preregistration.md` references the queue-simulator fill rules in section 4 by
name. Because a pre-registration is only meaningful against a committed,
versioned definition, **changes to section 4 after the registration commit must
be called out in the study writeup.**

---

## 1. Hot-path rules

Applies to the parser, the book, and anything that runs per message:

- **Zero heap allocation after warmup.** Arena or pool everything.
- No `std::function`, no `std::string`, no `shared_ptr`, no virtual dispatch.
  `string_view` over the mapped buffer for symbols.
- **Integer ticks only. Never floating point for money.** Float money is how
  backtests lie by fractions of a cent. Tick size is display metadata, never
  arithmetic.
- The parser is **templated on its handler**, not built on an abstract base
  class. The dispatch switch runs hundreds of millions of times per session; an
  indirect call the branch predictor cannot resolve and the inliner cannot see
  through is not acceptable there.
- Prefer `mmap` + sequential access over buffered reads.

---

## 2. Things that look like bugs and are not

Count and log these. Do not "fix" them:

- **Crossed or locked books** around halts and auctions. They legitimately
  occur. Count and log them; do not assert.
- **Cross Trade (`Q`) reporting zero shares** when order interest was
  insufficient.
- **Orphaned modifies** referencing orders that were never added, at session
  boundaries. Count and report; do not crash.
- **Hidden liquidity never appearing in the book.** `P` reports it after the
  fact by design. Fills against it are unmodelled and that is documented.

---

## 3. Tick size

Penny ticks are correct for the 2017–2020 sample sessions, which is why the
price axis indexes in cents. The 2024 Reg NMS Rule 612 amendments introduce a
half-penny tick for tick-constrained stocks; compliance was set for 3 November
2025 and has been delayed to November 2026. Do not silently "modernise" the
axis — if it ever changes, it is a deliberate, documented change.

---

## 4. Claims that must be measured before they are written down

Several things in this project's own design documents were initially stated as
fact and turned out to be hypotheses. Treat everything in this section as a
prediction to test, and never let one of them into the README, a commit
message, or a resume line until a measurement backs it.

### The memory bottleneck is a hypothesis

The naive story is "the order pool and index are ~100MB, don't fit L3, so every
E/X/D/U takes an LLC miss." That ignores **order lifetimes**. A large share of
cancels hit orders added milliseconds earlier. With a **LIFO free list**, the
pool slot reused by the next Add is the one just freed, so short-lived orders
likely stay hot in cache end to end.

The **hash index** is where locality plausibly dies: a good hash deliberately
scatters recent references across the whole table.

Before writing any prefetch code, measure LLC misses **attributed three ways**:

- **by structure** — pool vs index vs level array vs bitmap. `perf mem record`
  samples load latency with the data address; map addresses to each
  structure's range.
- **by message type** — E, C, X, D, U separately.
- **by order age** — time since the referenced order's Add, bucketed
  (<1 ms, 1–10 ms, 10–100 ms, 100 ms–1 s, >1 s). For exact per-message counts,
  read the LLC-miss counter with `rdpmc` around each message.

A legitimate outcome is "half the misses aren't there." If so, that finding is
the result and the prefetch story gets rewritten around it.

### Hash choice has a locality twist

Order references are day-unique, and the spec's guarantee that they increase
was removed in February 2009 — but in practice they are *roughly* increasing.
Identity-mod on roughly increasing refs places recent orders in nearby slots,
so it may beat multiply-shift on **cache behavior** while distributing worse.

Performance may exploit rough monotonicity. **Correctness must never depend on
it**: probing and wraparound must be correct for arbitrary refs. Measure miss
rate and probe length for each hash, not just wall time.

### Order struct size: predict the straddle before measuring it

64 is not a multiple of 24. From a 64-byte-aligned pool base, a 24-byte order
averages 2.67 per line and **2 of every 8 (25%) straddle two cache lines**; an
access touching fields on both halves can cost two misses. A 32-byte order
fits exactly two per line with **zero straddling**.

Prediction to record *before* the experiment: 24 bytes wins on density for the
old-order population; 32 bytes wins on straddle; the straddle penalty shows up
mainly on cold (old) orders, because young orders under a LIFO free list have
both lines hot. Log the measured result against the prediction either way.

### Queue simulator fill rules

Tracking the ahead-count is not enough; the simulator must say **when the
synthetic order fills**. The synthetic order is not in the real book, so real
flow continues as if it were absent. Rules, for a synthetic bid of size q at
price p:

1. **Execution of a real order ahead of you** at p: deduct its executed shares
   from the ahead-count.
2. **Execution of a real order behind you** at p — including any execution at p
   once the ahead-count is zero: the aggressor must have consumed everything in
   front of that order, which includes you. **You fill.** This is the same rule
   HftBacktest's L3 FIFO model uses.
3. **Trade-through**: any execution on your side at a price worse than p (for a
   bid, a resting bid below p) means the aggressor walked through your level.
   **You fill.**
4. **Non-displayed prints (`P`)** at exactly p: on NASDAQ displayed interest has
   priority over non-displayed at the same price, which suggests displayed
   interest at p — including you — was exhausted. But `P`'s side field has been
   hardcoded `'B'` since July 2014, so the aggressor side is unknown, and
   midpoint-peg prints trade between ticks. **This is a modeling decision, not
   a fact.** Choose a rule, document it, and report results with and without it.
5. **No-impact accounting**: when you fill, the named real order still executes
   in the replay, so liquidity is double counted. Negligible for small q; state
   it.

The market-by-price models all see the same trade volume at p. They differ in
how they attribute *cancels* — ahead of you or behind you — which moves the
ahead-count at a different rate. The fill rules above are what convert that
attribution difference into a fill-time difference, so they have to be written
down explicitly or the comparison means nothing.

### Prior art for the queue work

Exact FIFO queue position from market-by-order data is **not new**. HftBacktest
ships an `L3FIFOQueueModel`, and its Level-3 tutorial builds L2 data from L3 to
compare the two backtests on crypto futures. Do not describe the MBO simulator
as novel anywhere.

The defensible contribution is narrower: **the quantified fill-rate and
time-to-fill bias of each market-by-price approximation against exact queue
position, on real US-equity ITCH sessions, with the fill rules explicit**,
broken out by queue depth and symbol.

### Venue fees differ in sign

NASDAQ runs maker-taker. **BX runs taker-maker**: it pays credits to liquidity
takers and charges providers (see SR-BX-2017, covering the period of the free
BX session). Never pool BX with NASDAQ sessions in any cost-inclusive result.
BX remains fine for engineering and correctness work.
