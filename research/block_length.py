#!/usr/bin/env python3
"""Block length for the stationary bootstrap, by Politis-White selection.

docs/preregistration.md section 4 fixes the procedure. This implements it and
nothing else: there is no bin, no tolerance and no run length to choose, which
is the point. An earlier autocorrelation-band rule was withdrawn because its
unfixed time bin moved the answer from 1 minute to 319 minutes on one session;
the amendment record keeps that evidence.

The series is the PER-WINDOW SUMMAND of the primary metric, in event order:

    A, C  the hit indicator 1{sign(feature) == sign(label)}
    B     the per-window squared-error term of the univariate fit

That is the series the bootstrap resamples, so its dependence is what a block
has to span. Aggregating into time bins first -- the previous rule's mistake --
replaces it with a different series whose dependence is an artifact of the bin.

Across development sessions the MAXIMUM is taken. Underestimating dependence
narrows intervals, so the conservative direction is the longer block.

    usage: research/block_length.py <features.csv> [...] [--metric hit|sqerr]
"""

from __future__ import annotations

import argparse
import pathlib
import sys

import numpy as np
import pandas as pd
from arch.bootstrap import optimal_block_length

MIN_BLOCKS = 20  # section 4's guard; below this the study is exploratory


def summand(df: pd.DataFrame, metric: str) -> np.ndarray:
    """The per-window summand, in event order."""
    d = df[df["label_ticks"] != 0].sort_values("window_end_ts")
    if metric == "hit":
        feat = d["ofi"]
        return (np.sign(feat) == np.sign(d["label_ticks"])).astype(float).to_numpy()
    if metric == "hit_queue":
        feat = d["queue_imbalance"]
        return (np.sign(feat) == np.sign(d["label_ticks"])).astype(float).to_numpy()
    if metric == "sqerr":
        # Univariate OLS of the label on the feature, in-session. The summand
        # is the squared-error REDUCTION against the baseline forecast, not
        # the squared error: R2 is a ratio, and the series the bootstrap
        # resamples is the numerator's summand. See the candidate-B amendment
        # in docs/preregistration.md section 4 (2026-09-24).
        x = d["ofi"].to_numpy()
        y = d["label_halfspreads"].to_numpy()
        beta = float(np.dot(x, y) / np.dot(x, x)) if np.dot(x, x) else 0.0
        baseline = float(y.mean())
        return (y - baseline) ** 2 - (y - beta * x) ** 2
    raise SystemExit(f"unknown metric {metric}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("features", nargs="+", type=pathlib.Path)
    ap.add_argument("--metric", default="hit", choices=["hit", "hit_queue", "sqerr"])
    ap.add_argument("--heldout-windows", type=int, default=0,
                    help="if given, apply the minimum-blocks guard against it")
    args = ap.parse_args()

    per_session = []
    for path in args.features:
        name = path.stem.lower()
        if "heldout" in name or "held_out" in name:
            raise SystemExit(f"refusing to run: {path} looks like a held-out session")
        df = pd.read_csv(path)
        x = summand(df, args.metric)
        opt = optimal_block_length(x)
        stationary = float(np.asarray(opt["stationary"])[0])
        circular = float(np.asarray(opt["circular"])[0])
        per_session.append((path.stem, len(x), stationary, circular))
        print(f"{path.stem:<24} n {len(x):>9,}  stationary {stationary:>9.2f}  "
              f"circular {circular:>9.2f}")

    chosen = max(t[2] for t in per_session)
    print()
    print(f"sessions                 {len(per_session)}")
    print(f"BLOCK LENGTH (stationary, max across sessions)  {chosen:.2f} windows")

    if args.heldout_windows:
        blocks = args.heldout_windows / chosen
        print(f"held-out windows         {args.heldout_windows:,}")
        print(f"effective blocks         {blocks:.1f}")
        if blocks < MIN_BLOCKS:
            print(f"GUARD FAILS: fewer than {MIN_BLOCKS} blocks. The study is "
                  f"declared EXPLORATORY before any held-out access.")
        else:
            print(f"guard passes ({MIN_BLOCKS} required)")

    print()
    print("pre-committed sensitivity, section 4:")
    for mult in (0.5, 1.0, 2.0):
        print(f"  {mult:>3}x  {chosen * mult:.2f} windows")
    return 0


if __name__ == "__main__":
    sys.exit(main())
