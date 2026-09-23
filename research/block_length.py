#!/usr/bin/env python3
"""Block length for the intraday block bootstrap, by the pre-registered rule.

docs/preregistration.md fixes the rule before the autocorrelation function was
looked at: the smallest lag at which the autocorrelation of the primary metric
stays within +/-0.05 for 30 consecutive lags, rounded up to the next whole
minute. The rule is mechanical and has no free parameter left for a later
choice to enter through.

The series the rule is applied to is the PRIMARY METRIC per unit of time, not
the raw feature. For a directional metric that is the per-bin mean of the
indicator that the signal's sign matched the label's sign; a bootstrap
resamples blocks of that series, so its dependence is what the block length
has to span.

    usage: research/block_length.py <features.csv> [--metric directional]
           [--bin-seconds 1] [--max-lag 3600]
"""

from __future__ import annotations

import argparse
import pathlib
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402
import pandas as pd  # noqa: E402

INK = "#1b1b1b"
TOL = 0.05
RUN = 30


def metric_series(df: pd.DataFrame, metric: str, bin_seconds: int) -> pd.Series:
    """The primary metric aggregated into equal time bins of exchange time."""
    d = df[df["label_ticks"] != 0].copy()
    if metric == "directional":
        feat = d["ofi"]
    elif metric == "queue":
        feat = d["queue_imbalance"]
    else:
        raise SystemExit(f"unknown metric {metric}")
    d["hit"] = (np.sign(feat) == np.sign(d["label_ticks"])).astype(float)
    ns = bin_seconds * 1_000_000_000
    d["bin"] = (d["window_end_ts"] // ns).astype("int64")
    # Mean over every symbol in the bin: the bootstrap resamples time, so a
    # block carries all symbols in it and cross-symbol dependence within a
    # block is not something the block length has to span.
    return d.groupby("bin")["hit"].mean()


def autocorr(x: np.ndarray, max_lag: int) -> np.ndarray:
    x = x - x.mean()
    denom = float(np.dot(x, x))
    if denom == 0:
        raise SystemExit("series has no variance")
    n = len(x)
    max_lag = min(max_lag, n - RUN - 1)
    return np.array([float(np.dot(x[: n - k], x[k:])) / denom for k in range(1, max_lag + 1)])


def rule(ac: np.ndarray, bin_seconds: int) -> tuple[int, int]:
    """Smallest lag with |rho| <= TOL for RUN consecutive lags, in bins.

    Returns the lag in bins and the rounded-up whole minutes.
    """
    for start in range(len(ac) - RUN + 1):
        if np.all(np.abs(ac[start : start + RUN]) <= TOL):
            lag_bins = start + 1  # ac[0] is lag 1
            seconds = lag_bins * bin_seconds
            minutes = int(np.ceil(seconds / 60.0))
            return lag_bins, max(minutes, 1)
    raise SystemExit(
        f"no lag satisfies |rho| <= {TOL} for {RUN} consecutive lags within the "
        f"range examined. The rule returns nothing, which is a finding about the "
        f"data and is reported as one rather than worked around."
    )


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("features", type=pathlib.Path)
    ap.add_argument("--metric", default="directional", choices=["directional", "queue"])
    ap.add_argument("--bin-seconds", type=int, default=1)
    ap.add_argument("--max-lag", type=int, default=3600)
    ap.add_argument("--out", type=pathlib.Path, default=pathlib.Path("docs/figures"))
    args = ap.parse_args()

    name = args.features.stem.lower()
    if "heldout" in name or "held_out" in name:
        raise SystemExit(f"refusing to run: {args.features} looks like a held-out session")

    df = pd.read_csv(args.features)
    s = metric_series(df, args.metric, args.bin_seconds)
    s = s.sort_index()
    # Fill gaps in exchange time so lags are uniform: a missing bin is a bin
    # with no qualifying window, and treating it as absent would compress the
    # lag axis and understate the block length.
    full = np.arange(s.index.min(), s.index.max() + 1)
    s = s.reindex(full)
    filled = int(s.isna().sum())
    s = s.interpolate(limit_direction="both")

    ac = autocorr(s.to_numpy(), args.max_lag)
    lag_bins, minutes = rule(ac, args.bin_seconds)

    print(f"session            {args.features.name}")
    print(f"metric             {args.metric}")
    print(f"bins               {len(s):,} of {args.bin_seconds} s ({filled:,} interpolated)")
    print(f"lags examined      {len(ac):,}")
    print(f"rule               |rho| <= {TOL} for {RUN} consecutive lags")
    print(f"first such lag     {lag_bins} bins = {lag_bins * args.bin_seconds} s")
    print(f"BLOCK LENGTH       {minutes} minute(s), rounded up")
    print(f"rho at that lag    {ac[lag_bins - 1]:+.4f}")
    print(f"max |rho| in run   {np.max(np.abs(ac[lag_bins - 1 : lag_bins - 1 + RUN])):.4f}")

    args.out.mkdir(parents=True, exist_ok=True)
    fig, ax = plt.subplots(figsize=(7.2, 3.2))
    lags = np.arange(1, len(ac) + 1) * args.bin_seconds
    ax.axhspan(-TOL, TOL, color="#dddddd", zorder=0, label=f"±{TOL}")
    ax.plot(lags, ac, color=INK, lw=0.9)
    ax.axvline(lag_bins * args.bin_seconds, color="#b8500e", lw=1.2,
               label=f"rule: {minutes} min")
    ax.set_xlabel("lag (seconds of exchange time)")
    ax.set_ylabel("autocorrelation")
    ax.set_title(f"Primary metric autocorrelation — {args.features.stem}", fontweight="bold")
    ax.set_xscale("log")
    ax.legend(frameon=False, fontsize=8)
    ax.grid(alpha=0.3, lw=0.5)
    out = args.out / f"block_length_{args.features.stem}_{args.metric}.png"
    fig.savefig(out, dpi=140, bbox_inches="tight")
    plt.close(fig)
    print(f"wrote              {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
