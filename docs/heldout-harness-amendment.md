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
