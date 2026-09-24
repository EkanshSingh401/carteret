#!/usr/bin/env python3
"""The gated computation, in the order docs/preregistration.md section 4 fixes.

Development sessions only. Nothing here opens a held-out session, and the
argument check below refuses a file whose name looks like one.

The order is not a convenience. *m* is computed BEFORE the MDE table, because
*m* sets the economic thresholds and a threshold chosen after seeing what the
study can resolve is not a threshold. The steps, in the order they run:

  1  m, window-weighted and pooled across all seven development sessions, and
     the accuracy and R-squared bars that follow from it
  2  Politis-White block length, per session, per candidate; maximum taken
  3  MDE under plan (b), the intraday stationary bootstrap -- primary
     MDE under plan (c), symbol clustering        -- sensitivity only
  4  the selection rule: smallest MDE-to-threshold ratio under plan (b)
  5  the minimum-blocks guard and the pre-committed exploratory fallback

    usage: research/gated.py results/features/dev_*.csv
"""

from __future__ import annotations

import argparse
import pathlib
import sys

import numpy as np
import pandas as pd
from arch.bootstrap import optimal_block_length

Z_ALPHA = 1.959963985   # two-sided 0.05
Z_POWER = 0.841621234   # 80%
MIN_BLOCKS = 20         # section 4, step 2c
REPLICATIONS = 10_000
SEED = 20191030
ONE_SESSION_M = 1.5543  # the provisional value, 12302019 only

# Candidate -> (feature column, summand kind). Section 3.
CANDIDATES = {
    "A - OFI, directional": ("ofi", "hit"),
    "B - OFI, out-of-sample R2": ("ofi", "sqerr"),
    "C - queue imbalance, directional": ("queue_imbalance", "hit"),
}
SIMPLER = "C - queue imbalance, directional"   # the registered tie-break


def refuse_heldout(paths: list[pathlib.Path]) -> None:
    for p in paths:
        n = p.stem.lower()
        if "heldout" in n or "held_out" in n or "10302019" in n or "01302020" in n:
            raise SystemExit(f"refusing to run: {p} looks like a held-out session")


def load(paths: list[pathlib.Path]) -> pd.DataFrame:
    frames = []
    for p in paths:
        d = pd.read_csv(p)
        d["session"] = p.stem
        frames.append(d)
    return pd.concat(frames, ignore_index=True)


def moved(df: pd.DataFrame) -> pd.DataFrame:
    """Windows with a nonzero move, in event order. The same conditioning the
    directional metric uses, so m is a scale of the same population."""
    return df[df["label_ticks"] != 0].sort_values(["session", "window_end_ts"])


def summand(d: pd.DataFrame, feature: str, kind: str) -> np.ndarray:
    if kind == "hit":
        return (np.sign(d[feature]) == np.sign(d["label_ticks"])).to_numpy(dtype=float)
    if kind == "sqerr":
        x = d[feature].to_numpy(dtype=float)
        y = d["label_halfspreads"].to_numpy(dtype=float)
        xx = float(np.dot(x, x))
        beta = float(np.dot(x, y) / xx) if xx else 0.0
        return (y - beta * x) ** 2
    raise SystemExit(f"unknown summand {kind}")


def stationary_bootstrap_se(values: np.ndarray, sessions: np.ndarray, block: float,
                            replications: int = REPLICATIONS, seed: int = SEED) -> float:
    """SE of the pooled mean under the stationary bootstrap, resampling WITHIN
    sessions only (section 4, step 2b): a block never crosses a session
    boundary, because an overnight gap is not a dependence structure to splice
    across.

    Each session is resampled to its own length and the pooled mean is the
    length-weighted combination, which is the statistic being bootstrapped.
    Block sums come from a prefix sum over the circularly extended series, so
    the cost per replication is the number of blocks rather than the number of
    rows.
    """
    rng = np.random.default_rng(seed)
    order = np.unique(sessions)
    p = 1.0 / max(block, 1.0)
    total = len(values)

    per_session = []
    for s in order:
        x = values[sessions == s].astype(float)
        n = len(x)
        if n == 0:
            continue
        ext = np.concatenate([x, x])            # circular wrap
        pre = np.concatenate([[0.0], np.cumsum(ext)])
        per_session.append((n, pre))

    draws = np.empty(replications, dtype=float)
    for r in range(replications):
        acc = 0.0
        for n, pre in per_session:
            # Enough geometric blocks to cover n with room to spare.
            k = max(int(n / max(block, 1.0)) * 2 + 8, 8)
            lens = rng.geometric(p, size=k)
            cum = np.cumsum(lens)
            take = int(np.searchsorted(cum, n) + 1)
            lens = lens[:take].copy()
            over = int(cum[take - 1] - n)
            if over > 0:
                lens[-1] -= over
            lens = lens[lens > 0]
            starts = rng.integers(0, n, size=len(lens))
            ends = starts + lens
            acc += float(np.sum(pre[ends] - pre[starts]))
        draws[r] = acc / total
    return float(draws.std(ddof=1))


def clustered_se(values: np.ndarray, clusters: np.ndarray) -> tuple[float, int]:
    """One-way cluster-robust SE of a mean. Plan (c), symbol clustering."""
    n = len(values)
    xbar = float(values.mean())
    per = pd.DataFrame({"d": values - xbar, "g": clusters}).groupby(
        "g", sort=False)["d"].sum().to_numpy()
    g = len(per)
    if g < 2:
        return float("nan"), g
    return float(np.sqrt((g / (g - 1)) * np.sum(per ** 2)) / n), g


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("features", nargs="+", type=pathlib.Path)
    ap.add_argument("--heldout-sessions", type=int, default=2)
    ap.add_argument("--heldout-symbols", type=int, default=50)
    ap.add_argument("--replications", type=int, default=REPLICATIONS)
    args = ap.parse_args()

    refuse_heldout(args.features)
    df = load(args.features)
    d = moved(df)
    sessions = sorted(df["session"].unique())

    print("=" * 78)
    print("GATED COMPUTATION -- development sessions only")
    print("=" * 78)
    print(f"sessions {len(sessions)}   windows {len(df):,}   "
          f"windows with a move {len(d):,}   symbols {df['symbol'].nunique()}")
    for s in sessions:
        sub = df[df["session"] == s]
        print(f"  {s:<28} windows {len(sub):>9,}  moved {int((sub['label_ticks'] != 0).sum()):>9,}")

    # ---- 1. m, and the thresholds -------------------------------------------
    print()
    print("-" * 78)
    print("1. THE SCALE m, AND THE THRESHOLDS  (computed before the MDE table)")
    print("-" * 78)
    per_session_m = []
    for s in sessions:
        sub = d[d["session"] == s]
        per_session_m.append((s, len(sub), float(np.abs(sub["label_halfspreads"]).mean())))
    absdelta = np.abs(d["label_halfspreads"].to_numpy(dtype=float))
    m_pooled = float(absdelta.mean())          # window-weighted by construction
    m_meanofmeans = float(np.mean([t[2] for t in per_session_m]))

    for s, n, mv in per_session_m:
        print(f"  {s:<28} windows {n:>9,}   m {mv:.4f}")
    print()
    print(f"  m, one session (12302019, provisional)   {ONE_SESSION_M:.4f}")
    print(f"  m, seven sessions, window-weighted       {m_pooled:.4f}   <- registered")
    print(f"  m, unweighted mean of per-session means  {m_meanofmeans:.4f}   (not used)")

    p_star = 0.5 + 0.05 / m_pooled
    r2_star = float(np.sin(np.pi * (0.05 / m_pooled)) ** 2)
    p_star_1 = 0.5 + 0.05 / ONE_SESSION_M
    r2_star_1 = float(np.sin(np.pi * (0.05 / ONE_SESSION_M)) ** 2)
    print()
    print(f"  {'':34}{'one session':>14}{'seven sessions':>16}")
    print(f"  accuracy bar  p* = 1/2 + 0.05/m {p_star_1*100:>13.2f}%{p_star*100:>15.2f}%")
    print(f"  R2 bar  R2* = sin^2(pi*0.05/m)  {r2_star_1:>14.4f}{r2_star:>16.4f}")
    thresholds = {
        "A - OFI, directional": p_star,
        "B - OFI, out-of-sample R2": r2_star,
        "C - queue imbalance, directional": p_star,
    }

    # ---- 2. block length ----------------------------------------------------
    print()
    print("-" * 78)
    print("2. POLITIS-WHITE BLOCK LENGTH  (stationary; max across sessions)")
    print("-" * 78)
    blocks = {}
    print(f"  {'candidate':<34}{'session':<26}{'n':>10}{'stationary':>12}")
    for cand, (feat, kind) in CANDIDATES.items():
        per = []
        for s in sessions:
            sub = d[d["session"] == s]
            x = summand(sub, feat, kind)
            opt = optimal_block_length(x)
            st = float(np.asarray(opt["stationary"])[0])
            per.append(st)
            print(f"  {cand:<34}{s:<26}{len(x):>10,}{st:>12.2f}")
        blocks[cand] = max(per)
        print(f"  {cand:<34}{'MAX TAKEN':<26}{'':>10}{blocks[cand]:>12.2f}")
        print()

    # ---- 3. MDEs ------------------------------------------------------------
    print("-" * 78)
    print("3. MDE AT THE PLANNED HELD-OUT SIZE   alpha 0.05, power 0.80, two-sided")
    print("-" * 78)
    windows_per_session = len(d) / len(sessions)
    heldout_windows = int(round(windows_per_session * args.heldout_sessions))
    print(f"  planned held-out size: {args.heldout_sessions} sessions x "
          f"{args.heldout_symbols} symbols")
    print(f"  development windows with a move {len(d):,}; mean per session "
          f"{windows_per_session:,.0f}")
    print(f"  projected held-out windows with a move  {heldout_windows:,}")
    print()

    # Candidate B's summand is the per-window SQUARED ERROR, whose mean is the
    # mean squared error -- not R2. The metric, and therefore the threshold, is
    # R2 = 1 - MSE/S with S the variance of the label. A standard error on the
    # MSE scale is converted by dividing by S: an increment of dR2 in R2 is an
    # increment of dR2 * S in MSE. Comparing an MSE-scale MDE against an R2
    # threshold would be a units error, and a large one.
    label_var = float(np.var(d["label_halfspreads"].to_numpy(dtype=float)))
    print(f"  label variance S (half-spreads^2), for the B conversion  {label_var:.6f}")
    print()

    rows = []
    for cand, (feat, kind) in CANDIDATES.items():
        x = summand(d, feat, kind)
        sess = d["session"].to_numpy()
        L = blocks[cand]
        scale = label_var if kind == "sqerr" else 1.0
        se_dev_b = stationary_bootstrap_se(x, sess, L, replications=args.replications) / scale
        # Plan (b) scales by the square root of the ratio of EFFECTIVE BLOCKS.
        # L cancels, so this is the row-count ratio; it is written out because
        # the block is the independent unit, not the row.
        se_held_b = se_dev_b * np.sqrt((len(x) / L) / (heldout_windows / L))
        mde_b = (Z_ALPHA + Z_POWER) * se_held_b

        sym = d["symbol"].astype(str).to_numpy()
        se_dev_c, n_sym = clustered_se(x, sym)
        se_dev_c = se_dev_c / scale
        se_held_c = se_dev_c * np.sqrt(n_sym / args.heldout_symbols)
        mde_c = (Z_ALPHA + Z_POWER) * se_held_c

        thr = thresholds[cand]
        rows.append((cand, thr, se_dev_b, mde_b, mde_b / thr, se_dev_c, mde_c, n_sym))

    print(f"  {'candidate':<34}{'threshold':>11}{'SE dev (b)':>12}{'MDE (b)':>11}"
          f"{'ratio':>9}{'MDE (c)':>11}")
    for cand, thr, se_b, mde_b, ratio, se_c, mde_c, n_sym in rows:
        print(f"  {cand:<34}{thr:>11.4f}{se_b:>12.6f}{mde_b:>11.6f}"
              f"{ratio:>9.3f}{mde_c:>11.6f}")
    print()
    print(f"  symbol clusters in development: {rows[0][7]}")
    print("  plan (c) is a sensitivity analysis and takes no part in selection.")
    print("  It is anti-conservative under common market moves.")

    # ---- 4. selection -------------------------------------------------------
    print()
    print("-" * 78)
    print("4. SELECTION  (smallest MDE-to-threshold ratio under plan (b))")
    print("-" * 78)
    best = min(r[4] for r in rows)
    tied = [r for r in rows if np.isclose(r[4], best, rtol=1e-9, atol=1e-12)]
    if len(tied) > 1:
        names = ", ".join(t[0] for t in tied)
        chosen = next((t for t in tied if t[0] == SIMPLER), tied[0])
        print(f"  TIE between {names}")
        print(f"  tie-break applied: ties go to the simpler feature -> {chosen[0]}")
        tie_used = True
    else:
        chosen = tied[0]
        print("  no tie; the tie-break was NOT needed")
        tie_used = False
    for cand, thr, se_b, mde_b, ratio, se_c, mde_c, n_sym in rows:
        mark = "  <== SELECTED" if cand == chosen[0] else ""
        print(f"    {cand:<34} ratio {ratio:.4f}{mark}")

    # ---- 5. guard and fallback ---------------------------------------------
    print()
    print("-" * 78)
    print("5. MINIMUM-BLOCKS GUARD AND PRE-COMMITTED FALLBACK")
    print("-" * 78)
    L = blocks[chosen[0]]
    eff = heldout_windows / L
    print(f"  selected block length            {L:.2f} windows")
    print(f"  projected held-out windows       {heldout_windows:,}")
    print(f"  effective blocks                 {eff:.1f}   (>= {MIN_BLOCKS} required)")
    guard_ok = eff >= MIN_BLOCKS
    print(f"  guard                            {'PASSES' if guard_ok else 'FAILS'}")
    print()
    print(f"  selected MDE under plan (b)      {chosen[3]:.6f}")
    print(f"  its economic threshold           {chosen[1]:.6f}")
    print(f"  ratio                            {chosen[4]:.4f}")
    fallback = chosen[4] > 1.0
    print(f"  fallback (ratio > 1)             {'TRIGGERED' if fallback else 'not triggered'}")
    print()
    if fallback or not guard_ok:
        print("  RESULT: the study is declared EXPLORATORY before any held-out access.")
    else:
        print("  RESULT: confirmatory as designed; the held-out run may proceed once")
        print("  the registration is locked. Sequence: section 10.")
    print()
    print("pre-committed sensitivity, block length 0.5x / 1x / 2x:")
    for mult in (0.5, 1.0, 2.0):
        print(f"  {mult:>4}x  {L * mult:>9.2f} windows   effective blocks "
              f"{heldout_windows / (L * mult):>9.1f}")
    print()
    print(f"tie-break needed: {'yes' if tie_used else 'no'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
