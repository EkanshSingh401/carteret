#!/usr/bin/env python3
"""Power analysis for the pre-registered study. Development sessions only.

Fills in section 4 of ``docs/preregistration.md``: the development estimate of
the primary metric, its standard error by two routes, and the minimum
detectable effect at the planned held-out size.

This must be run, and its numbers written into the registration, BEFORE the
held-out sessions are fetched. If the minimum detectable effect exceeds the
smallest economically meaningful effect, the study as designed cannot answer
its own question, and section 4 names the remedies.

    usage: research/power.py results/features/dev_*.csv --heldout-clusters 100
"""

from __future__ import annotations

import argparse
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import study  # noqa: E402


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("features", nargs="+", type=pathlib.Path,
                    help="development feature CSVs, one per session")
    ap.add_argument("--metric", default="directional_accuracy")
    ap.add_argument("--null", type=float, default=0.5,
                    help="null value of the metric; 0.5 for directional accuracy")
    ap.add_argument("--cluster-on", default="session_symbol",
                    choices=["session", "session_symbol"])
    ap.add_argument("--heldout-sessions", type=int, default=2)
    ap.add_argument("--heldout-symbols", type=int, default=50)
    ap.add_argument("--bootstrap", type=int, default=10000)
    args = ap.parse_args()

    for p in args.features:
        name = p.stem.lower()
        if "heldout" in name or "held_out" in name:
            raise SystemExit(f"refusing to run: {p} looks like a held-out session")

    df = study.load(args.features)
    sessions = sorted(df["session"].unique())
    print(f"development sessions ({len(sessions)}):")
    for s in sessions:
        print(f"  {s}")
    print(f"rows {len(df):,}   symbols {df['symbol'].nunique():,}")
    print(f"metric {args.metric}   null {args.null}   clustered on {args.cluster_on}")
    print()

    heldout_clusters = (args.heldout_sessions * args.heldout_symbols
                        if args.cluster_on == "session_symbol"
                        else args.heldout_sessions)

    if len(sessions) < 2:
        print("WARNING: fewer than two development sessions.")
        print("  The block bootstrap resamples whole sessions and is undefined here,")
        print("  and a session-clustered standard error from one session is not an")
        print("  estimate of anything. The power analysis is NOT complete until more")
        print("  development sessions are available; see docs/preregistration.md")
        print("  section 4 and docs/data.md.")
        print()

    print("| feature | estimate | SE clustered | SE bootstrap | 95% CI | MDE (held-out) |")
    print("|---|---:|---:|---:|---|---:|")
    for feature in study.FEATURES:
        est = study.estimate(df, feature, metric=args.metric,
                             cluster_on=args.cluster_on, bootstrap=args.bootstrap)
        mde = study.minimum_detectable_effect(
            max(est.se_clustered, est.se_bootstrap if est.se_bootstrap == est.se_bootstrap
                else est.se_clustered),
            est.clusters, heldout_clusters)
        print(f"| {feature} | {est.value:.5f} | {est.se_clustered:.5f} | "
              f"{est.se_bootstrap:.5f} | [{est.ci_low:.5f}, {est.ci_high:.5f}] | "
              f"{mde:.5f} |")

    print()
    print("Write the chosen primary feature's row into docs/preregistration.md")
    print("section 4, beside the smallest economically meaningful effect, and set")
    print("the section 8 thresholds from it. Then commit, and only then write the")
    print("commit hash into research/heldout.lock.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
