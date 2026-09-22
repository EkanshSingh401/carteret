# Pre-registration

This document is committed, fully specified, before any code reads a held-out
session. The commit timestamp relative to the held-out run is the whole of the
claim, and `git log` verifies it. `research/run_heldout.sh` refuses to run
unless `research/heldout.lock` names a commit that is an ancestor of `HEAD` and
this file is unchanged since that commit.

Every section must be answered. An unanswered section means the study is not
registered.

> **Status: draft.** Sections 2, 4, 5, 6 and 8 carry placeholders and candidate
> options rather than final text. The draft is completed on development
> sessions only, per Stage 8, and committed by the author before any held-out
> data is fetched.

---

## 1. Predictive, not contemporaneous

Cont, Kukanov and Stoikov (2014) regress 10-second mid-price changes on the
**contemporaneous** order flow imbalance over the same 10 seconds, across 50 US
stocks, and report an average R² of approximately 65% (76% for SLB). That is a
decomposition of a price move into the order flow that constituted it, not a
forecast.

This study asks the predictive question: does a feature measured over interval
*t* predict the mid-price change over interval *t+1*? Predictive R² on this
data is expected to be a small fraction of one percent. The two quantities are
not comparable and are never reported as though they were; see
`docs/design.md` record 023. The question of interest is whether what remains
after that gap survives transaction costs.

## 2. Primary hypothesis — exactly one

One feature, one horizon, one direction, one metric, in one sentence.

*(Candidate hypotheses are offered in this section of the draft; the author
selects one and deletes the rest before the registration commit.)*

Shape: "Order flow imbalance over the prior *N* book events at the inside
predicts the sign of the next mid-price change over the following *N* events,
with held-out directional accuracy above 50%."

## 3. Secondary hypotheses and multiple-comparison correction

The remaining features — queue imbalance, micro-price, trade-sign imbalance,
queue-position-conditioned order flow imbalance — are secondary. They are
tested under a **Holm–Bonferroni** correction across the secondary family. A
secondary result that clears only uncorrected significance is reported as
exploratory and not as a finding.

## 4. Data, venue, and power

### Venue

NASDAQ only. BX is taker-maker and NASDAQ is maker-taker, so maker and taker
P&L change sign between them and the two are never pooled in a cost-inclusive
result (`docs/design.md` record 022).

### Sessions

Every session is listed in `docs/data.md` with its development or held-out
assignment. The assignment is made before any feature code is written, and
held-out sessions are neither downloaded nor opened until the registration
commit exists.

### Power

The free sample set is a small number of sessions, and intraday observations
within a session are heavily autocorrelated, so the effective sample size is
closer to the number of days than the number of events. A held-out set of two
or three sessions may be unable to distinguish a weakened signal from an absent
one.

Using development sessions only:

- Estimate the variance of the primary metric with standard errors **clustered
  by session** — and by symbol where symbols are pooled, since symbols on the
  same day co-move — or with a **block bootstrap resampling whole sessions**.
- Compute the **minimum detectable effect** at the planned held-out size, at
  α = 0.05 and 80% power.
- Record the MDE here, beside the smallest effect that is economically
  meaningful after the costs in section 7.

If the MDE exceeds that economically meaningful effect, the study as designed
cannot answer its own question. Remedies, in order of preference: add sessions
(historical NASDAQ ITCH is sold by LOBSTER and others); add symbols with
clustered inference; narrow the question to one the data can answer. The choice
is recorded here.

## 5. Features and horizons

Exact definitions: window, normalisation, and whether the axis is event time
(the next *N* book updates) or clock time (the next *N* milliseconds). Event
time is the primary axis; clock time is reported as a robustness check.

## 6. The strategy

A predictive signal has no P&L until it is a strategy. Specified here:

- **Taker or maker.** A taker crosses the spread and pays the take fee on every
  fill. A maker posts at the inside, earns the rebate, fills only when the
  queue reaches it, and bears adverse selection.
- **Entry and exit rules**, order size, holding horizon, inventory limit.
- **For a maker**, fills come from the queue simulator under the fill rules of
  `docs/design.md` record 020, with exact market-by-order queue position.
  Unfilled quotes are unfilled.

## 7. Cost model

Fees are taken from the schedule **in effect on the session date**, for the
venue named in section 4, cited to the archived price list rather than to the
current one.

The tier is the **base tier** — the tier a participant without volume
commitments occupies. Headline rebates are volume-tiered, and the gap between
the base and top tiers is the same order of magnitude as the edge a maker
strategy attempts to capture, so this single choice can flip the sign of the
result. The base-tier rates are stated here, and P&L at the top tier is
reported alongside as a sensitivity so the dependence is visible.

For the 2017-2020 sessions: penny ticks; NASDAQ removal fee of approximately
$0.0030 per share, which was also the Rule 610 access fee cap at the time. Both
change under the 2024 Reg NMS amendments (half-penny tick for tick-constrained
stocks, $0.0010 cap), with compliance delayed to November 2026 — irrelevant to
this data, which is why the schedule is identified by date.

Charged at minimum:

- Take fee on crossing fills; base-tier rebate on passive fills.
- **Adverse selection**, measured as the mid-price move over the horizon
  following each passive fill.
- Queue-position-dependent fill probability, for a maker strategy.
- Zero market impact, stated as an assumption rather than assumed silently.

## 8. Decision rules

Written before the held-out run:

- **Signal holds:** *(primary-metric threshold, with a confidence interval
  excluding the null)*
- **Signal fails:** *(the confidence interval excludes economically meaningful
  effects)*
- **Strategy is profitable:** *(net P&L per round trip after every cost in
  section 7, with a confidence interval excluding zero, at the base tier)*

## 9. Failure criteria for the study itself

Distinct from the signal failing. If any of the following occurs, the result is
**inconclusive** and is reported as inconclusive rather than as a negative:

- The held-out MDE exceeds the economically meaningful effect from section 4.
- Book reconstruction on a held-out session fails any correctness layer, or a
  determinism hash changes without an explanation.
- Fewer than *(N)* simulated fills per session, for a maker strategy.
- Any held-out data was read before this file was committed.

"The data cannot distinguish these hypotheses" is a result. It is not the same
as "the effect is absent", and conflating the two is the error this section
exists to prevent.

---

## Result

*(Completed after the single held-out run.)*

Reported as exactly one of **signal holds / signal fails / inconclusive**, and
separately as **strategy profitable / unprofitable / inconclusive** at the base
tier, with the top-tier sensitivity alongside.

The outcome anticipated at registration time is that predictive power exists in
development, the held-out estimate is weaker, and the maker strategy is
unprofitable at the base tier once adverse selection is charged — provided the
study has the power to establish that. Absent that power, the result is
"inconclusive at this sample size", and that is what is reported everywhere the
study is cited.

A second run, if one occurs, is reported as a second run, with the reason.
