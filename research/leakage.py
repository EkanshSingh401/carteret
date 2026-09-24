#!/usr/bin/env python3
"""Leakage audit for the selected hypothesis. Development sessions only.

A development directional accuracy of 0.62 is high enough to be worth
disbelieving before it is believed. Queue imbalance predicting the sign of the
next mid-price move is a documented effect -- Gould and Bonart, *Queue
Imbalance as a One-Tick-Ahead Price Predictor in a Limit Order Book*, Market
Microstructure and Liquidity 2(1), 2016 -- so a large estimate is expected
rather than surprising. That is a reason to check the construction, not a
reason to skip the check: a genuine effect and a leak both produce a large
number, and only one of them survives the tests below.

Three checks, each of which a leak fails and a real effect passes:

  1  ORDERING. Every message the feature could have seen is strictly earlier
     than the first message of the interval the label measures. Read from the
     exporter's own audit stream, so it tests the code that made the features
     rather than a description of it.

  2  ONE-MESSAGE LAG. The features are recomputed from the book one message
     earlier, with the label and its baseline untouched. A real effect decays
     gently. An effect that collapses was reading the move it claims to
     predict.

  3  LABEL PERMUTATION. Labels are shuffled WITHIN each session, which
     destroys the pairing while preserving every marginal distribution. Any
     accuracy materially away from 0.5 is a defect in the metric itself.

    usage: research/leakage.py --features F [F ...] --lag1 F [--audit F] [...]
"""

from __future__ import annotations

import argparse
import pathlib
import sys

import numpy as np
import pandas as pd

FEATURES = {"queue_imbalance": "C", "ofi": "A"}
SAMPLE = 10_000
SEED = 20191030


def refuse_heldout(paths) -> None:
    for p in paths:
        n = pathlib.Path(p).stem.lower()
        if "heldout" in n or "held_out" in n or "10302019" in n or "01302020" in n:
            raise SystemExit(f"refusing to run: {p} looks like a held-out session")


def accuracy(df: pd.DataFrame, feature: str) -> tuple[float, int]:
    d = df[df["label_ticks"] != 0]
    hit = (np.sign(d[feature]) == np.sign(d["label_ticks"])).to_numpy(dtype=float)
    return float(hit.mean()), len(hit)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--features", nargs="+", required=True, type=pathlib.Path)
    ap.add_argument("--lag1", nargs="+", required=True, type=pathlib.Path)
    ap.add_argument("--audit", nargs="+", default=[], type=pathlib.Path)
    args = ap.parse_args()
    refuse_heldout(args.features + args.lag1 + list(args.audit))

    print("=" * 74)
    print("LEAKAGE AUDIT -- development sessions only")
    print("=" * 74)

    # ---- 1. ordering -----------------------------------------------------
    print()
    print("1. ORDERING: feature's last message strictly before the label's first")
    print("-" * 74)
    total = viol = 0
    rng = np.random.default_rng(SEED)
    for p in args.audit:
        a = pd.read_csv(p)
        n = len(a)
        idx = rng.choice(n, size=min(SAMPLE, n), replace=False)
        s = a.iloc[idx]
        bad = int((s["feat_last_msg"] >= s["label_first_msg"]).sum())
        bad_order = int((s["label_first_msg"] > s["label_last_msg"]).sum())
        total += len(s)
        viol += bad + bad_order
        print(f"  {p.stem:<32} sampled {len(s):>7,}  of {n:>9,}   "
              f"violations {bad + bad_order}")
    print(f"  TOTAL sampled {total:,}   VIOLATIONS {viol}   "
          f"{'PASS' if viol == 0 else 'FAIL'}")

    # ---- 2. one-message lag ----------------------------------------------
    print()
    print("2. ONE-MESSAGE LAG: features from the book one message earlier")
    print("-" * 74)
    base = pd.concat([pd.read_csv(p).assign(session=p.stem) for p in args.features],
                     ignore_index=True)
    lag = pd.concat([pd.read_csv(p).assign(session=p.stem) for p in args.lag1],
                    ignore_index=True)
    print(f"  {'feature':<20}{'registered':>12}{'lag 1 msg':>12}{'change':>10}"
          f"{'excess kept':>13}")
    for feat, cand in FEATURES.items():
        a0, n0 = accuracy(base, feat)
        a1, n1 = accuracy(lag, feat)
        kept = (a1 - 0.5) / (a0 - 0.5) if a0 != 0.5 else float("nan")
        print(f"  {feat:<20}{a0:>12.5f}{a1:>12.5f}{a1 - a0:>10.5f}{kept:>12.1%}"
              f"   ({cand})")
    print("  A collapse toward 0.5 would indicate the feature was reading the")
    print("  move it claims to predict. Gentle decay is what a real effect does.")

    # ---- 3. label permutation --------------------------------------------
    print()
    print("3. LABEL PERMUTATION within session: accuracy must be about 0.5")
    print("-" * 74)
    d = base[base["label_ticks"] != 0].copy()
    perm = np.empty(len(d), dtype=float)
    rng2 = np.random.default_rng(SEED)
    lab = d["label_ticks"].to_numpy(dtype=float)
    sess = d["session"].to_numpy()
    for s in np.unique(sess):
        m = sess == s
        v = lab[m].copy()
        rng2.shuffle(v)
        perm[m] = v
    # The null of this test is NOT 0.5 in general, and assuming it were would
    # turn an arithmetic certainty into a false alarm. Under random pairing the
    # expected accuracy is P(f>0)P(l>0) + P(f<0)P(l<0), which equals 0.5 only
    # when a marginal is balanced AND the feature is never exactly zero.
    # np.sign(0) is 0, and 0 never equals the sign of a nonzero label, so a
    # window whose feature is exactly zero is scored as a MISS.
    lsign = np.sign(lab)
    p_lp, p_lm = float((lsign > 0).mean()), float((lsign < 0).mean())
    print(f"  {'feature':<18}{'observed':>10}{'permuted':>10}{'null':>10}"
          f"{'P(f=0)':>9}{'gap':>9}")
    for feat, cand in FEATURES.items():
        fs = np.sign(d[feat].to_numpy(dtype=float))
        obs = float((fs == lsign).mean())
        sh = float((fs == np.sign(perm)).mean())
        null = float((fs > 0).mean()) * p_lp + float((fs < 0).mean()) * p_lm
        pz = float((fs == 0).mean())
        print(f"  {feat:<18}{obs:>10.5f}{sh:>10.5f}{null:>10.5f}{pz:>9.5f}"
              f"{sh - null:>9.5f}   ({cand})")
    print(f"  labels are near balanced: P(+) {p_lp:.5f}, P(-) {p_lm:.5f}")
    print("  The permutation preserves every marginal and destroys only the")
    print("  pairing. The test is whether 'permuted' matches 'null', not 0.5.")
    print()
    print("  NOTE, and it is a defect in the metric rather than a leak: a")
    print("  feature of exactly zero is counted as a wrong answer. Tied queues")
    print("  are common, so this biases queue imbalance DOWNWARD -- its")
    print("  accuracy is measured against a ceiling of 1 - P(f=0), not 1.")
    print()
    print("Reference: Gould & Bonart, Market Microstructure and Liquidity 2(1), 2016.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
