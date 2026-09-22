#!/usr/bin/env python3
"""Runs the registered study against a session and reports per the decision rules.

Invoked by ``research/run_heldout.sh``, which refuses to call it until the
registration is locked. It can also be pointed at a development session with
``--development``, which is how the analysis is checked before registration.

It does not decide anything by itself: the thresholds live in
``docs/preregistration.md`` section 8 and are passed in, so this script cannot
quietly become the place where the bar is set.

    usage: research/signal_study.py --heldout <session-file> --out DIR
           research/signal_study.py --development <session-file> --out DIR
"""

from __future__ import annotations

import argparse
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import study  # noqa: E402


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    group = ap.add_mutually_exclusive_group(required=True)
    group.add_argument("--heldout", type=pathlib.Path)
    group.add_argument("--development", type=pathlib.Path)
    ap.add_argument("--out", type=pathlib.Path, required=True)
    ap.add_argument("--metric", default="directional_accuracy")
    ap.add_argument("--null", type=float, default=0.5)
    ap.add_argument("--primary", default=None,
                    help="the registered primary feature; omitted before registration")
    ap.add_argument("--threshold", type=float, default=None,
                    help="the registered section 8 threshold")
    ap.add_argument("--window", type=int, default=50)
    ap.add_argument("--symbols", type=int, default=50)
    ap.add_argument("--bootstrap", type=int, default=10000)
    args = ap.parse_args()

    session = args.heldout or args.development
    held_out = args.heldout is not None
    args.out.mkdir(parents=True, exist_ok=True)

    features_csv = args.out / f"features_{session.name}.csv"
    study.export_features(session, features_csv, window=args.window, symbols=args.symbols)
    df = study.load([features_csv])

    print(f"session   {session}")
    print(f"mode      {'HELD OUT' if held_out else 'development'}")
    print(f"rows      {len(df):,}   symbols {df['symbol'].nunique():,}")
    print(f"metric    {args.metric}   null {args.null}")
    print()

    # A single session gives one session cluster, so the block bootstrap over
    # sessions is undefined. Clustering falls back to symbol within the
    # session, which is weaker, and the report says so rather than printing a
    # standard error that looks stronger than it is.
    single = df["session"].nunique() < 2
    if single:
        print("NOTE: one session. The session block bootstrap is undefined, so the")
        print("  interval below rests on symbol clustering within the session alone.")
        print("  Combine sessions before quoting an interval as the study's result.")
        print()

    results = {}
    for feature in study.FEATURES:
        est = study.estimate(df, feature, metric=args.metric,
                             cluster_on="session_symbol", bootstrap=args.bootstrap)
        results[feature] = est
        marker = "  <- primary" if feature == args.primary else ""
        print(study.describe(feature, est, args.null) + marker)
        print()

    if args.primary is None:
        print("No primary feature named. Before registration that is correct: the")
        print("choice belongs in docs/preregistration.md section 2, not on a command")
        print("line. No verdict is reported.")
        return 0

    est = results[args.primary]
    if args.threshold is None:
        print("No registered threshold passed. No verdict is reported.")
        return 0

    holds = est.value > args.threshold and est.excludes(args.null)
    fails = est.ci_high < args.threshold
    verdict = "SIGNAL HOLDS" if holds else ("SIGNAL FAILS" if fails else "INCONCLUSIVE")

    print(f"primary   {args.primary}")
    print(f"threshold {args.threshold}")
    print(f"verdict   {verdict}")
    if verdict == "INCONCLUSIVE":
        print("  The interval contains both the null and the registered threshold.")
        print("  That is a result, and it is not the same as the effect being absent.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
