# Held-out harness amendment, 2026-09-24

**The registration text is unchanged.** `docs/preregistration.md` has SHA-256
`28f10db45fff7316df6acca3c2871a9805511c893e1f8d36c4ac28e7f265708a`, which is
the digest `research/heldout.lock` pins and the digest it had at the
registration commit `29228cc`. This document exists because that file must not
be edited: its digest is what makes the registration checkable. Nothing here
changes a rule. It records that the **code** which was supposed to run the
rules did not, and what was written to make it do so.

**No held-out feature or label has been computed.** The manifest committed
with this amendment, `docs/manifests/harness-amendment-2026-09-24.txt`, lists
every file under `data/` and `results/` with its digest. It reports **8
held-out paths present** and flags them, which is correct: both sessions were
fetched and verified after the lock, as authorised, and the flag records the
fact rather than hiding it. What matters is what is *absent* — the manifest
contains **no feature file derived from either session**, so no feature and no
label has been computed from held-out data. The rehearsal below used
development sessions only.

The held-out sessions, with the digests recorded at download:

| Session | `.gz` bytes | SHA-256 (unpacked session) |
|---|---:|---|
| `10302019.NASDAQ_ITCH50` | 3,872,931,242 | `55d4620a0cbb1926dbba97ff560a0979cd879dbe9087fcb8a62ebbf993578ab4` |
| `01302020.NASDAQ_ITCH50` | 5,597,158,940 | `a324b2cc6daa0992e411d021b1f714cbc614dc5d1d3f2828b19b79d3f9fbe75b` |

Both passed all five arrival checks and the differential replay reported
`RESULT: identical`, before any feature was computed, as section 9 requires.

## Why an amendment was needed

The held-out path was read before it was run. Four defects, none of which
would have announced themselves:

**1. The verdict was never computed.** `research/run_heldout.sh` invoked
`research/signal_study.py` with `--heldout` and `--out` and nothing else.
That script takes `--primary` and `--threshold`, and **nothing in the
repository passed either**. With no primary named it printed "No verdict is
reported" and returned **0**. The run would have exited successfully, produced
files, and never tested candidate C against its bar.

**2. The wrong estimator.** Even with those arguments, `study.estimate()`
resamples **whole sessions**. With two held-out sessions that is two clusters
— the degenerate estimator section 4 considered and rejected when it chose
plan (b). The registered interval is the stationary bootstrap **within**
sessions at the Politis–White block length.

**3. The wrong unit of analysis.** `signal_study.py` ran one session at a
time and, for a single session, fell back to symbol clustering — plan (c),
which section 8 designates sensitivity-only and anti-conservative, and which
"decides nothing".

**4. Four registered outputs were absent entirely**: Holm–Bonferroni over the
family of five, the direct-value secondary, the strategy P&L at both tiers,
and the A/B exploratory reporting. `study.FEATURES` also still carried
`micro_dev_ticks`, which section 5 drops for a sign-based primary because
normalised it is algebraically identical to queue imbalance.

## Rule to code

Every constant is **read**, never estimated. The block length, thresholds, *m*
and the development estimates come from `docs/generated/gated_values.tsv`,
written by `research/gated.py` from development sessions before the
registration commit. The value requirement, family size, fee tiers and rule-4
setting come from the registration text.

| Registered rule | Where | Implemented in |
|---|---|---|
| Primary C, directional, vs 55.16% | §2, §8 | `heldout_study.py` PRIMARY block; bar read as `threshold_C` |
| Plan (b): stationary bootstrap within sessions at *L* | §4 step 2b | `gated.stationary_bootstrap_se`, **imported, not copied** |
| *L* = 177.58 | §4 step 2 | read as `selected_block_length` |
| Symbol clustering is sensitivity only, decides nothing | §8 | `gated.clustered_se_influence`, printed with its label |
| Holm–Bonferroni, family of five | §3 | `holm()`, `HOLM_FAMILY = 5` |
| Direct value over all windows, sign(0) contributes zero, vs 0.10 | §3 | `study.direct_value`, shared with the development path |
| Micro-price deviation dropped | §5 | `SECONDARY_FEATURES` excludes it |
| A and B exploratory, outside the Holm family | §2 | EXPLORATORY block, printed with its label |
| Two-session scope sentence | §4, §8 | `SCOPE`, printed with the verdict |
| Fee tiers, base and top | §7 | `FEES` |
| Rule 4 OFF in the primary | §6 | `RULE4_PRIMARY = False` |
| Missing constant exits nonzero | this amendment | `Missing` exception, exit 3 |

**One value was computed rather than read, and it is development data.**
Section 4 fixes the block-length *procedure* per series — Politis–White on the
per-window summand, per session, maximum taken — but `gated.py` had only ever
applied it to A, B and C. The secondary series `trade_sign` and the
direct-value series had no block length. Applying the registered procedure to
them is following the rule, not choosing a value, and it was done on the seven
**development** sessions, which is where the rule says block lengths come
from. The results are `block_trade_sign` 1612.00 and `block_direct_value`
3.60, now in the generated file. Re-running `gated.py` reproduced every
previously published value **byte for byte**; only those two keys were added.

## What is still blocked, and why nothing was chosen

`docs/preregistration.md` section 8 ends:

> Signal threshold *(to be filled)*. Minimum fills per session *(to be filled)*.

Both are **registered constants that were never filled in**.

- The **signal threshold** is what section 6 uses to decide when to post a
  quote: "when the signal's magnitude exceeds a threshold fixed in section 8".
  Without it the maker strategy cannot be simulated, so there is no P&L, no
  base-tier figure and no top-tier sensitivity.
- **Minimum fills per session** is the *N* in section 9's study-failure
  criterion, "fewer than *(N)* simulated fills per held-out session". Without
  *N* that criterion cannot be evaluated.

The harness **refuses** rather than picking values: `heldout_study.py` exits
**3** with the reason. Choosing either here, with held-out sessions already on
disk, would be setting a registered parameter after the data arrived — the
precise failure the registration exists to prevent.

Filling them has a consequence worth stating plainly: it edits
`docs/preregistration.md`, which changes its SHA-256, which invalidates the
current lock. That requires a new registration commit and a new lock. There is
no way to supply a missing registered constant without doing so.

## Rehearsal, on development sessions only

The amended harness was run end to end over the seven development sessions as
if they were held out. Output in `docs/generated/heldout_rehearsal.txt`.

- Every registered output is produced: primary with its interval and verdict,
  the symbol-clustered sensitivity with its label, the Holm table, the
  direct-value test, the A/B exploratory block, and the scope sentence.
- **The plan-(b) interval for C matches `gated.py` on the same data**:
  accuracy `0.62409` against `dev_estimate_C`, SE `0.000515` against
  `se_dev_b_C`. A and B match `dev_estimate_A` `0.56219` and `dev_estimate_B`
  `0.00551`. The shared bootstrap function is why.
- Negative tests, all of which fail as they should:

| Test | Expected | Result |
|---|---|---|
| Omit `--primary` | nonzero | exit 2, "the following arguments are required: --primary" |
| Strategy without section 8's constants | nonzero | exit 3, names both missing constants |
| Held-out invocation with a corrupted lock commit | nonzero | exit 1, "not a commit in this repository" |

The rehearsal is a test of the harness. It is **not** a result: it reports
development numbers, which were already visible, and it is labelled
`DEVELOPMENT` in its own output.

---

# Second amendment, 2026-09-24: the strategy is exploratory

The registration is **still not edited**. Its digest is unchanged at
`28f10db45fff7316df6acca3c2871a9805511c893e1f8d36c4ac28e7f265708a`. The two
placeholders stay where they are, and this section says what they cost.

## The two placeholders, and why they cannot be filled now

`docs/preregistration.md` section 8 ends:

> Signal threshold *(to be filled)*. Minimum fills per session *(to be filled)*.

Filling either now would set a registered parameter **after both held-out
sessions are on disk**. The registration's entire claim is that its
parameters were fixed before the data was reachable; a constant supplied at
this point has no such claim, whatever value is chosen and however reasonable
it looks. Editing the file would also change its digest and break the lock —
which is the lock working, not a defect in it.

So they are not filled. The component that depends on them is demoted
instead.

<!-- placeholder-allowlist -->

| Placeholder | Component it demotes |
|---|---|
| `Signal threshold *(to be filled)*` | Strategy P&L — exploratory only |
| `Minimum fills per session *(to be filled)*` | Section 9 minimum-fills criterion — not evaluated; fill counts reported instead |
| `Thresholds marked *(to be filled)* are set` | Nothing — prose describing the convention in section 8's preamble, not an unfilled constant |

<!-- /placeholder-allowlist -->

`tools/check_placeholders.py` enforces this table in CI: a placeholder in the
registration that is not listed here fails the build, and a listed entry that
matches no placeholder fails too, so the allowlist cannot outlive what it
excuses.

## Confirmatory scope

These run under the registration **as written**. Each is listed with every
registered constant it reads, and **none of them reads an unfilled one**.

| Output | Registered constants it reads | Source |
|---|---|---|
| Primary C, directional accuracy, verdict vs bar | `threshold_C` 0.5516; `selected_block_length` 177.58; null 0.5 | generated file; §2, §8 |
| Primary 95% interval, plan (b) | `selected_block_length` 177.58 | generated file; §4 step 2b |
| Symbol-clustered sensitivity (decides nothing) | none beyond the data | §8 |
| Holm–Bonferroni over the family | family size 5 | §3 |
| Secondary `ofi` | `block_A` 299.58; null 0.5 | generated file |
| Secondary `trade_sign` | `block_trade_sign` 1612.00; null 0.5 | generated file |
| Direct-value secondary | requirement 0.10; `block_direct_value` 3.60 | §3; generated file |
| A and B, exploratory labels | `block_A`, `block_B`, `threshold_A`, `threshold_B` | generated file |
| Two-session scope sentence | none | §4, §8 |

**The section 9 minimum-fills criterion applies to the strategy component
alone.** It is one of several study-failure criteria; the others — digests at
download, integrity, a clean census, `RESULT: identical` — are all satisfied
and are recorded above. Nothing in the confirmatory scope depends on a
simulated fill, so an unevaluable minimum-fills rule does not gate the
confirmatory results. It gates only the P&L, which is already exploratory for
the same reason.

## Exploratory parameters, declared before any held-out feature was computed

| Parameter | Declared value | Why this and not another |
|---|---|---|
| Signal threshold | **0** | Section 6 posts "when the signal's magnitude exceeds a threshold". At zero the rule degenerates to the primary's own sign rule — quote on the side the signal favours, whenever it has a side. It is the **only value that requires no choice**: every positive value is a number someone picks, and picking one here would be doing with the strategy exactly what the registration forbids doing with the hypothesis. |
| Minimum fills per session | **no rule** | The criterion cannot be evaluated without *N*. Rather than invent a bar, the harness reports **fill counts per session** so a reader can apply whatever bar they think right. |

Everything else in section 6 is registered and is implemented as registered:
one round lot at the inside on the signal's side; fills from the queue
simulator under record 020 with **exact** market-by-order queue position;
rule 4 **off** in the primary arm and **on** as a labelled sensitivity;
inventory limit of one lot per symbol; cancellation when the window ends;
exit at the end of the following window at the mid; base and top tier from
section 7.

`src/strategy_pnl.cpp` drives `include/carteret/queue_sim.hpp` rather than
reimplementing the fill rules, because the registration says fills come from
that simulator and a second copy of the queue logic would not be it. The
simulator gained three additive entry points — directed placement, per-symbol
cancellation and a fill callback — plus a flag that disables timer placement.
**The existing behaviour is provably unchanged**: `queue_study` over
`20190130.BX_ITCH_50` produces byte-identical output before and after the
change, and `test_queue_sim` passes.

## Labelling

Every line the strategy prints begins with `EXPLORATORY`, in the C++ tool and
in the Python harness that calls it. A confirmatory run prints **no** strategy
output at all: the P&L requires `--strategy`, and `--strategy` without
`--exploratory` exits **3** naming the missing constants.

## No held-out feature or label has been computed

The manifest committed with this amendment reports both held-out sessions
present, as authorised, and **no feature file derived from either**. The
rehearsal below used development sessions only, and the strategy was rehearsed
on a development session.

---

# Third amendment, 2026-09-24: the runner pointed at the wrong script

The registration is **still not edited**; its digest remains
`28f10db45fff7316df6acca3c2871a9805511c893e1f8d36c4ac28e7f265708a`.

## What was wrong

The first amendment wrote `research/heldout_study.py`, rehearsed it, and
documented it. The second amendment added the strategy, the placeholder gate
and a second lock. **Neither rewired `research/run_heldout.sh`**, which went
on invoking `research/signal_study.py` — the script the first amendment
existed to replace.

So the two amendment sections above, and both lock commits, described a wiring
that did not exist. Running the study would have executed the defective path:
no `--primary`, no `--threshold`, "No verdict is reported", **exit 0**.

## How it was found, and why nothing caught it

It was found by reading `run_heldout.sh` line by line while preparing to run
it, after the frozen-code check failed for an unrelated reason and forced a
stop. Three things that ought to have caught it did not:

- **`--dry-run` could not.** It exited *before* the section that invokes the
  analysis. It validated the lock, printed the session list and stopped. A dry
  run that never reaches the command it would run cannot report that the
  command is wrong, and its passing was read as evidence the path was sound.
- **The rehearsal could not.** `heldout_study.py` was rehearsed by calling it
  **directly**. That tests the script and says nothing about whether anything
  invokes it. The entry point was never exercised.
- **CI could not.** No job runs the held-out entry point; it needs market data
  that is not in the repository and must not be.

The common shape: every check tested a *component*, and the defect was in the
*wiring between* components. A test that never executes the entry point cannot
detect that the entry point calls the wrong thing.

## The corrected checks

**1. The frozen-code check was wrongly defined**, and its failure is what
forced the stop that found the rest. `research/heldout.lock` lives under
`research/`, so `git diff <frozen-base> HEAD -- research/` can never be empty
once the lock is written: the lock commit necessarily follows the amendment it
points at. It is now two checks, both of which must pass:

```
git diff <frozen-base> HEAD -- research/ src/ include/ tools/ ':!research/heldout.lock'
git diff <lock-commit>  HEAD -- research/heldout.lock
```

The first is the frozen surface; the second says the lock has not moved since
it was written. Excluding the lock from its own check is not a loosening —
the lock is the *statement* of what is frozen, not part of the frozen surface.

**2. The runner invokes the amended harness**, once, over every session
together, because the registered inference pools them and resamples within
sessions.

**3. `signal_study.py` refuses held-out mode outright**, exit 3. The legacy
path cannot be reached by accident again.

**4. `--dry-run` is now meaningful.** It builds the command list first, prints
it, and checks it against the list recorded below. A runner wired to the wrong
script produces a different last line and **fails**. The real run additionally
refuses if any command mentions the legacy script.

**5. `--rehearse` executes the identical code path** on the seven development
sessions. Only the session list and the output directory differ.

## The commands the real run executes

Checked by `--dry-run` against this block. If the runner is rewired without
updating this list, or this list is edited without rewiring the runner, the
dry run fails.

<!-- expected-commands -->
    ./build/release/census data/10302019.NASDAQ_ITCH50
    ./build/release/determinism data/10302019.NASDAQ_ITCH50
    ./build/release/replay --market Q data/10302019.NASDAQ_ITCH50
    ./build/release/export_features --window 50 --symbols 50 --out results/heldout/features_10302019.NASDAQ_ITCH50.csv data/10302019.NASDAQ_ITCH50
    ./build/release/census data/01302020.NASDAQ_ITCH50
    ./build/release/determinism data/01302020.NASDAQ_ITCH50
    ./build/release/replay --market Q data/01302020.NASDAQ_ITCH50
    ./build/release/export_features --window 50 --symbols 50 --out results/heldout/features_01302020.NASDAQ_ITCH50.csv data/01302020.NASDAQ_ITCH50
    python3 research/heldout_study.py --sessions results/heldout/features_10302019.NASDAQ_ITCH50.csv results/heldout/features_01302020.NASDAQ_ITCH50.csv --out results/heldout --primary C --label heldout --strategy --exploratory --strategy-sessions data/10302019.NASDAQ_ITCH50 data/01302020.NASDAQ_ITCH50
<!-- /expected-commands -->

## Trace: every step, and which registered output it produces

| # | Step | Produces |
|---|---|---|
| 1 | `census` per session | clean framing, complete session — §9 failure criterion 3 |
| 2 | `determinism` per session | determinism hash — §9, `docs/correctness.md` layer 5 |
| 3 | `replay --market Q` per session | `RESULT: identical` and the crossed/locked gate — §9 criterion 4 |
| 4 | `export_features` per session | the windows, features and labels every metric reads |
| 5 | `heldout_study.py`, once, over both | everything below |

Within step 5:

| Registered output | Produced by |
|---|---|
| Primary C accuracy, plan-(b) interval, verdict vs 0.5516 | PRIMARY block; `stationary_bootstrap_se` shared with `gated.py` |
| Symbol-clustered sensitivity, labelled, deciding nothing | PRIMARY block, `clustered_se_influence` |
| Holm–Bonferroni over the family of five | SECONDARIES block, `holm()` |
| Direct-value secondary vs 0.10 | SECONDARIES block, `study.direct_value` |
| A and B, labelled exploratory | EXPLORATORY block |
| Strategy P&L, both arms, both tiers, fill counts | STRATEGY block → `./build/release/strategy_pnl`, twice per session |
| Two-session scope sentence | printed with the primary verdict |

`strategy_pnl` is the only binary invoked by step 5 rather than by the runner
directly; it is called with `--symbols 50 <session>` and again with `--rule4`.
