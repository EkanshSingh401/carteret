# Pre-registration

This document is committed, fully specified, before any code reads a held-out
session. The commit timestamp relative to the held-out run is the whole of the
claim, and `git log` verifies it. `research/run_heldout.sh` refuses to run
until `research/heldout.lock` names a commit that is an ancestor of `HEAD` and
this file is byte-identical to its content at that commit.

> **Status: draft, unregistered.** Section 2 offers three candidate primary
> hypotheses and section 4 carries no minimum detectable effect yet. The
> author selects one hypothesis, deletes the other two, completes section 4
> from development sessions only, commits this file, and writes that commit's
> hash into `research/heldout.lock` in a separate commit. Until then the
> held-out sessions must not be downloaded.

---

## 1. Predictive, not contemporaneous

Cont, Kukanov and Stoikov (2014) regress 10-second mid-price changes on the
**contemporaneous** order flow imbalance over the same 10 seconds, across 50
US stocks, and report an average R² of approximately 65% (76% for SLB). That
is a decomposition of a price move into the order flow that constituted it. It
is not a forecast, and nothing in it could have been traded.

This study asks the predictive question: does a feature measured over interval
*t* predict the mid-price change over interval *t+1*? Predictive R² on this
data is expected to be a small fraction of one percent. The two quantities are
not comparable, are never reported side by side as though they were, and
neither is cited in support of the other. See `docs/design.md` record 023.

The question of interest is not whether the predictive R² is large. It is
whether what remains survives transaction costs.

## 2. Primary hypothesis — exactly one

One feature, one horizon, one direction, one metric. The author selects one of
the following and deletes the rest before the registration commit.

**Candidate A — order flow imbalance, directional.**
> Order flow imbalance at the inside, measured over a window of *N* = 50
> consecutive book updates and normalised by mean inside depth over that
> window, predicts the *sign* of the mid-price change over the following 50
> book updates, with held-out directional accuracy strictly greater than 50%
> on windows whose mid-price change is nonzero.

**Candidate B — order flow imbalance, magnitude.**
> A univariate ordinary-least-squares regression of the mid-price change over
> the next 50 book updates on normalised order flow imbalance over the
> previous 50, fitted on development sessions, achieves a strictly positive
> out-of-sample R² on the held-out sessions.

**Candidate C — queue imbalance, directional.**
> Queue imbalance at the inside, *(q_bid − q_ask) / (q_bid + q_ask)*, measured
> at the end of a window of 50 book updates, predicts the sign of the
> mid-price change over the following 50 book updates, with held-out
> directional accuracy strictly greater than 50%.

Candidates A and B use the same feature and differ in what is claimed: A that
the sign is predictable, B that the magnitude is. B is the stronger claim and
the more fragile. Candidate C tests a state variable rather than a flow
variable and is the cheapest to compute, which matters if the strategy in
section 6 is to run on a book update.

Whichever is chosen, the other two move to section 3 as secondaries.

## 3. Secondary hypotheses and multiple-comparison correction

The features not chosen as primary are secondary: order flow imbalance, queue
imbalance, micro-price deviation **in ticks**, trade-sign imbalance, and
queue-position-conditioned order flow imbalance, each at the horizons in
section 5. That is five distinct features.

**The family size depends on the primary metric**, and is fixed here rather
than discovered later. Micro-price deviation and queue imbalance are related
by *micro − mid = (s/2) · QI* with the spread *s* strictly positive
(`docs/design.md` record 031), so:

- If the primary metric uses **magnitude** (candidate B), the two are distinct
  — their correlation is 0.22 — and the family is **five**.
- If the primary metric uses **only the sign** (candidates A and C), the two
  are identical, because they always share a sign. Micro-price deviation is
  then dropped and the family is **four**.

Registering both under a sign-based metric would have entered one feature
twice, tightening every other feature's corrected threshold for no added
evidence and presenting one result as two. Any feature added to this family in
future is checked for an algebraic relationship to the existing ones, in
magnitude and in sign, before registration.

Secondaries are tested under a **Holm–Bonferroni** correction across the
secondary family, with the family size fixed here once the primary is chosen
and not enlarged afterwards. A secondary result that clears only uncorrected
significance is reported as exploratory and not as a finding.

## 4. Data, venue, and power

### Venue

**NASDAQ only.** BX is taker-maker and NASDAQ is maker-taker, so maker and
taker P&L change sign between them; pooling them in a cost-inclusive result is
not a controllable nuisance but a sign flip. See `docs/design.md` record 022.

### Sessions

Assigned in `docs/data.md` before any feature code was written.

| Set | Sessions |
|---|---|
| Development | 2019-01-30, 2019-03-27, 2019-05-30, 2019-07-30, 2019-08-30, 2019-10-18, 2019-12-30 |
| **Held out** | **2019-10-30, 2020-01-30** |

Held-out sessions have not been downloaded. `docs/data.md` records one flaw in
the split: 2019-12-30 falls chronologically after the held-out 2019-10-30,
because it was spent on the Stage 3 correctness gate before the split existed.

### Symbol universe

The 50 symbols with the most book messages in each session, selected per
session from that session's own activity, as in `queue_study`. Fixed here so
that the held-out universe is not chosen after seeing held-out data.

### The inference problem, and four ways out

**The four options below were drafted for the author to choose between. The
choice has been made and is recorded in the next section; the options are kept
here because the reasoning that rejected three of them is part of the
registration, not scaffolding to be deleted once a decision exists.**

`docs/data.md` lists **nine** NASDAQ ITCH 5.0 sessions in the 2017-2020
window. Seven are development and **two are held out**. Three further NASDAQ
sessions (2018-12-13, 2018-12-14, 2018-12-31) are unassigned and held in
reserve.

Two held-out sessions is the whole difficulty, and it cannot be fixed by
analysis. With two clusters:

- A **session-clustered standard error** has two terms. The finite-sample
  correction is *G/(G−1) = 2*, the estimator has one degree of freedom, and
  the usual normal or *t* reference distribution is not even approximately
  right. It is degenerate, not merely imprecise.
- A **session block bootstrap** resamples two units with replacement. It can
  draw only three distinct multisets — {A,A}, {A,B}, {B,B} — so the bootstrap
  distribution has at most three mass points and its percentiles are an
  artifact of that, not an estimate.

Reporting either as though it were a confidence interval would be worse than
reporting nothing, because it would look like inference. The options are:

#### (a) Acquire more sessions

**Assumption:** additional sessions are exchangeable with the ones already
held, so pooling them estimates the same quantity.

The three unassigned NASDAQ sessions in `docs/data.md` are free and would take
the held-out set to five, which is still small but no longer degenerate.
Beyond that, historical NASDAQ ITCH is sold commercially — LOBSTER and
several vendors resell it — at a cost per session-symbol that the author would
need to establish; this project has not obtained a quote, and that figure is
**not** filled in here on the basis of a guess.

*Cost:* money and time, and ~4 GB per session of download at the rates
observed in `docs/data.md`. *Benefit:* the only option that actually fixes the
problem rather than working around it.

#### (b) Intraday block bootstrap

**Assumption:** dependence between observations decays to negligible beyond
some block length *L*, so blocks of length *L* within a session are
approximately independent and may be resampled.

This is the standard move when there are too few natural clusters, and it buys
inference at the cost of an assumption that has to be defended rather than
asserted. Two things must be stated in advance and neither can be chosen after
seeing the result:

- **The block length.** Proposed: **30 minutes of exchange time**, giving 13
  blocks per session and 26 across the held-out set. The justification has to
  be empirical, from the development sessions: the autocorrelation of the
  primary metric computed per block should be indistinguishable from zero at
  lag one by 30 minutes. If it is not, *L* rises and the block count falls,
  which is the trade this option makes.
- **The dependence structure it assumes away.** Blocks within a day share the
  day's volatility regime, its news, and its market-wide moves. A block
  bootstrap treats two blocks from the same session as independent draws, and
  they are not. It will therefore be **anti-conservative** — intervals too
  narrow — by an amount that grows with how much of the metric's variance is
  common to the session rather than local to the block. That fraction is
  estimable on development sessions and must be reported beside any interval
  this option produces.

#### (c) Symbol clustering

**Assumption:** symbols within a session are independent given the session.

They are not, and the direction of the error is known. Symbols on the same day
co-move: an index move, a sector move or a market-wide liquidity event hits
many symbols at once, and the primary metric is a function of order flow that
responds to exactly those. Clustering on symbol therefore treats correlated
observations as independent and produces **intervals that are too narrow** —
anti-conservative, in the same direction as (b) and probably by more, since 50
symbols on one day share more than 13 half-hours do.

It is listed because it is what the queue-position study already uses for its
own intervals, where the quantity is a mechanical property of a queue model
rather than a market-wide signal, and the assumption is far less strained.
Using it for the *signal* study would be a different and weaker claim.

*If chosen, the write-up says "clustered on symbol, which is anti-conservative
under common market moves" every time an interval appears.*

#### (d) Declare the study exploratory

**Assumption:** none. This is the option that assumes nothing.

The held-out set is used as a single out-of-sample look, the point estimate is
reported with no confidence interval, and the study is described as
**exploratory rather than confirmatory** wherever it is cited. The
pre-registration still does its job: it fixes the hypothesis, the features,
the strategy and the costs in advance, so the point estimate is not the best
of many tried. What it cannot do at this sample size is attach a calibrated
probability to that estimate.

*Cost:* no significance claim. *Benefit:* nothing stated is wrong.

#### What is not an option

Running (b) or (c), obtaining an interval that excludes the null, and
reporting it as a confirmatory finding without stating the assumption that
produced it. Both are anti-conservative in a known direction, and an interval
that is too narrow by an unstated amount is not evidence.

### The inference plan — decided

**Fixed on 2026-09-22, before any minimum detectable effect had been
computed.** The ordering matters and is the reason it is stated: choosing
between inference plans by comparing their minimum detectable effects would
systematically select the plan with the narrowest intervals, and the two plans
available here are both anti-conservative in a known direction. A rule that
picks whichever estimator reports the smallest standard error is a rule that
picks whichever estimator assumes away the most dependence. The plan is
therefore chosen on its assumptions alone, and the MDEs are computed
afterwards and used only for the separate question of which hypothesis to
test.

**1. Primary inference: the intraday block bootstrap, plan (b).**

**2. Block length,** chosen by a rule applied to the primary metric on
**development sessions only**: the smallest lag at which the autocorrelation
stays within ±0.05 for **30 consecutive lags**, rounded up to the next whole
minute. The rule is mechanical, it is stated before the autocorrelation
function has been looked at, and it has no free parameter left for a later
choice to enter through. The 30-minute figure proposed in option (b) above is
superseded by whatever this rule returns; if the rule returns a length that
leaves too few blocks for a bootstrap to mean anything, that is a finding
about the data and is reported as one rather than worked around.

**3. Symbol clustering, plan (c), is a sensitivity analysis only.** It is
reported beside the primary result, never in place of it, and every interval
it produces carries the sentence that it is **anti-conservative under common
market moves**: symbols on one day share index moves, sector moves and
market-wide liquidity events, and the primary metric is a function of exactly
the order flow that responds to them.

**4. Hypothesis selection.** Among the three candidates in section 3, the one
tested on the held-out set is the one with the **smallest ratio of its MDE
under plan (b), at the planned held-out size, to its own pre-stated economic
threshold**. Ties go to the simpler feature. **Development effect sizes are
not used for selection** — only the MDE, which is a property of the estimator
and the sample size, and the threshold, which is a property of the cost model
in section 7. Selecting on development effect size would make the held-out
test a test of the largest of three development estimates, which is the
multiple-comparison problem this document exists to avoid, arriving through
the selection step instead of the testing step.

**5. Pre-committed fallback.** If the selected hypothesis's MDE under plan (b)
**exceeds its economic threshold**, the study is declared **exploratory before
any held-out access**, and every result is reported as exploratory. This is
option (d), committed to in advance rather than reached for afterwards. The
fallback is not a failure of the study: a design that can state before looking
that it cannot resolve an effect worth having is doing its job.

**6. Scope of any confirmatory claim.** With two held-out sessions,
**confirmatory claims apply to those sessions and are not generalised beyond
them.** This sentence appears in the write-up wherever a confirmatory result
is stated. A block bootstrap within two days estimates the uncertainty of a
quantity measured on those two days; it says nothing about the distribution of
days from which they were drawn, and no amount of resampling inside them can
make it say anything.

### Power — completed on development sessions only

Intraday observations within a session are heavily autocorrelated, so the
effective sample size is far closer to the number of sessions than to the
number of events. Two sessions held out is a small number, and it is entirely
possible that this study cannot distinguish a weakened signal from an absent
one. That has to be established **before** the held-out run, not discovered
after.

Procedure, run by `research/power.py` on development sessions:

1. Estimate the primary metric per session, per symbol and per intraday block.
2. Estimate its standard error under **both** inference plans that could
   actually be used at the held-out size — (b) the intraday block bootstrap
   and (c) symbol clustering — with 10,000 replications each. The
   session-clustered and session-block-bootstrap estimators are computed on
   the development set too, where there are seven sessions and they are
   meaningful, so that the degeneracy at the held-out size is visible as a
   comparison rather than asserted.
3. Compute the **minimum detectable effect** at the planned held-out size
   under each of (b) and (c), at α = 0.05 and 80% power, two-sided, **for each
   of the three candidate hypotheses**, so the choice of hypothesis can be
   made knowing what each can resolve.
4. Record them below, beside the smallest effect that is economically
   meaningful after the costs in section 7.
5. Apply the selection rule fixed in the previous section: the hypothesis
   tested is the one with the smallest MDE-to-threshold ratio under plan (b),
   ties to the simpler feature; and if that ratio exceeds one, the study is
   declared exploratory before any held-out access.

The block length used in step 2 is whatever the ±0.05-for-30-lags rule
returns on the development sessions, rounded up to the next whole minute. It
is recorded in the table below as a measured quantity, not chosen.

| Candidate | Economic threshold (section 7) | MDE, plan (b) — **primary** | Ratio | MDE, plan (c) — sensitivity |
|---|---|---|---|---|
| A — OFI, directional | *(to be filled)* | *(to be filled)* | *(to be filled)* | *(to be filled)* |
| B — OFI, out-of-sample R² | *(to be filled)* | *(to be filled)* | *(to be filled)* | *(to be filled)* |
| C — queue imbalance, directional | *(to be filled)* | *(to be filled)* | *(to be filled)* | *(to be filled)* |

The **Ratio** column is the selection rule, and the smallest value in it
selects the hypothesis. Plan (c)'s column is reported for the sensitivity
analysis and takes no part in the selection.

| | Value |
|---|---|
| Development estimate of the chosen metric | *(to be filled)* |
| Session-clustered SE, development set (7 sessions) | *(to be filled)* |
| Session block-bootstrap SE, development set | *(to be filled)* |
| Block length *L* returned by the ±0.05-for-30-lags rule | *(to be filled)* |
| Blocks per session at that length, and across the held-out set | *(to be filled)* |
| Intraday block-bootstrap SE, block length *L* | *(to be filled)* |
| Symbol-clustered SE | *(to be filled)* |
| Fraction of metric variance common to the session | *(to be filled)* |
| Smallest economically meaningful effect after costs | *(to be filled)* |

**If the selected hypothesis's MDE under plan (b) exceeds its economic
threshold, the study as designed cannot answer its own question**, and the
pre-committed response is option (d): report the point estimate and call the
study exploratory. That is a decision rule, not a judgement to be made when
the number appears.

Doing none of this and running anyway, then reporting whichever interval
happens to exclude the null, is not an option: it would produce a number with
no power to be wrong.

**The inference plan is chosen and recorded above before the registration
commit, and not changed afterwards.** Switching plans after seeing the
held-out result is the specific failure this whole document exists to prevent,
and it would be undetectable from the outside — which is why the choice, and
the assumption it rests on, are written down where a reader can check them
against the commit date.

## 5. Features and horizons

All features are computed from the reconstructed book, on **event time**,
which is the primary axis. Clock time is reported as a robustness check only.

A **window** is *N* = 50 consecutive updates to either side of the inside
(price or size change at the best bid or best offer). Windows do not overlap.
Windows spanning the open (09:30:00) or the close (16:00:00) are dropped, as
are windows containing a trading halt for that symbol.

**Order flow imbalance (OFI).** For consecutive inside states *n−1*, *n* with
best bid *(P^b, q^b)* and best offer *(P^a, q^a)*:

```
e_n =  1{P^b_n >= P^b_(n-1)} q^b_n  -  1{P^b_n <= P^b_(n-1)} q^b_(n-1)
     - 1{P^a_n <= P^a_(n-1)} q^a_n  +  1{P^a_n >= P^a_(n-1)} q^a_(n-1)
```

OFI over a window is the sum of *e_n* across it, normalised by the mean of
*(q^b + q^a)/2* over the same window. This is the Cont–Kukanov–Stoikov
definition; only the use of it is different (section 1).

**Queue imbalance.** *(q^b − q^a) / (q^b + q^a)* at the last update of the
window.

**Micro-price deviation.** *((q^a P^b + q^b P^a)/(q^a + q^b) − mid)*, in
**ticks**, at the last update of the window.

Deliberately not normalised by the half spread. Normalising makes it
algebraically identical to queue imbalance — *(M − m)/(s/2) = (q^b − q^a)/(q^b
+ q^a)* for every input, derived in `docs/design.md` record 031 — so the two
would be the same feature entered twice in the correction family. In ticks it
equals queue imbalance multiplied by half the spread and therefore carries the
spread as well; measured on a session its correlation with queue imbalance is
0.22 rather than 1.

**Trade-sign imbalance.** Signed executed shares over the window, normalised
by total executed shares. The sign is **known exactly**, not inferred: the
market-by-order feed names the order each execution hits, and the book knows
that order's side, so no Lee–Ready style classifier is used and no
classification error enters the feature.

**Queue-position-conditioned OFI.** OFI with each event weighted by the
fraction of the inside queue it sits in front of, from the exact queue
position the market-by-order book provides.

**Label.** The mid-price change over the *following* window, in Price(4) units
and also in half-spreads at the window boundary. Mid is *(P^b + P^a)/2* on
NASDAQ's own book, not the NBBO.

**Horizons.** Primary *N* = 50 updates. Robustness at *N* = 10 and *N* = 200,
and in clock time at 100 ms, 1 s and 10 s. Robustness results are secondary
and carry the correction in section 3.

## 6. The strategy

A predictive signal has no P&L until it is a strategy.

- **Maker.** Posts one round lot at the inside on the side the signal favours,
  when the signal's magnitude exceeds a threshold fixed in section 8. A taker
  variant is reported as a secondary, because crossing the spread pays the
  full take fee on every fill and the section 7 arithmetic makes it very
  unlikely to clear.
- **Fills** come from the queue simulator under the fill rules of
  `docs/design.md` record 020, with **exact market-by-order queue position**.
  Unfilled quotes are unfilled. Record 030 is why the queue model is named
  here: conservative attribution understates fill rates by up to 60% and
  overstates adverse selection by 54%, so a strategy result is meaningless
  without it.
- **Exit** at the end of the following window, at the mid, as a modelled
  liquidation. This overstates realisable P&L by the exit's own half-spread
  and is stated rather than corrected; a round-trip maker strategy would have
  to earn the spread twice.
- **Order size** one round lot. **Inventory limit** one round lot per symbol:
  no new quote while a position is open in that symbol.
- **Cancellation** after the window ends if unfilled.

## 7. Cost model

Rates are those **in effect on the session dates**, from the NASDAQ price list
as archived on **2019-12-07**
(`web.archive.org/web/20191207142614/https://www.nasdaqtrader.com/trader.aspx?id=pricelisttrading2`),
not the current schedule.

**Base tier — the tier a participant with no volume commitment occupies. This
is the headline case.**

| | Rate |
|---|---|
| Rebate to add displayed liquidity, "All other firms", Tape C (NASDAQ-listed) | **$0.0015 / share** |
| Rebate to add displayed liquidity, "All other firms", Tape A and B | **$0.0020 / share** |
| Charge to remove liquidity, all MPIDs, shares at or above $1.00 | **$0.0030 / share** |

The take fee is flat: it is not tiered, and $0.0030 was also the Rule 610
access fee cap then in force. Securities below $1.00 are excluded from the
study; their schedule differs (no add rebate, 30 basis points to remove).

**Top tier, reported as a sensitivity alongside every P&L figure.**

| | Rate |
|---|---|
| Rebate to add displayed liquidity, greater than 1.50% of consolidated volume added | **$0.00305 / share** |

The gap between $0.0015 and $0.00305 per share is **$0.00155**, which on a
one-cent spread is more than fifteen percent of the gross spread captured.
This single choice can flip the sign of the result, which is why the tier is
named, cited, and reported both ways rather than chosen silently.

Charged at minimum:

- Add rebate on passive fills at the base tier; take fee on any crossing fill.
- **Adverse selection**, measured as the mid-price move over the holding
  horizon following each passive fill. This is not an add-on to the cost
  model, it is the dominant term: the Stage 7 study measures −0.92
  half-spreads at one second on BX, which is more than the entire spread
  captured.
- Queue-position-dependent fill probability, from exact position.
- **Zero market impact**, stated as an assumption rather than assumed
  silently. Defensible for one round lot and not for more.
- No clearing, settlement, borrow, or regulatory fees. Stated as excluded.

## 8. Decision rules

Written before the held-out run. Thresholds marked *(to be filled)* are set
from the section 4 power analysis, on development sessions only, before the
registration commit.

**Every interval below is the intraday block bootstrap of section 4 at the
block length that section's rule returns.** An earlier draft of these rules
said "session-clustered", which with two held-out sessions is the degenerate
estimator section 4 rejects; the correction is recorded here rather than made
silently. The symbol-clustered interval is reported alongside as a
sensitivity, carries its anti-conservative label, and **decides nothing**: if
the two disagree, the verdict follows the block bootstrap and the
disagreement is reported.

- **Signal holds.** The primary metric on the held-out sessions exceeds
  *(threshold)*, with a 95% block-bootstrap confidence interval excluding the
  null (50% for a directional metric, zero for an R² metric).
- **Signal fails.** The 95% interval excludes every effect at or above the
  economically meaningful size from section 4.
- **Signal inconclusive.** The interval contains both the null and the
  economically meaningful effect. This is the expected outcome at this sample
  size, and reporting it as a failure would be wrong.
- **Strategy is profitable.** Mean net P&L per round trip, after every cost in
  section 7, is strictly positive at the **base tier**, with a 95%
  block-bootstrap confidence interval excluding zero.
- **Strategy is unprofitable.** The same interval lies entirely below zero.
- **Strategy inconclusive.** Otherwise.

**If the study has been declared exploratory** under the section 4 fallback,
none of the six verdicts above is available. The point estimate is reported,
the interval is omitted rather than computed, and the write-up says so.

Every confirmatory verdict is stated with the scope sentence fixed in
section 4: with two held-out sessions, it applies to those sessions and is not
generalised beyond them.

The signal verdict and the strategy verdict are reported **separately**. A
signal that holds while the strategy loses money is a coherent and likely
result, and collapsing the two would hide it.

Signal threshold *(to be filled)*. Minimum fills per session *(to be filled)*.

## 9. Failure criteria for the study itself

Distinct from the signal failing. If any of these occurs, the result is
**inconclusive** and is reported as inconclusive, not as a negative:

- The held-out minimum detectable effect exceeds the economically meaningful
  effect from section 4.
- Book reconstruction on a held-out session fails any correctness layer in
  `docs/correctness.md`, or a determinism hash changes without explanation.
  `research/run_heldout.sh` runs the census, the determinism hashes and the
  differential replay on each held-out session before the study, for this
  reason.
- Fewer than *(N)* simulated fills per held-out session for the maker
  strategy.
- Any held-out data was read before the registration commit existed.
- The held-out sessions turn out to contain a market-wide event that the
  development sessions have no counterpart for. This is a judgement call and
  is made and recorded **before** looking at the study output, from the
  session's `S`, `H` and `V` messages alone.

"The data cannot distinguish these hypotheses" is a result. It is not the same
as "the effect is absent", and conflating the two is the error this section
exists to prevent.

---

## Result

*(Completed after the single held-out run. Nothing is written here before it.)*

Reported as exactly one of **signal holds / signal fails / inconclusive**, and
separately as **strategy profitable / unprofitable / inconclusive** at the base
tier, with the top-tier sensitivity alongside.

The outcome anticipated at registration is that predictive power exists in
development, the held-out estimate is weaker, and the maker strategy is
unprofitable at the base tier once adverse selection is charged — provided the
study has the power to establish that. Absent that power, the result is
"inconclusive at this sample size", and that is what is reported everywhere
the study is cited.

A second run, if one occurs, is reported as a second run, with the reason.
`research/run_heldout.sh` refuses to overwrite an existing result, so a second
run requires moving the first aside deliberately.
