# Pre-registration

**Commit this file, fully filled in, BEFORE any code touches a held-out
session.** The commit timestamp relative to the held-out run is the entire
claim. `git log` checks it in thirty seconds; if the order is wrong the claim is
worth less than nothing.

Every section below must be answered. A blank section means the study is not
registered.

---

## 1. The distinction this study rests on

Cont, Kukanov and Stoikov (2014) regress 10-second mid-price changes on the
**contemporaneous** order flow imbalance over the same 10 seconds, across 50
US stocks. Average R² is **65%** (76% for SLB). That is an explanation, not a
forecast: knowing that order flow moved the price while it was moving the price
is nothing you could have traded on.

This study asks the **predictive** question: does a feature measured over
interval *t* predict the mid-price change over interval *t+1*? Expect predictive
R² to be a small fraction of a percent. The contrast between 65% and that
number is the point, and the question is whether what remains survives costs.

## 2. Primary hypothesis — exactly one

*(One sentence. One feature, one horizon, one direction, one metric.)*

Example shape: "OFI over the prior N book events at the inside predicts the
sign of the next mid-price change over the following N events, with
out-of-sample directional accuracy above 50%."

## 3. Secondary hypotheses and multiple-comparison correction

The other features — queue imbalance, micro-price, trade-sign imbalance,
queue-position-conditioned OFI — are **secondary**. Test them under a
**Holm–Bonferroni** correction across the secondary family. A secondary result
that clears only uncorrected significance is reported as exploratory, not as a
finding.

## 4. Data, venue, and the power problem

### Venue

NASDAQ runs maker-taker. **BX runs taker-maker** — it pays liquidity takers and
charges providers — so maker and taker P&L change sign between them. **Do not
pool BX with NASDAQ sessions.** The study uses a single venue; name it here.

### Sessions

List every session, and which are development and which held out. Assign these
**before writing any feature code.**

### Power — do this before registering

The free sample set is a handful of sessions, and intraday observations within
a session are heavily autocorrelated. The effective sample size is closer to the
number of days than the number of events. A held-out set of two or three
sessions may be unable to distinguish "the signal weakened" from "the signal is
gone."

So, using the **development** sessions only:

- Estimate the variance of the primary metric with **standard errors clustered
  by session** (and by symbol if pooling symbols; symbols on the same day
  co-move), or a **block bootstrap resampling whole sessions**.
- Compute the **minimum detectable effect** at the planned held-out size, at
  α = 0.05 and 80% power.
- Write the MDE here, next to the smallest effect that would matter
  economically after costs.

If the MDE is larger than that economically relevant effect, the study as
designed cannot answer its own question. Options, in order: add sessions
(historical NASDAQ ITCH is sold by LOBSTER and others), add symbols with
clustered inference, or narrow the question to one the data can answer. Record
which you chose.

## 5. Features and horizons

Exact definitions: window, normalisation, and whether event time (next N book
updates) or clock time (next N ms). Event time is the primary axis; clock time
is reported as a robustness check.

## 6. The strategy — specified, not implied

A predictive signal has no P&L until it is a strategy. Specify:

- **Taker or maker.** A taker crosses the spread and pays the take fee on every
  fill. A maker posts at the inside, earns the rebate, fills only when the queue
  reaches it, and eats adverse selection.
- **Entry and exit rules**, order size, holding horizon, and inventory limit.
- **For a maker:** fills come from the queue simulator under the fill rules in
  DESIGN.md §4, with exact MBO queue position. Unfilled quotes are unfilled.

## 7. Cost model

Use the fee schedule **in effect on the session date**, for the venue named in
§4, not today's schedule.

**Fee tier: the tier a small participant would realistically occupy — the
base tier, not the top.** Headline rebates are volume-tiered, and the gap
between the base and top tiers is on the same order as the edge a maker
strategy is trying to capture. This single choice can flip the sign of the
result, so state the tier and its rates here, and report the P&L at the top
tier too, as a sensitivity, so the dependence is visible.

For the 2017–2020 sessions: penny ticks; NASDAQ removal fee ~$0.0030/share,
which was also the Rule 610 access fee cap then. Both change under the 2024
Reg NMS amendments (half-penny tick for tick-constrained stocks; $0.0010 cap),
compliance delayed to November 2026 — irrelevant to this data, which is exactly
why the schedule is named by date.

Charge at minimum:

- Take fee on crossing fills; base-tier rebate on passive fills
- **Adverse selection**, measured as the mid-price move over the horizon after
  each passive fill
- Queue-position-dependent fill probability (maker only)
- Zero market impact, stated as an assumption

## 8. Decision rules

Write these now:

- **Signal holds:** *(the primary-metric threshold, with its CI excluding the
  null)*
- **Signal fails:** *(the CI excludes economically meaningful effects)*
- **Strategy is profitable:** *(net P&L per round trip after all costs in §7,
  with CI excluding zero, at the base tier)*

## 9. What counts as a failure of the study itself

Distinct from the signal failing. If any of these occur, the result is
**inconclusive**, and is reported as inconclusive — not as a negative:

- The held-out MDE exceeds the economically relevant effect from §4
- Book reconstruction on a held-out session fails any correctness layer, or the
  determinism hash changes unexplained
- Fewer than *(N)* simulated fills per session for a maker strategy
- Any held-out data was touched before this file was committed
- *(add your own)*

"We could not tell" is a real result. It is not the same as "it does not work,"
and conflating them is the error this section exists to prevent.

---

## Result

*(Filled in after the single held-out run.)*

Report exactly one of: **signal holds / signal fails / inconclusive**, and
separately, **strategy profitable / unprofitable / inconclusive** at the base
tier, with the top-tier sensitivity alongside.

A likely honest outcome is: predictive power exists in development, the
held-out estimate is weaker, and the maker strategy is unprofitable at the base
tier after adverse selection — *if the study has the power to say so*. If it
doesn't, the result is "inconclusive at this sample size," and that is what gets
written, including on a resume.

If a second run was needed, say so and say why. An honest second look beats a
hidden one.
