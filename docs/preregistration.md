# Pre-registration

This document is committed, fully specified, before any code reads a held-out
session. The commit timestamp relative to the held-out run is the whole of the
claim, and `git log` verifies it. `research/run_heldout.sh` refuses to run
until `research/heldout.lock` names a commit that is an ancestor of `HEAD` and
this file is byte-identical to its content at that commit.

> **Status: REGISTERED.** The primary hypothesis is **candidate C**, selected
> by the rule in section 4 on development sessions only. The gated computation
> (2026-09-24, `research/gated.py`) is complete: *m*, the block lengths, the
> MDE table, the selection, the minimum-blocks guard and the fallback all carry
> measured values, and every figure quoted here is checked against the
> computation's own output by `tools/check_numbers.py` in CI.
>
> Candidates A and B are retained, marked **considered, not selected**. On
> held-out data they are exploratory only — outside every confirmatory claim
> and outside the Holm–Bonferroni family.
>
> **No held-out session has been downloaded.** The manifests in
> `docs/manifests/` record the contents of `data/` and `results/` with digests
> at each step, and say so rather than asserting it. `research/heldout.lock`
> names this commit, the CI run that went green on it, and this file's SHA-256
> at that commit; `research/run_heldout.sh` refuses to run if any of the three
> stops matching.

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

One feature, one horizon, one direction, one metric.

> **REGISTERED PRIMARY: candidate C.** Selected by the rule in section 4 —
> smallest MDE-to-threshold ratio under plan (b) — on 2026-09-24. No tie, so
> the tie-break was not reached.
>
> **A and B were considered and not selected by the registered rule.** They
> are kept in this document rather than deleted, because the rule that
> rejected them is only checkable if what it chose between is visible. On
> held-out data they are reported as **exploratory results only**: outside any
> confirmatory claim, outside the Holm–Bonferroni family of section 3, and
> never described as tested hypotheses. An exploratory number that clears a
> bar is still exploratory.

**Candidate C — queue imbalance, directional. REGISTERED PRIMARY.**
> Queue imbalance at the inside, *(q_bid − q_ask) / (q_bid + q_ask)*, measured
> at the end of a window of 50 book updates, predicts the sign of the
> mid-price change over the following 50 book updates, with held-out
> directional accuracy strictly greater than 50%.

**Candidate A — order flow imbalance, directional. Considered, not selected.**
> Order flow imbalance at the inside, measured over a window of *N* = 50
> consecutive book updates and normalised by mean inside depth over that
> window, predicts the *sign* of the mid-price change over the following 50
> book updates, with held-out directional accuracy strictly greater than 50%
> on windows whose mid-price change is nonzero.

**Candidate B — order flow imbalance, magnitude. Considered, not selected.**
> A univariate ordinary-least-squares regression of the mid-price change over
> the next 50 book updates on normalised order flow imbalance over the
> previous 50, fitted on development sessions, achieves a strictly positive
> out-of-sample R² on the held-out sessions.

Candidates A and B use the same feature and differ in what is claimed: A that
the sign is predictable, B that the magnitude is. B is the stronger claim and
the more fragile. Candidate C tests a state variable rather than a flow
variable and is the cheapest to compute, which matters if the strategy in
section 6 is to run on a book update.

### Why C, when a units-consistent rule would have chosen B

This is the least comfortable fact in the document, and it is put here, in the
section that names the primary, rather than in a footnote.

The registered selection rule divides the MDE by the **threshold**. For a
directional metric that threshold is a level near 0.55; for an R² metric it is
already a difference, because that metric's null is zero. The rule therefore
compares unlike quantities, and it flatters the directional candidates by
roughly seventeen times. **Under a units-consistent rule — the MDE against the
effect that actually has to be detected — candidate B has the smallest ratio
and would have been selected.**

| Candidate | Registered ratio | Units-consistent ratio | Development estimate | Its bar |
|---|---:|---:|---:|---:|
| A — OFI, directional | 0.0065 | 0.0698 | 0.56219 | 0.5516 |
| B — OFI, R² | 0.0421 | **0.0421** | **0.00551** | **0.0261** |
| C — queue imbalance | **0.0056** | 0.0603 | **0.62409** | **0.5516** |

**Note what the last two columns say.** B's development estimate, R² =
0.00551, lies **below** its own bar of 0.0261. C's, 0.62409, lies **above**
its bar of 0.5516. The rule that would have selected B would have selected the
candidate that development data says does not clear its threshold. That is
visible only now, which is precisely why it cannot be used to choose.

C is retained, for three reasons stated in full:

1. **It is the registered rule's output.** That rule was fixed before any MDE
   existed, and it is the only selection in this study not made with
   development estimates in view. Every alternative on the table now —
   including the units-consistent one — is a rule being considered *after* its
   answer is known. That asymmetry is the whole content of a pre-registration,
   and it does not stop applying because the registered rule turned out to be
   imperfect.
2. **The defect does not touch the rule's purpose.** The ratio exists to
   exclude candidates the study cannot resolve. Under correct units B is
   powered about 24× over and C about 17× over; both clear that purpose by a
   wide margin. The units error changes the *ranking* of two adequately
   powered candidates, not the *decision* the rule was built to make.
3. **C is the pre-registered preference among comparable candidates.** Section
   4 already fixes that ties go to the simpler feature, which is C: queue
   imbalance is a state variable read at one instant, order flow imbalance a
   flow accumulated over a window. Where the rule's discrimination between
   them is an artifact, the registered preference is what remains.

None of this makes the registered rule correct. It is recorded so that a
reader can disagree with the choice on the same evidence the author had.

## 3. Secondary hypotheses and multiple-comparison correction

The features not chosen as primary are secondary: order flow imbalance, queue
imbalance, micro-price deviation **in ticks**, trade-sign imbalance, and
queue-position-conditioned order flow imbalance, each at the horizons in
section 5. That is five distinct features, and the direct-value test
registered below is a sixth member of the family.

**The family size depends on the primary metric**, and is fixed here rather
than discovered later. Micro-price deviation and queue imbalance are related
by *micro − mid = (s/2) · QI* with the spread *s* strictly positive
(`docs/design.md` record 031), so:

- If the primary metric uses **magnitude** (candidate B), the two are distinct
  — their correlation is 0.22 — and the family is **five**, plus the
  direct-value test below: **six**.
- If the primary metric uses **only the sign** (candidates A and C), the two
  are identical, because they always share a sign. Micro-price deviation is
  then dropped and the family is **four**, plus the direct-value test:
  **five**.

The direct-value test is counted in the family. It is a distinct claim about a
distinct quantity, and exempting it because it happens to test the same
economic requirement as the primary would be the multiple-comparison problem
by another name.

**The family size does not shrink because A and B became exploratory.** With C
registered as primary, the non-selected candidates are reported on held-out
data as exploratory results outside every confirmatory claim (section 2). A
reader might then argue that order flow imbalance has left the secondary
family, since candidates A and B *are* the OFI claims, which would reduce the
family from five to three and **loosen** every remaining secondary's corrected
threshold.

That argument is rejected and the family stays at **five**. Holm–Bonferroni
controls the error rate over the tests actually conducted, and OFI is still
going to be looked at on held-out data — calling that look exploratory changes
its status in the write-up, not the number of comparisons made. Shrinking a
correction family after the primary is known, in the direction that makes the
survivors easier to clear, is the multiple-comparison problem arriving through
the bookkeeping. The conservative reading costs nothing here and is the one
registered.

Registering both under a sign-based metric would have entered one feature
twice, tightening every other feature's corrected threshold for no added
evidence and presenting one result as two. Any feature added to this family in
future is checked for an algebraic relationship to the existing ones, in
magnitude and in sign, before registration.

**Direct-value secondary, registered here.** The primary's conversion from an
economic requirement to an accuracy bar assumes |Δ| is independent of whether
the signal was right (section 4). This secondary tests the requirement with
**no conversion at all**:

> **V** = the mean **signed** mid-price change in the predicted direction, in
> half-spreads, over **every window** —
> *V = mean( Δ · sign(feature) )* — tested against **0.10**, one-sided,
> under the same stationary bootstrap and block length as the primary.
> A window with no move contributes Δ = 0; a window whose feature is exactly
> zero contributes *sign* = 0. Both stay in the denominator. See the
> correction below.

0.10 is the economic requirement itself, in the units it was stated in, so
this test needs neither *m* nor the independence assumption. It is a member of
the secondary family and carries the Holm–Bonferroni correction like any
other.

#### Correction: V averages over all windows, and a zero feature contributes zero

**Made 2026-09-24, in the stricter direction, before the registration commit.**
Two points about V's population, both settled here so that the held-out
computation cannot decide them.

**A zero feature contributes zero and the window stays in the denominator.**
*sign(0) = 0*, so a window in which the signal took no side contributes
nothing to the numerator and still counts in the denominator. This is what the
formula already says, and it is what the requirement means: 0.10 half-spreads
**per window**, and a window in which the signal declined to take a side
earned nothing in it. Dropping such windows would measure the value of the
signal *when it fires* — a different and easier quantity, since the strategy
still sat through the others. On development, dropping them would raise V for
queue imbalance from 0.32542 to 0.36219, so the registered treatment is the
conservative one. `research/study.py: direct_value()` implements it, and the
same function serves the development and held-out paths so the two cannot
diverge.

**V averages over all windows, not only those that moved.** This section
originally said "over windows with a nonzero move". That conditions the
average on the move while testing it against a requirement stated *per
window*, and so divides by the fraction of windows that move — the identical
error corrected in *m* in section 4, and corrected here for the same reason
and in the same direction. Zero-move windows have Δ = 0 and contribute
nothing to the numerator, so the correction is exactly a change of
denominator.

| Feature | V over moved windows | **V over all windows** | Requirement |
|---|---:|---:|---:|
| `queue_imbalance` | 0.53204 | **0.32542** | 0.10 |
| `ofi` | 0.21077 | **0.12892** | 0.10 |

The conditional form overstates value per window by 1/0.61164 = 1.635×. Both
features still clear 0.10 on development under the corrected definition, so
the correction changes no development conclusion; it is made because the
definition was wrong, not because the answer needed changing.

This is a **secondary**, and the fix follows from the registered value
requirement rather than from anything seen in the data, which is why it is
made now rather than recorded and left. The primary metric is **unchanged**.

**If the primary and this secondary disagree, the disagreement is the
finding** and is reported as one: it localises the failure to the
independence assumption rather than to the signal. Accuracy clearing while
value does not means the signal is right on small moves and wrong on large
ones; value clearing while accuracy does not means the reverse, and the
accuracy bar was the wrong instrument. Neither is reported as a bare win for
whichever test happened to pass.

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

**2. Block length,** by **Politis–White automatic selection** (Politis &
White 2004, with the Patton, Politis & White 2009 correction), as implemented
by `arch.bootstrap.optimal_block_length`. This **replaces** an
autocorrelation-band rule that did not survive being applied; the amendment
and the evidence for it are recorded below.

- **The series.** The **per-window summand of the primary metric**, in event
  order: the hit indicator *1{sign(feature) = sign(label)}* for candidates A
  and C, and for candidate B the per-window **squared-error reduction**
  *d = (y − baseline)² − (y − ŷ)²*, per the amendment below — **not** the
  squared-error term this section named until 2026-09-24. **No time binning.** The quantity resampled is the quantity whose dependence the block
  length must span, and binning was the free parameter that broke the previous
  rule.
- **The estimate used** is the selector's **stationary** figure, because the
  bootstrap below is the stationary bootstrap.
- **Across development sessions:** computed per session, and the **maximum**
  is taken. Underestimating dependence narrows intervals, so the conservative
  direction is the longer block.

**2b. The bootstrap** is the **stationary bootstrap** (Politis & Romano 1994)
at that expected block length, resampling **within sessions only**. A block
never crosses a session boundary: an overnight gap is not a dependence
structure the bootstrap should be free to splice across.

**2c. Minimum-blocks guard.** If

> (held-out windows) / (selected block length) **< 20**

the study is **declared exploratory before any held-out access**. A bootstrap
over fewer than twenty effective blocks is the same degeneracy that two
session-level clusters produce, arriving by a different route, and it is
better caught by a stated arithmetic than by judgement after the fact.

**2d. Pre-committed sensitivity.** Every reported interval also appears at
**0.5× and 2× the selected block length**. The selector is an estimator with
its own error; reporting one interval from one estimate would present that
error as absent.

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

The block length used in step 2 is whatever **Politis–White automatic
selection** returns on the development sessions, per section 4 step 2: the
stationary figure, computed per session on the per-window summand in event
order, with the maximum taken across sessions. There is no rounding to a time
unit, because there is no time binning. It is recorded in the table below as a
measured quantity, not chosen.

*(This paragraph named the ±0.05-for-30-lags rule until 2026-09-24. That rule
was retired by the amendment in section 4 and the sentence was left behind;
correcting it changes no procedure, because section 4 is what the code
implements. The retired rule's evidence is kept below on purpose.)*

#### Amendment: the autocorrelation-band rule was withdrawn and replaced

**Status: amended. The rule below is no longer in force; section 4's
Politis–White procedure replaces it. The evidence that retired it is kept
here, because an amendment whose grounds are deleted is indistinguishable
from a change of mind.**

**The two defects.**

1. **Underspecified.** The rule named "the autocorrelation of the primary
   metric" without defining the series it is computed on. A per-window metric
   has no autocorrelation function until it is aggregated into something with
   a time index, and the aggregation was left open. That is not a detail: the
   table below shows the choice moving the answer by two orders of magnitude,
   from 1 minute to 319 minutes.
2. **Miscalibrated.** A **fixed** ±0.05 band was tested against an estimator
   whose own standard error is about **1/√n** and therefore varies with the
   same choice. At a 60-second bin the band equals one standard error, so ρ
   leaves it by chance every few lags and "30 consecutive lags inside" is
   reached only by luck. A tolerance that does not scale with the precision of
   what it is testing is not a criterion.

**Why the replacement has no free parameter to steer.** Politis–White takes
the series and returns a length; there is no bin, no band and no run length to
choose. The one remaining choice — which series — is now fixed explicitly as
the metric's own per-window summand in event order, which is the series the
bootstrap actually resamples. The stationary/circular choice is determined by
the bootstrap used, and the multi-session rule is fixed as the maximum in the
conservative direction.

**Circumstances of the amendment, stated because they matter.** The
sensitivity table below existed, for **one** development session, before this
amendment was written. Any bin chosen from that point would have been chosen
knowing the block length it produced, which is why no bin was chosen and the
rule was replaced outright rather than repaired. **The replacement's output
had not been computed when this amendment was committed** — the selector had
been run only on synthetic AR(1) data to confirm the call signature. The
sequence is visible in the history: this amendment, then a push, then CI, then
the computation.

#### Amendment: candidate B's summand was wrong for an R² metric

**Committed 2026-09-24, before any development-set output existed.** The error
was in **this document's specification**, not in the code that implemented it:
the code did what section 4 said, and what section 4 said was wrong.

**What was registered.** Section 4 named the per-window summand for candidate
B as *the per-window squared-error term*. That is the summand of the **mean
squared error**. Candidate B's metric is not MSE, it is **R²**, and R² is a
**ratio**: *R² = 1 − MSE/S*, with *S* the baseline forecast's mean squared
error.

**Why that is an error and not a presentational choice.** Recovering an R²
standard error from an MSE standard error means dividing by *S* and treating
*S* as a known constant. It is not one — it is estimated from the same
windows. Under a weak signal, which is the regime this study is in, *MSE* and
*S* are close in value and **strongly positively correlated across resamples**:
a resample with unusually large moves inflates both together, leaving the
ratio almost unmoved. Holding the denominator fixed discards that cancellation
and charges the ratio with the full sampling variability of its numerator. The
result overstates the uncertainty of R², and the overstatement is severe.

**What is registered now.**

- The per-window summand for B is the **squared-error reduction** against B's
  baseline forecast: *d = (y − baseline)² − (y − ŷ)²*, with **R² =
  mean(d) / mean(b)** where *b = (y − baseline)²*.
- **The baseline forecast is the training-sample mean of y.** The registration
  said "out-of-sample R²" without naming a baseline; it is pinned here, since
  an R² is meaningless until its baseline is stated.
- **Politis–White block length for B is computed on *d***, the series the
  bootstrap now resamples.
- **Under plan (b), R² is bootstrapped as a ratio statistic** — recomputed
  from both resampled sums on every replication — so the numerator–denominator
  dependence is carried rather than assumed away.
- Under plan (c), the cluster-robust standard error is taken on the ratio's
  **influence function**, *(d − R²·b)/mean(b)*, which is the same estimator
  the directional candidates use, applied to the linearisation.

**This makes candidate B more likely to be selected, not less, and that is
stated plainly.** The correction shrinks B's MDE and therefore its
MDE-to-threshold ratio, which is the selection statistic. An amendment that
improves the standing of one candidate is exactly the kind that invites
suspicion about its timing, so the timing is stated precisely, including the
part of it that cannot be checked.

**What is checkable, and what is not.** The amendment was committed as
`4d01003`. At that moment `results/` contained only the BX smoke-test file and
no development feature file had been written. **That state is not recoverable
from git.** `results/` and `data/` are gitignored — correctly, because market
data is not redistributable — and git records nothing whatever about an
untracked path. The commit message of `4d01003` says "the check is in the
history"; **that sentence is wrong**, and this paragraph is the correction.
History is not rewritten to fix it (`docs/history.md`), so the incorrect
sentence stands in the log with the correction recorded here.

What a reader can check: the commit's author date, its position in the
history relative to the commit that carries the gated computation's outputs,
and — from `tools/manifest.sh` onward — a committed manifest of `data/` and
`results/` at each of the points section 10 names. What a reader cannot check
is the state of an untracked directory at a commit that predates the manifest.
That is a real gap, it is the reason the manifest exists, and asserting it
away would be worse than recording it.

**The number that prompted it came from a smoke test, and was not a result.**
A ratio of **1992** for candidate B was produced by a mechanical run of
`research/gated.py` against `results/features/bx_2019-01-30.csv` — **BX, a
different venue, taker-maker, and not a development session**. It was run to
check that the script executed end to end, and its only role was to make the
units error visible: a ratio three orders of magnitude from the others is not
a finding about a signal, it is a sign that two quantities are not in the same
units. No development session had been exported at that point.

#### Amendment: the held-out window projection is the development minimum

**Committed 2026-09-24, with the above.** The MDE scales to the held-out size,
and the registration pinned the held-out *universe* — two sessions, fifty
symbols — without pinning the **window count** that the scaling needs.

- **Primary: the development per-session minimum × 2.** A smaller held-out
  sample produces a larger MDE, and the fallback exists to catch the case
  where the study cannot resolve an effect worth having. Projecting from the
  mean would understate the MDE whenever a held-out session is quieter than
  the development average — which is the case the guard is for. The
  conservative direction is the one that risks declaring the study exploratory
  when it need not be, not the one that risks a confirmatory claim the sample
  cannot support.
- **Sensitivity: the development mean × 2**, reported beside it.
- **The fallback decision uses the primary.**

**The evidence.** The rule fixes the tolerance (±0.05), the run length (30 consecutive lags) and
the rounding. It does not fix **the time bin the metric is aggregated into
before the autocorrelation is taken**, and on `12302019.NASDAQ_ITCH50` that
choice moves the answer by two orders of magnitude:

| Bin | First qualifying lag | Block length |
|---:|---:|---:|
| 1 s | 2 s | **1 minute** |
| 5 s | 10 s | **1 minute** |
| 10 s | 20 s | **1 minute** |
| 30 s | 8,850 s | **148 minutes** |
| 60 s | 19,140 s | **319 minutes** |

A 319-minute block on a 390-minute session yields one block, which is not a
bootstrap. Both ends are artifacts, by two different mechanisms, and neither
is a statement about dependence in the data.

**Small bins: noise in the metric.** At a 1-second bin there are 10.4
qualifying windows per bin, so the per-bin hit rate is mostly binomial
sampling noise — observed standard deviation 0.2393 against a noise floor of
0.1554, so roughly 65% of the variance is noise. Noise is uncorrelated, so it
drives the autocorrelation towards zero and the rule fires almost
immediately. The block is short because the series is noisy, not because
dependence is absent.

**Large bins: noise in the autocorrelation estimate.** The rule demands 30
consecutive lags inside ±0.05, but the estimate has its own standard error of
about 1/√n:

| Bin | Bins in a session | SE(ρ) | Tolerance in SE |
|---:|---:|---:|---:|
| 1 s | 23,400 | 0.0065 | 7.7 |
| 10 s | 2,340 | 0.0207 | 2.4 |
| 30 s | 780 | 0.0358 | 1.4 |
| 60 s | 390 | 0.0506 | **1.0** |

At a 60-second bin the tolerance **equals one standard error**, so ρ wanders
outside ±0.05 by chance every few lags and 30 consecutive lags inside it is
reached only by luck — which is exactly what
`docs/figures/block_length_dev_2019-12-30_directional.png` shows past the
2,000-second mark.

So the rule is reliable only where the bin is large enough that the metric is
not mostly noise **and** small enough that ±0.05 is several standard errors of
ρ. Those two requirements point in opposite directions, and nothing in the
registration says where to stand between them.

**Both remedies were available** — fix the bin by a minimum count per bin, or
scale the tolerance to SE(ρ) — and **neither was taken**, because the table
above had already been seen and either would have been a parameter chosen with
its consequence visible. The rule was withdrawn instead. The figure and the
two tables are computed on development data only and are retained as the
grounds for that withdrawal.

#### The economic thresholds, fixed before any MDE was computed

One economic requirement, expressed in each metric's own units. Stating it
once and converting keeps the three candidates commensurable, which is what
the selection rule's ratio depends on.

**The requirement.** The signal must shift the expected mid move by at least
**0.10 half-spreads per window**. On the one-cent inside spread that is modal
for these names that is **$0.0005 per share**: one third of the $0.0015 add
rebate the strategy is paid at the base tier, and one sixth of the $0.0030
take fee. Below that the signal is not worth acting on, because the edge is
smaller than the rounding in the fee schedule it has to survive.

Why this rather than "positive net P&L": a passive fill captures the half
spread, so a strategy with *no signal at all* is close to break-even at the
base tier — Stage 7 measures adverse selection at 0.77 to 1.06 half-spreads
against a captured 1.0, leaving roughly the rebate. A threshold of "positive"
would therefore be cleared by a signal worth nothing. The requirement above is
what the signal itself must add.

**Converted into each metric, in full.** The conversion needs one measured
quantity, and an earlier version of this section used it without stating it or
measuring it.

*The scale.* Let **m** be the mean absolute mid-price change per window, in
half-spreads, over windows whose change is nonzero — the same conditioning the
directional metric uses. This is a **scale of the data, not an effect size of
any signal**, and it is measured on development sessions only. It enters
because an edge expressed in half-spreads has to be compared against how far
the mid actually moves in a window.

*Directional metrics (A, C).* A signal that picks the side and is right with
probability *p* earns, in expectation per window,

> E[Δ · sign(signal)] = (2p − 1) · m   half-spreads,

**and that step assumes |Δ| is independent of whether the signal was right.**
The assumption is stated because it is not obviously true and could fail in
either direction. A signal that is right mainly on small moves and wrong on
large ones clears the accuracy bar while losing money; one that is right on
the large moves and wrong on the small ones makes money while missing the bar.
Order flow imbalance is a plausible candidate for the second: large imbalances
both predict more strongly and precede larger moves. So the conversion is a
convenience, and section 3 registers a secondary that does not need it.

Setting the expectation equal to the 0.10 half-spread requirement gives

> **p = ½ + 0.05 / m**

*Magnitude metric (B).* The same edge under the Gaussian sign relation
*p = ½ + arcsin(ρ)/π* gives *ρ = sin(π(p − ½))* and *R² = ρ²*.

*The measured value.* On `12302019.NASDAQ_ITCH50`, over 239,162 windows with a
nonzero move, **m = 1.5543 half-spreads** (median 1.50; mean absolute move
1.66 ticks against a mean spread of 2.93 ticks). The final value is the same
statistic pooled over all seven development sessions; the thresholds below are
**provisional on one session** until those are in hand. No freedom remains in
them — the economic requirement and the formula are fixed here, and *m* is
measured, not chosen.

| Candidate | Metric | **Economic threshold** | Provisional |
|---|---|---:|---|
| A — OFI, directional | held-out directional accuracy | ½ + 0.05/m | **53.22%** |
| B — OFI, out-of-sample R² | held-out out-of-sample R² | sin²(π(p−½)) | **0.0102** |
| C — queue imbalance, directional | held-out directional accuracy | ½ + 0.05/m | **53.22%** |

#### Registered form: the thresholds are functions, not numbers

**Fixed here. Only *m* is measured later.**

> **Accuracy bar (A, C):**  p\* = ½ + 0.05 / m
> **R² bar (B):**  R²\* = sin²( π · (0.05 / m) )

*m* is the **window-weighted** mean of |Δ| in half-spreads over **every
window, including those in which the mid did not move**, pooled across all
seven development sessions — not the mean of per-session means, which would
weight a quiet session equally with a busy one.

#### Correction: *m* averages over all windows, not only those that moved

**Made 2026-09-24, with the development estimates in view, and in the stricter
direction.** This section originally defined *m* over windows with a nonzero
move — the same conditioning the accuracy metric uses. That was wrong, and it
made the bar too easy.

**The two populations are not the same, and must not be.**

| Quantity | Population | Why |
|---|---|---|
| accuracy *p* | windows with a **nonzero move** | a zero move has no sign to predict, so such windows carry no directional information |
| scale *m* | **all** windows, zero moves included | the requirement is 0.10 half-spreads **per window**, and a window in which the mid did not move is still a window the strategy sat through |

Over all windows, with the independence assumption already stated,

> E[Δ · sign(signal)] = P(move) · (2p − 1) · E[|Δ| ‖ move] = (2p − 1) · *m*<sub>all</sub>

so *p*\* = ½ + 0.05 / *m*<sub>all</sub>, with *p* still the **conditional**
accuracy. Defining *m* over moved windows only sets the bar as though every
window moved, and understates what the signal must achieve by exactly the
fraction of windows that move.

That fraction is **0.6116** <!--gen:frac_moved--> here, so the error was
large: *m* falls from 1.5835 <!--gen:m_moved--> to **0.9686**
<!--gen:m_all-->, and the bars rise.

| Bar | One session, moved only | Seven sessions, moved only | **Seven sessions, all windows** |
|---|---:|---:|---:|
| Accuracy `½ + 0.05/m` | 53.22% | 53.16% <!--gen:accuracy_bar_moved--> | **55.16%** <!--gen:accuracy_bar--> |
| R² `sin²(π·0.05/m)` | 0.0102 | 0.0098 <!--gen:r2_bar_moved--> | **0.0261** <!--gen:r2_bar--> |

**This is a correction derived from the registered requirement, not a change
to it.** The requirement — 0.10 half-spreads per window — is the same sentence
it always was; what changed is that the conversion now uses the population the
phrase "per window" names. It is recorded here rather than quietly applied
because it was made **after** the development estimates were visible, which is
the circumstance under which a threshold change is least trustworthy. Two
things make it checkable: the direction is **stricter**, against the author's
interest in clearing the bar, and the arithmetic follows from a requirement
fixed before any estimate existed.

At the gated step *m* is computed **before the MDE table**, and the
one-session value (1.5543) and the seven-session value are reported **side by
side**, with the resulting bars for each. If they differ materially that is
itself worth seeing: it says the scale is session-dependent, and therefore
that a bar calibrated on one session would have been the wrong bar.

**Nothing about the bars is decided after the MDEs exist.** The formula, the
definition of *m*, the pooling rule and the order of computation are all
fixed by this section.

#### Disclosure: what had been computed when the thresholds were revised

The revision from 55.0%/0.0245 to the formula above is commit `126778b`
(2026-09-23 10:08:52 −0400). The original bars are commit `57c70cc`
(09:58:45), and the retired block-length rule's evidence is `6f75527`
(10:03:13). Between those commits a directional **hit series was built**, so
the question of what had been seen is a fair one and is answered exactly.

**Computed and displayed at the time of the revision:**

- Feature and label distributions: mean, standard deviation, min and max of
  `ofi`, `queue_imbalance`, `label_ticks` and `spread_ticks`; the share of
  windows with a nonzero label (0.5866).
- The scale that drove the revision: mean, median and quartiles of |Δ| in
  half-spreads (**m = 1.5543**), mean |Δ| in ticks (1.6645), mean spread in
  ticks (2.9312).
- For **candidate A's hit series only**: per-bin hit-rate **standard
  deviations** at 1, 5, 10, 30 and 60-second bins; a binomial reference
  standard deviation computed at an **assumed** p = 0.5; their ratio; lag-1
  autocorrelations; the full autocorrelation functions; and the block lengths
  the retired rule returned.

**Not computed and not displayed:**

- **Any candidate's directional accuracy** — the mean of the hit series.
- Any R², any MDE, any P&L, or any comparison of a candidate against a
  threshold.
- **Candidate C's hit series was never built.** The retired script had a
  `queue` option; it was never run.

**One mean was computed by the machine and never surfaced.** The
autocorrelation function centred the series with `x = x - x.mean()`
(`research/block_length.py` at `6f75527`, line 57). That grand mean *is*
candidate A's development directional accuracy. It was never returned,
printed, logged or recorded, and it has not been seen.

**Do the displayed statistics identify it?** No.

- Dispersion and autocorrelation are **location-invariant**; neither moves
  with the level.
- The binomial reference was computed **at an assumed p = 0.5**, so it is not
  a measurement of anything.
- The observed per-bin dispersion cannot be inverted for the level: at a
  1-second bin, reproducing the observed 0.2393 as binomial noise over 10.4
  windows would require p(1−p) = 0.596, which exceeds the maximum possible
  0.25. The dispersion is dominated by heterogeneity between bins, not by the
  hit rate, and carries no information about it.

**And the revision did not use a performance quantity at all.** *m* is a
property of the **label distribution alone** — it involves no feature, no
signal and no prediction — so it cannot encode any candidate's performance
even in principle.

**Correction.** The first version of this table read 55.0% and 0.0245. Those
numbers assumed **m = 1**, silently: they took "shift the expected move by
0.10 half-spreads" to mean *2p − 1 = 0.10*, which holds only if a window's
typical move is exactly one half-spread. It is not — it is about one and a
half — so the bar was set roughly 1.8 percentage points of accuracy too high,
and the R² bar was more than twice too high. The requirement itself is
unchanged at 0.10 half-spreads per window; what changed is that the conversion
now states its assumption and measures it.

A and C carry the same threshold because they are the same claim about the
same quantity, made from different features. **The tie-break in the selection
rule therefore matters and is already fixed: ties go to the simpler feature,
which is C** — queue imbalance is a state variable read at one instant, while
order flow imbalance is a flow accumulated over a window.

#### Leakage audit

A development directional accuracy of 0.62409 <!--gen:dev_estimate_C--> is high
enough to be worth disbelieving before it is believed. Three checks, run by
`research/leakage.py` on development sessions only; output in
`docs/generated/leakage_audit.txt`.

**The effect is documented, so a large estimate is expected.** Gould and
Bonart, *Queue Imbalance as a One-Tick-Ahead Price Predictor in a Limit Order
Book*, Market Microstructure and Liquidity **2**(1), 2016, establish that queue
imbalance at the inside predicts the direction of the next mid-price move on
LOB data. This study's candidate C is a version of that effect at a 50-update
horizon. Finding it is not evidence of a defect, and not a novel result; the
contribution here is the cost-inclusive held-out test, not the existence of the
signal. That is a reason to check the construction anyway — a leak and a real
effect both produce a large number — not a reason to skip the checks.

**1. Ordering.** The exporter writes, per emitted row, the index of the last
message the feature could have seen and the first message of the interval the
label measures. Over **70,000** sampled rows, 10,000 per session:
**zero violations**. The audit reads the exporter's own stream, so it tests the
code that made the features rather than a description of it. Adding the stream
left the feature files byte-identical — the re-exported digest matches the one
in `docs/manifests/gated-2026-09-24.txt`.

**2. One-message lag.** Features recomputed from the book one message earlier,
with the label and its baseline untouched.

| Feature | Registered | Lagged one message | Change | Excess over ½ kept |
|---|---:|---:|---:|---:|
| `queue_imbalance` (C) | 0.62409 | 0.60902 | −0.01507 | 87.9% |
| `ofi` (A) | 0.56219 | 0.56478 | +0.00259 | 104.2% |

Neither collapses. C decays gently, which is what a real effect does at a
one-message perturbation; A is unchanged within noise, as expected for a
feature aggregated over fifty updates rather than read at an instant.

**3. Label permutation within session.** Labels shuffled inside each session,
which destroys the pairing and preserves every marginal.

| Feature | Observed | Permuted | Null under random pairing | P(feature = 0) | Gap |
|---|---:|---:|---:|---:|---:|
| `queue_imbalance` (C) | 0.62409 | 0.47290 | 0.47282 | 0.05436 | +0.00008 |
| `ofi` (A) | 0.56219 | 0.49147 | 0.49164 | 0.01674 | −0.00016 |

**The null of this test is not 0.5, and saying so matters.** Under random
pairing the expected accuracy is *P(f>0)P(l>0) + P(f<0)P(l<0)*. Labels are
near balanced (0.49936 / 0.50064), so the deviation comes entirely from the
feature: **`sign(0)` is never equal to the sign of a nonzero label, so a
window whose feature is exactly zero is scored as a miss.** The permuted
values match that null to within 0.0002 for both features, which is the pass.

**This exposes a defect in the metric, recorded and not repaired.** Queue
imbalance is exactly zero whenever the two inside queues are equal, which
happens in **5.4%** of moved windows. Those windows are counted as wrong
answers rather than excluded, although the registered hypothesis says the
feature "predicts the sign" and a zero feature predicts no sign — the same
argument by which zero-**label** windows are already excluded. The treatment
is asymmetric. The effect is to bias C's accuracy **downward**: it is measured
against a ceiling of 0.94564, not 1.

**The primary metric is unchanged, and the bias runs the safe way.** Scoring a
zero feature as a miss caps C's achievable accuracy at **0.94564**, not 1, and
pushes the measured value down. The primary test is therefore **conservative**:
whatever accuracy C reports on held-out data, the quantity it is being asked to
clear 0.5516 with has been handicapped, and a pass is a pass against a metric
that was made harder than it needed to be. A correction here would raise the
selected candidate's estimate, after that estimate was seen, which is not a
change a reader should have to take on trust.

The **secondary** V is treated differently and deliberately: there, a zero
feature contributes zero to the numerator while the window stays in the
denominator, which is also the conservative direction (see section 3). The two
treatments differ because a miss and a zero answer different questions — "was
the side right" has no answer when no side was taken, while "what was earned"
has the answer zero. Both were settled before the registration commit.

#### The MDE table

| Candidate | Economic threshold | MDE, plan (b) — **primary** | Ratio | MDE, plan (c) — sensitivity |
|---|---:|---|---|---|
| A — OFI, directional | 0.5516 <!--gen:threshold_A--> | 0.003602 <!--gen:mde_b_A--> | 0.0065 <!--gen:ratio_A--> | 0.035184 <!--gen:mde_c_A--> |
| B — OFI, out-of-sample R² | 0.0261 <!--gen:threshold_B--> | 0.001099 <!--gen:mde_b_B--> | 0.0421 <!--gen:ratio_B--> | 0.004447 <!--gen:mde_c_B--> |
| C — queue imbalance, directional | 0.5516 <!--gen:threshold_C--> | 0.003115 <!--gen:mde_b_C--> | 0.0056 <!--gen:ratio_C--> | 0.048986 <!--gen:mde_c_C--> |

Computed 2026-09-24 by `research/gated.py` over 7 <!--gen:sessions--> development
sessions: 3,603,271 <!--gen:windows--> windows, 2,203,916 <!--gen:windows_moved-->
with a nonzero move, 96 <!--gen:symbols--> symbols. *m* was computed before this
table, as section 4 requires, and the thresholds are those of the correction
above. Every figure quoted here is written by `research/gated.py` to
`docs/generated/gated_values.tsv`; `tools/check_numbers.py` fails in CI if this
document and that file disagree.

The **Ratio** column is the selection rule, and the smallest value in it
selects the hypothesis. Plan (c)'s column is reported for the sensitivity
analysis and takes no part in the selection.

**The registered ratio is not scale-consistent, and this is recorded rather
than quietly repaired.** The MDE is a detectable **difference** from the null.
The denominator is the threshold, which for A and C is a **level** (0.5316)
and for B is already a difference, because that metric's null is zero. A and C
are therefore divided by about 0.53 and B by about 0.0098, which flatters the
directional candidates by a factor of roughly seventeen for a reason that has
nothing to do with what they can resolve. It is the same class of error as the
candidate-B summand above: two quantities compared without checking they are
in the same units.

The scale-consistent denominator is the threshold's **excess over its own
null**, the effect that actually has to be detected. Both are reported:

| Candidate | Registered ratio, MDE / threshold | Consistent ratio, MDE / (threshold − null) |
|---|---:|---:|
| A — OFI, directional | 0.0065 <!--gen:ratio_A--> | 0.0698 <!--gen:ratio_units_A--> |
| B — OFI, out-of-sample R² | 0.0421 <!--gen:ratio_B--> | **0.0421** <!--gen:ratio_units_B--> |
| C — queue imbalance, directional | **0.0056** <!--gen:ratio_C--> | 0.0603 <!--gen:ratio_units_C--> |

**They no longer agree.** Before *m* was corrected, both forms selected C and
the defect was harmless. With the corrected *m* the R² bar rises by a factor
of 2.7 while the accuracy bar rises by 2 percentage points, and under correct
units **candidate B now has the smallest ratio**. The registered rule selects
C; the scale-consistent rule would select B.

This is recorded as prominently as it can be, because it is the single most
consequential thing the review has to weigh. The registered rule is **not**
amended: the computation has run, so changing the selection rule now would be
choosing a hypothesis with its inputs in view, which is the failure this whole
document exists to prevent. C is what the registered rule returns and C is
what stands — but a reader is entitled to know that the rule that returned it
compares a difference against a level, and that a rule without that defect
returns something else.

The registered rule is what selected, because it is what was registered. It is
**not** amended here: the computation has now been run, so an amendment to the
selection rule at this point would be a rule changed with its inputs in view,
which is the thing this document exists to prevent. It is recorded for the
review, and for any study that reuses this design.

| | Value |
|---|---|
| Development estimate of the chosen metric (C, directional accuracy) | 0.62409 <!--gen:dev_estimate_C--> |
| Block length *L*, Politis–White stationary, max across sessions | 177.58 <!--gen:selected_block_length--> windows |
| Blocks across the projected held-out set | 2661.6 <!--gen:effective_blocks--> (guard requires ≥ 20) |
| Intraday block-bootstrap SE, block length *L*, development | 0.000515 <!--gen:se_dev_b_C--> |
| Symbol-clustered SE, development (plan (c), 96 symbol clusters) | 0.012619 |
| MDE at the projected held-out size, plan (b) | 0.003115 <!--gen:mde_b_C--> |
| Projected held-out windows, primary (per-session minimum × 2) | 472,652 <!--gen:heldout_windows_primary--> |
| Projected held-out windows, sensitivity (mean × 2) | 629,690 <!--gen:heldout_windows_sensitivity--> |
| Guard | passes <!--gen:guard--> |
| Tie-break needed | no <!--gen:tie_break_needed--> |
| Selected hypothesis | **C — queue imbalance, directional** |
| Smallest economically meaningful effect after costs | 0.10 half-spreads per window = $0.0005/share |

**If the selected hypothesis's MDE under plan (b) exceeds its economic
threshold, the study as designed cannot answer its own question**, and the
pre-committed response is option (d): report the point estimate and call the
study exploratory. That is a decision rule, not a judgement to be made when
the number appears.

**The fallback inherits the selection rule's units defect, and as written it
cannot fire for A or C.** It compares the MDE — a detectable **difference**
from the null — against the threshold as a **level**. For the directional
candidates that level is about 0.55, so the rule asks whether the study can
resolve an effect of more than half the metric's range. No plausible MDE
exceeds that, so for A and C the registered fallback is not a test that could
have failed. For B the null is zero, so its threshold is already a difference
and its fallback is meaningful.

Both forms, for the selected candidate:

| Form | Comparison | Ratio | Fires? |
|---|---|---:|---|
| As registered | MDE 0.003115 against threshold 0.5516 | 0.0056 <!--gen:ratio_C--> | not triggered <!--gen:fallback_registered--> |
| Correct units | MDE 0.003115 against required effect 0.051623 | 0.0603 <!--gen:fallback_ratio_correct_units--> | not triggered <!--gen:fallback_correct_units--> |

**Both agree here**: the study is well inside its power requirement either
way, by a factor of about seventeen under the stricter reading. The defect is
recorded because a rule that cannot fail is not a safeguard, and a later
reader should not mistake "the fallback did not trigger" for evidence that it
could have.

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
- **Rule 4 is OFF in the primary specification.** Rule 4 fills a resting order
  when a non-displayed print occurs at exactly its price. That is a *modelling
  choice and not an inference*: `P` carries no usable side, and
  midpoint-pegged prints trade between ticks, so a print at the order's price
  does not establish that displayed interest there was exhausted. Turning it
  on can only add fills, so it can only flatter the strategy, and a
  specification that flatters itself by an assumption it cannot check is the
  wrong primary. **Rule 4 on is reported alongside as a labelled sensitivity**
  and carries the correction in section 3 like any other secondary.
  Both arms are run for every result; neither is chosen after seeing them.
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
- **Any of the following is not true of a held-out session before a single
  feature is computed from it.** All four are recorded in `docs/data.md` at
  download time and checked by `research/run_heldout.sh`, which refuses to
  proceed if any fails:
  1. Its **SHA-256 is recorded at download**, for the compressed file and for
     the unpacked session, computed twice by independent implementations
     (`shasum` and `census --sha256`).
  2. The **integrity checks pass**: the compressed length equals the length
     the server advertised, and `tools/verify_archive.sh` exits zero — no
     truncated stream, no appended bytes, digest matching if one is known.
  3. The **census is clean and the session is complete**: no unknown type, no
     length mismatch, no trailing bytes, and the final message is System
     Event `'C'`. Without that last check a truncated download is a
     well-formed prefix of a valid session (`docs/design.md` record 032).
  4. The **differential replay reports `RESULT: identical`**, with zero
     unexplained crossed or locked observations under record 036's gate.

  A session failing any of these is not analysed and is not quietly replaced;
  the failure is reported. Computing a feature first and checking afterwards
  would mean the decision to re-download had been made with the result already
  visible.
- Book reconstruction on a held-out session fails any other correctness layer
  in `docs/correctness.md`, or a determinism hash changes without explanation.
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

## 10. The sequence, in order, with nothing reorderable

A pre-registration is only worth what an outside reader can check about
**when** it was fixed. Every step below leaves a public artifact with a
timestamp that is not under the author's control, and each must complete
before the next begins.

| # | Step | Artifact a reader can check |
|---|---|---|
| 0 | **Gated computation commit.** The outputs of `research/gated.py`, together with a `tools/manifest.sh` manifest of `data/` and `results/` listing the inputs it read. | the manifest file, and the digests in it |
| 1 | **Registration commit.** This document, complete: hypothesis, features, strategy, costs, decision rules, MDE table, selected hypothesis, block length. **Carries a manifest showing no held-out session is present.** | commit hash and author date; the manifest's held-out line |
| 2 | **Push.** | GitHub's own receipt of the push, which the author cannot backdate |
| 3 | **CI green.** All jobs, both compilers, both platforms. | workflow run id, conclusion and time |
| 4 | **`heldout.lock` commit.** Records the registration commit hash, the CI run id that went green, and the SHA-256 of this document at that commit. **Carries a manifest showing no held-out session is present.** | commit hash; a digest that changes if the registration is edited afterwards; the manifest's held-out line |
| 5 | **Push.** | second push receipt |
| 6 | **Only then, download the held-out sessions.** | `docs/data.md` download timestamps and digests, all later than step 5 |

**Why the lock is a separate commit after CI rather than part of step 1.** A
lock written in the same commit as the registration proves nothing about the
registration: both are written at once by the same hand. Written afterwards,
against a pushed commit and a completed CI run, it pins a document that was
already public. And because it carries the registration's SHA-256, any later
edit to this file is detectable by anyone who recomputes it — including edits
that would otherwise look like tidying.

**What would invalidate the study, and is checkable.** If any held-out
session's download timestamp in `docs/data.md` precedes the push at step 5, or
if this document's digest at the registration commit does not match the one in
`heldout.lock`, the study is not confirmatory and must be reported as
exploratory regardless of its result. `research/run_heldout.sh` checks both
before it runs anything and refuses if either fails.

**Nothing between steps 1 and 6 may touch held-out data**, including reading
its size, its per-type census or its first message. The sessions are not
downloaded at all until step 6, which is the only version of this rule that
does not depend on the author's restraint.

**Why the manifests are part of the sequence.** `data/` and `results/` are
gitignored, so the repository records nothing about what was on disk at any
commit. Without a manifest, "no held-out session had been downloaded" and "no
development feature file existed yet" are claims about the author that a
reader has to accept. `tools/manifest.sh` writes the path, byte count and
SHA-256 of every file under both directories to a tracked file, which carries
no market data and so may be committed. A manifest cannot prove a file was
never downloaded and deleted — nothing in a repository can — but it fixes the
positive claim at each of the points above, and it makes the negative claim
checkable at every point where a manifest exists rather than at none.

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
