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

They are distinct by construction, not by assumption: half-spread-normalised
micro-price deviation is algebraically identical to queue imbalance
(`docs/design.md` record 031), and registering both would have put one feature
in the family twice and tightened every other feature's threshold for no added
evidence. Any feature added to this family in future is checked for an
algebraic relationship to the existing ones before registration.

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

### Power — completed on development sessions only

Intraday observations within a session are heavily autocorrelated, so the
effective sample size is far closer to the number of sessions than to the
number of events. Two sessions held out is a small number, and it is entirely
possible that this study cannot distinguish a weakened signal from an absent
one. That has to be established **before** the held-out run, not discovered
after.

Procedure, run by `research/power.py` on development sessions:

1. Estimate the primary metric per session and per symbol.
2. Estimate its standard error two ways, and report both: standard errors
   **clustered by session**, and a **block bootstrap resampling whole
   sessions** with 10,000 replications. Where symbols are pooled, cluster on
   session × symbol, because symbols on the same day co-move.
3. Compute the **minimum detectable effect** at the planned held-out size
   (2 sessions × 50 symbols), at α = 0.05 and 80% power, two-sided.
4. Record the MDE here, beside the smallest effect that is economically
   meaningful after the costs in section 7.

| | Value |
|---|---|
| Primary metric, development estimate | *(to be filled)* |
| Session-clustered standard error | *(to be filled)* |
| Block-bootstrap standard error | *(to be filled)* |
| Minimum detectable effect, held-out size, α = 0.05, power 0.80 | *(to be filled)* |
| Smallest economically meaningful effect after costs | *(to be filled)* |

**If the MDE exceeds the economically meaningful effect, the study as designed
cannot answer its own question.** Remedies, in order of preference, with the
choice recorded here:

1. Add sessions. `docs/data.md` holds three unassigned NASDAQ sessions in
   reserve (2018-12-13, 2018-12-14, 2018-12-31); historical ITCH is also sold
   commercially.
2. Add symbols, with inference clustered on session × symbol.
3. Narrow the question to one the data can answer, and say so plainly.

Doing none of these and running anyway is not an option: it would produce a
number with no power to be wrong.

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

- **Signal holds.** The primary metric on the held-out sessions exceeds
  *(threshold)*, with a session-clustered 95% confidence interval excluding
  the null (50% for a directional metric, zero for an R² metric).
- **Signal fails.** The 95% confidence interval excludes every effect at or
  above the economically meaningful size from section 4.
- **Signal inconclusive.** The confidence interval contains both the null and
  the economically meaningful effect. This is the expected outcome at this
  sample size, and reporting it as a failure would be wrong.
- **Strategy is profitable.** Mean net P&L per round trip, after every cost in
  section 7, is strictly positive at the **base tier**, with a
  session-clustered 95% confidence interval excluding zero.
- **Strategy is unprofitable.** The same interval lies entirely below zero.
- **Strategy inconclusive.** Otherwise.

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
