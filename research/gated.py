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
    "B - OFI, out-of-sample R2": ("ofi", "r2"),
    "C - queue imbalance, directional": ("queue_imbalance", "hit"),
}
# The series Politis-White is run on, per candidate: the hit indicator for the
# directional metrics, and the squared-error REDUCTION d for B.
BLOCK_SERIES = {"hit": "hit", "r2": "d"}
SIMPLER = "C - queue imbalance, directional"   # the registered tie-break

# Null value of each metric. Needed only for the diagnostic below, not for the
# registered rule.
NULLS = {
    "A - OFI, directional": 0.5,
    "B - OFI, out-of-sample R2": 0.0,
    "C - queue imbalance, directional": 0.5,
}


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


def summand(d: pd.DataFrame, feature: str, kind: str) -> dict[str, np.ndarray]:
    """The per-window summand(s) of a candidate's metric, in event order.

    For the directional candidates this is the hit indicator and the metric is
    its mean.

    For candidate B the metric is a RATIO, and the amendment of 2026-09-24
    fixes what is resampled. The registration's original summand -- the
    per-window squared error -- is the summand of the MSE, not of an R2, and
    dividing SE(MSE) by a fixed S overstates the uncertainty of R2: under a
    weak signal MSE and S are close and move together, so the ratio is far
    better determined than its numerator. What is returned instead is

        b = (y - baseline)^2        the baseline forecast's squared error
        e = (y - beta x)^2          the fitted model's squared error
        d = b - e                   the per-window squared-error REDUCTION

    with R2 = mean(d) / mean(b). The baseline forecast is the training-sample
    mean of y: the registration said "out-of-sample R2" without naming a
    baseline, so it is pinned here.
    """
    if kind == "hit":
        return {"hit": (np.sign(d[feature]) == np.sign(d["label_ticks"])).to_numpy(dtype=float)}
    if kind == "r2":
        x = d[feature].to_numpy(dtype=float)
        y = d["label_halfspreads"].to_numpy(dtype=float)
        xx = float(np.dot(x, x))
        beta = float(np.dot(x, y) / xx) if xx else 0.0
        baseline = float(y.mean())
        b = (y - baseline) ** 2
        e = (y - beta * x) ** 2
        return {"d": b - e, "b": b}
    raise SystemExit(f"unknown summand {kind}")


def stationary_bootstrap_se(cols: dict[str, np.ndarray], sessions: np.ndarray, block: float,
                            combine, replications: int = REPLICATIONS,
                            seed: int = SEED) -> float:
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
    names = list(cols)
    total = len(next(iter(cols.values())))

    per_session = []
    for s in order:
        sel = sessions == s
        n = int(sel.sum())
        if n == 0:
            continue
        pres = {}
        for k in names:
            x = cols[k][sel].astype(float)
            ext = np.concatenate([x, x])        # circular wrap
            pres[k] = np.concatenate([[0.0], np.cumsum(ext)])
        per_session.append((n, pres))

    draws = np.empty(replications, dtype=float)
    for r in range(replications):
        acc = {k: 0.0 for k in names}
        for n, pres in per_session:
            # Enough geometric blocks to cover n with room to spare.
            k_draw = max(int(n / max(block, 1.0)) * 2 + 8, 8)
            lens = rng.geometric(p, size=k_draw)
            cum = np.cumsum(lens)
            take = int(np.searchsorted(cum, n) + 1)
            lens = lens[:take].copy()
            over = int(cum[take - 1] - n)
            if over > 0:
                lens[-1] -= over
            lens = lens[lens > 0]
            starts = rng.integers(0, n, size=len(lens))
            ends = starts + lens
            for key in names:
                pre = pres[key]
                acc[key] += float(np.sum(pre[ends] - pre[starts]))
        draws[r] = combine(acc, total)
    return float(draws.std(ddof=1))


def clustered_se_influence(infl: np.ndarray, clusters: np.ndarray) -> tuple[float, int]:
    """One-way cluster-robust SE from a statistic's influence function.

    For a mean the influence function is x - xbar and this is the ordinary
    cluster-robust SE of a mean. For a ratio of means it is the standard
    linearisation, which lets both candidates use one estimator instead of a
    separate variance derivation each. Plan (c), symbol clustering.
    """
    n = len(infl)
    per = pd.DataFrame({"d": infl, "g": clusters}).groupby(
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
            x = summand(sub, feat, kind)[BLOCK_SERIES[kind]]
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
    # Held-out window projection. The PRIMARY projection uses the development
    # MINIMUM windows-per-session, not the mean: a smaller held-out sample
    # gives a larger MDE, and the fallback exists to catch the case where the
    # study cannot resolve an effect worth having. Projecting with the mean
    # would understate the MDE whenever a held-out session is quieter than
    # average, which is exactly the case the guard is for. The mean-based
    # projection is reported beside it as a sensitivity and decides nothing.
    per_sess_moved = [int((df[df["session"] == s]["label_ticks"] != 0).sum())
                      for s in sessions]
    min_per_session = min(per_sess_moved)
    mean_per_session = float(np.mean(per_sess_moved))
    heldout_windows = int(round(min_per_session * args.heldout_sessions))
    heldout_windows_sens = int(round(mean_per_session * args.heldout_sessions))
    print(f"  planned held-out size: {args.heldout_sessions} sessions x "
          f"{args.heldout_symbols} symbols")
    print(f"  development windows with a move {len(d):,}")
    print(f"  per-session minimum {min_per_session:,}   mean {mean_per_session:,.0f}")
    print(f"  projected held-out windows, PRIMARY (min x {args.heldout_sessions})     "
          f"{heldout_windows:,}   <- decides the fallback")
    print(f"  projected held-out windows, sensitivity (mean x {args.heldout_sessions}) "
          f"{heldout_windows_sens:,}")
    print()

    rows = []
    sess = d["session"].to_numpy()
    sym = d["symbol"].astype(str).to_numpy()
    for cand, (feat, kind) in CANDIDATES.items():
        cols = summand(d, feat, kind)
        L = blocks[cand]
        n = len(next(iter(cols.values())))

        if kind == "hit":
            # Statistic is a mean; bootstrap it directly.
            combine = lambda acc, tot: acc["hit"] / tot
            point = float(cols["hit"].mean())
            infl = cols["hit"] - point           # influence function of a mean
        else:
            # R2 is a RATIO of two means. It is bootstrapped as a ratio --
            # recomputed from both resampled sums on every replication -- so
            # the dependence between numerator and denominator is carried
            # rather than assumed away by holding the denominator fixed.
            combine = lambda acc, tot: acc["d"] / acc["b"] if acc["b"] else float("nan")
            mean_b = float(cols["b"].mean())
            point = float(cols["d"].mean()) / mean_b
            # Influence function of a ratio of means, for the clustered SE.
            infl = (cols["d"] - point * cols["b"]) / mean_b

        se_dev_b = stationary_bootstrap_se(cols, sess, L, combine,
                                           replications=args.replications)
        # Plan (b) scales by the square root of the ratio of EFFECTIVE BLOCKS.
        # L cancels, so this is the row-count ratio; it is written out because
        # the block is the independent unit, not the row.
        se_held_b = se_dev_b * np.sqrt((n / L) / (heldout_windows / L))
        mde_b = (Z_ALPHA + Z_POWER) * se_held_b
        se_held_b_s = se_dev_b * np.sqrt(n / heldout_windows_sens)
        mde_b_sens = (Z_ALPHA + Z_POWER) * se_held_b_s

        # Plan (c): cluster the influence function on symbol. For a mean this
        # is the ordinary cluster-robust SE; for the ratio it is the same
        # estimator applied to the linearisation, which is what makes the two
        # candidates comparable.
        se_dev_c, n_sym = clustered_se_influence(infl, sym)
        se_held_c = se_dev_c * np.sqrt(n_sym / args.heldout_symbols)
        mde_c = (Z_ALPHA + Z_POWER) * se_held_c

        thr = thresholds[cand]
        rows.append((cand, thr, se_dev_b, mde_b, mde_b / thr, se_dev_c, mde_c,
                     n_sym, point, mde_b_sens, mde_b_sens / thr))

    print(f"  {'candidate':<34}{'dev est':>10}{'threshold':>11}{'SE dev (b)':>12}"
          f"{'MDE (b)':>11}{'ratio':>9}{'MDE (c)':>11}")
    for (cand, thr, se_b, mde_b, ratio, se_c, mde_c, n_sym, point,
         mde_bs, ratio_s) in rows:
        print(f"  {cand:<34}{point:>10.5f}{thr:>11.4f}{se_b:>12.6f}"
              f"{mde_b:>11.6f}{ratio:>9.3f}{mde_c:>11.6f}")
    print()
    print("  sensitivity, held-out windows projected from the development MEAN:")
    for (cand, thr, se_b, mde_b, ratio, se_c, mde_c, n_sym, point,
         mde_bs, ratio_s) in rows:
        print(f"    {cand:<34}MDE (b) {mde_bs:>10.6f}   ratio {ratio_s:>8.3f}")
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
    for r in rows:
        mark = "  <== SELECTED" if r[0] == chosen[0] else ""
        print(f"    {r[0]:<34} ratio {r[4]:.4f}{mark}")

    # ---- diagnostic: the registered ratio is not scale-consistent ----------
    #
    # The registered rule divides the MDE by the ABSOLUTE threshold. The MDE is
    # a detectable DIFFERENCE from the null; the threshold for a directional
    # metric is a LEVEL (0.5316), while for R2 it is already a difference,
    # because that metric's null is zero. So A and C are divided by ~0.53 and B
    # by ~0.0098, which flatters the directional candidates by a factor of
    # about seventeen for no reason connected to what they can resolve.
    #
    # The scale-consistent denominator is the threshold's EXCESS OVER ITS OWN
    # NULL -- the size of the effect that has to be detected. This is reported
    # as a diagnostic and CHANGES NOTHING: the registered rule is what selects,
    # until it is amended in the usual way.
    print()
    print("  diagnostic, not the registered rule: MDE / (threshold - null),")
    print("  which is the effect size that actually has to be resolved.")
    diag = []
    for r in rows:
        excess = r[1] - NULLS[r[0]]
        diag.append((r[0], r[3], excess, r[3] / excess if excess else float("nan")))
    best_d = min(t[3] for t in diag)
    for name, mde, excess, ratio in diag:
        mark = "  <== would select" if np.isclose(ratio, best_d) else ""
        print(f"    {name:<34} MDE {mde:.6f} / {excess:.6f} = {ratio:.4f}{mark}")
    agree = np.isclose(min(t[3] for t in diag), next(t[3] for t in diag if t[0] == chosen[0]))
    print(f"  agrees with the registered rule: {'yes' if agree else 'NO'}")

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
