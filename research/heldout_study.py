#!/usr/bin/env python3
"""The registered analysis, run over a set of sessions. Nothing here is chosen.

Replaces the held-out path that `research/run_heldout.sh` used to call. That
path could not answer the registered questions, and the four reasons are set
out in `docs/heldout-harness-amendment.md`. This module implements the rules
as registered and refuses when a registered constant is absent.

EVERY constant is READ, never estimated:

  * the block length L, the thresholds, m and the development estimates come
    from `docs/generated/gated_values.tsv`, which `research/gated.py` wrote
    from development sessions before the registration commit;
  * the value requirement, the Holm family size, the fee tiers and the rule-4
    setting come from the registration text.

Nothing is re-estimated on held-out data. A held-out session contributes
features and labels and nothing else.

The interval is the registered plan (b): the stationary bootstrap within
sessions at L, sharing `gated.stationary_bootstrap_se` rather than a copy of
it, so the held-out interval cannot drift from the one the power analysis was
computed with.

    usage: research/heldout_study.py --sessions F [F ...] --out DIR
                                     --primary C --label heldout|development
"""

from __future__ import annotations

import argparse
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import numpy as np                     # noqa: E402
import pandas as pd                    # noqa: E402

import gated                           # noqa: E402  -- shared, not copied
import study                           # noqa: E402

GEN = pathlib.Path("docs/generated/gated_values.tsv")
PREREG = pathlib.Path("docs/preregistration.md")

# Section 3: with a sign-based primary the family is four features plus the
# direct-value test. Section 3 also fixes that it does not shrink because A
# and B became exploratory.
HOLM_FAMILY = 5

# Section 5 drops micro-price deviation for a sign-based primary: normalised
# it is algebraically identical to queue imbalance, so it is not a distinct
# test. It is removed from this path rather than computed and discarded.
SECONDARY_FEATURES = ("ofi", "trade_sign")

# Section 7, base tier and top tier, dollars per share.
FEES = {
    "base_add_tape_c": 0.0015,
    "base_add_tape_ab": 0.0020,
    "take": 0.0030,
    "top_add": 0.00305,
}
RULE4_PRIMARY = False      # section 6: OFF in the primary specification

SCOPE = ("With two held-out sessions, a confirmatory verdict applies to those "
         "sessions and is not generalised beyond them.")


class Missing(Exception):
    """A registered constant is absent. Never a reason to pick one."""


def read_generated() -> dict[str, str]:
    if not GEN.exists():
        raise Missing(f"{GEN} is absent; run research/gated.py")
    out = {}
    for line in GEN.read_text().splitlines():
        if line.strip() and not line.startswith("#"):
            k, _, v = line.partition("\t")
            out[k] = v
    return out


def need(gen: dict[str, str], key: str) -> str:
    if key not in gen:
        raise Missing(f"{GEN} has no value for {key!r}")
    return gen[key]


def num(gen: dict[str, str], key: str) -> float:
    return float(need(gen, key).rstrip("%").replace(",", ""))


def registered_signal_threshold() -> float:
    """Section 6's quoting threshold, from section 8.

    Section 8 carries the literal text "Signal threshold *(to be filled)*", so
    there is nothing to read. Raising here is the registered behaviour: the
    strategy cannot be simulated without it, and choosing one would be setting
    a registered parameter after the data is on disk.
    """
    text = PREREG.read_text()
    if "Signal threshold *(to be filled)*" in text:
        raise Missing(
            "docs/preregistration.md section 8 leaves the SIGNAL THRESHOLD "
            "unfilled: 'Signal threshold *(to be filled)*'. Section 6 posts a "
            "quote only when the signal's magnitude exceeds it, so the maker "
            "strategy cannot be simulated. This is not a value to choose here.")
    raise Missing("could not locate the signal threshold in section 8")


def registered_min_fills() -> int:
    text = PREREG.read_text()
    if "Minimum fills per session *(to be filled)*" in text:
        raise Missing(
            "docs/preregistration.md section 8 leaves MINIMUM FILLS PER "
            "SESSION unfilled, and section 9 makes 'fewer than (N) simulated "
            "fills per held-out session' a study-failure criterion. Without N "
            "that criterion cannot be evaluated.")
    raise Missing("could not locate the minimum fills per session in section 8")


def hit(df: pd.DataFrame, feature: str) -> tuple[np.ndarray, np.ndarray]:
    """The registered directional summand and its session labels.

    Windows with a zero label are excluded, as registered. A feature of
    exactly zero is scored as a miss, which is the registered primary metric
    and is conservative; see the leakage audit in section 4.
    """
    d = df[df[study.LABEL] != 0].sort_values(["session", "window_end_ts"])
    h = (np.sign(d[feature]) == np.sign(d[study.LABEL])).to_numpy(dtype=float)
    return h, d["session"].to_numpy()


def interval(values: np.ndarray, sessions: np.ndarray, block: float,
             reps: int, seed: int) -> tuple[float, float, float, float]:
    """Point estimate, SE and 95% interval under plan (b)."""
    point = float(values.mean())
    se = gated.stationary_bootstrap_se(
        {"v": values}, sessions, block,
        lambda acc, tot: acc["v"] / tot, replications=reps, seed=seed)
    z = 1.959963985
    return point, se, point - z * se, point + z * se


def holm(pvals: dict[str, float], family: int) -> dict[str, tuple[float, float, bool]]:
    """Holm-Bonferroni over a family whose size is fixed by section 3."""
    ordered = sorted(pvals.items(), key=lambda kv: kv[1])
    out, prev = {}, 0.0
    for i, (name, p) in enumerate(ordered):
        adj = min(1.0, max(prev, p * (family - i)))
        prev = adj
        out[name] = (p, adj, adj < 0.05)
    return out


def one_sided_p(point: float, se: float, null: float) -> float:
    """P(observe this or more extreme | null), upper tail, normal."""
    if se <= 0 or not np.isfinite(se):
        return float("nan")
    from math import erfc, sqrt
    z = (point - null) / se
    return 0.5 * erfc(z / sqrt(2.0))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--sessions", nargs="+", required=True, type=pathlib.Path,
                    help="feature CSVs for the sessions under test")
    ap.add_argument("--out", required=True, type=pathlib.Path)
    ap.add_argument("--primary", required=True, choices=["A", "B", "C"],
                    help="the registered primary; no default, on purpose")
    ap.add_argument("--label", required=True, choices=["heldout", "development"])
    ap.add_argument("--bootstrap", type=int, default=10000)
    ap.add_argument("--seed", type=int, default=gated.SEED)
    ap.add_argument("--skip-strategy", action="store_true",
                    help="rehearsal only: skip the P&L, which needs section 8's "
                         "unfilled constants")
    args = ap.parse_args()

    if args.primary != "C":
        print(f"primary {args.primary} is not the registered primary (C)",
              file=sys.stderr)
        return 2

    args.out.mkdir(parents=True, exist_ok=True)
    gen = read_generated()

    # ---- registered constants, read and echoed so a reader can check them ---
    L = num(gen, "selected_block_length")
    bar_c = num(gen, "threshold_C")
    bar_a = num(gen, "threshold_A")
    bar_b = num(gen, "threshold_B")
    m_all = num(gen, "m_all")
    if need(gen, "selected") != "C - queue imbalance, directional":
        print(f"generated values name a different primary: {gen['selected']}",
              file=sys.stderr)
        return 2

    df = study.load(list(args.sessions))
    n_sessions = df["session"].nunique()

    print("=" * 74)
    print(f"REGISTERED ANALYSIS -- {args.label.upper()}")
    print("=" * 74)
    print(f"sessions {n_sessions}   windows {len(df):,}   "
          f"symbols {df['symbol'].nunique()}")
    for s in sorted(df["session"].unique()):
        print(f"  {s}")
    print()
    print("registered constants, read not estimated:")
    print(f"  block length L for the primary                {L}")
    print(f"  block lengths for the secondaries             "
          f"ofi {num(gen, 'block_A')}, trade_sign "
          f"{num(gen, 'block_trade_sign')}, value "
          f"{num(gen, 'block_direct_value')}")
    print(f"  accuracy bar for C                            {bar_c}")
    print(f"  R2 bar for B                                  {bar_b}")
    print(f"  m over all windows                            {m_all}")
    print(f"  value requirement                             0.10 half-spreads/window")
    print(f"  Holm family                                   {HOLM_FAMILY}")
    print(f"  rule 4 in the primary specification           {'ON' if RULE4_PRIMARY else 'OFF'}")
    print(f"  base add / take / top add ($/share)           "
          f"{FEES['base_add_tape_c']} / {FEES['take']} / {FEES['top_add']}")
    print()

    # ---- primary -----------------------------------------------------------
    print("-" * 74)
    print("PRIMARY -- candidate C, queue imbalance, directional")
    print("-" * 74)
    h, sess = hit(df, "queue_imbalance")
    point, se, lo, hi = interval(h, sess, L, args.bootstrap, args.seed)
    print(f"  windows with a move   {len(h):,}")
    print(f"  directional accuracy  {point:.5f}")
    print(f"  plan (b) SE at L={L}  {se:.6f}")
    print(f"  95% interval          [{lo:.5f}, {hi:.5f}]")
    print(f"  registered bar        {bar_c}")
    print(f"  null                  0.5")
    holds = point > bar_c and lo > 0.5
    fails = hi < bar_c
    verdict = "SIGNAL HOLDS" if holds else ("SIGNAL FAILS" if fails else "INCONCLUSIVE")
    print(f"  VERDICT               {verdict}")
    if verdict == "INCONCLUSIVE":
        print("    The interval contains both the null and the economically")
        print("    meaningful effect. That is a result, and it is not the same")
        print("    as the effect being absent.")
    print()
    print(f"  {SCOPE}")
    print()

    # symbol-clustered sensitivity, which decides nothing (section 8)
    d = df[df[study.LABEL] != 0].sort_values(["session", "window_end_ts"])
    se_c, n_sym = gated.clustered_se_influence(h - point,
                                               d["symbol"].astype(str).to_numpy())
    print(f"  sensitivity, symbol-clustered SE {se_c:.6f} over {n_sym} symbols,")
    print(f"    95% interval [{point - 1.959963985 * se_c:.5f}, "
          f"{point + 1.959963985 * se_c:.5f}]")
    print("    Anti-conservative under common market moves. Decides nothing;")
    print("    if it disagrees with the block bootstrap the verdict follows")
    print("    the block bootstrap and the disagreement is reported.")
    print()

    # ---- secondaries under Holm -------------------------------------------
    print("-" * 74)
    print(f"SECONDARIES -- Holm-Bonferroni, family of {HOLM_FAMILY}")
    print("-" * 74)
    # Each secondary uses the block length the registered rule returns for ITS
    # OWN summand, not the primary's. Section 4 fixes the procedure per series;
    # the values are in the generated file.
    sec_block = {"ofi": num(gen, "block_A"),
                 "trade_sign": num(gen, "block_trade_sign"),
                 "direct_value": num(gen, "block_direct_value")}
    pvals, detail = {}, {}
    for feat in SECONDARY_FEATURES:
        hv, sv = hit(df, feat)
        pt, s_, l_, h_ = interval(hv, sv, sec_block[feat], args.bootstrap, args.seed)
        p = one_sided_p(pt, s_, 0.5)
        pvals[feat] = p
        detail[feat] = (pt, s_, l_, h_)

    # the direct-value secondary, section 3
    terms, n_v, n_zero = study.direct_value(df, "queue_imbalance")
    sess_all = df.sort_values(["session", "window_end_ts"])["session"].to_numpy()
    terms_sorted = (df.sort_values(["session", "window_end_ts"])["label_halfspreads"]
                    .to_numpy(dtype=float)
                    * np.sign(df.sort_values(["session", "window_end_ts"])
                              ["queue_imbalance"].to_numpy(dtype=float)))
    v_pt, v_se, v_lo, v_hi = interval(terms_sorted, sess_all,
                                      sec_block["direct_value"], args.bootstrap,
                                      args.seed)
    pvals["direct_value"] = one_sided_p(v_pt, v_se, 0.10)
    detail["direct_value"] = (v_pt, v_se, v_lo, v_hi)

    adj = holm(pvals, HOLM_FAMILY)
    print(f"  {'test':<16}{'estimate':>11}{'SE':>10}{'95% interval':>26}"
          f"{'p':>10}{'Holm p':>10}  sig")
    for name in ("ofi", "trade_sign", "direct_value"):
        pt, s_, l_, h_ = detail[name]
        p, a, sig = adj[name]
        print(f"  {name:<16}{pt:>11.5f}{s_:>10.6f}"
              f"{f'[{l_:.5f}, {h_:.5f}]':>26}{p:>10.4g}{a:>10.4g}  "
              f"{'yes' if sig else 'no'}")
    print()
    print("  direct-value secondary: mean signed mid move in the predicted")
    print("  direction, half-spreads, over ALL windows; a zero feature")
    print(f"  contributes zero and stays in the denominator ({n_zero:,} of")
    print(f"  {n_v:,} windows). Tested against 0.10, one-sided.")
    print(f"  requirement 0.10   estimate {v_pt:.5f}   "
          f"{'CLEARS' if v_lo > 0.10 else 'does not clear'} on the interval")
    print()
    print("  Family size is five and does not shrink because A and B became")
    print("  exploratory; section 3 records why. Two of the five members are")
    print("  reported above, the primary is reported separately, and micro-")
    print("  price deviation is dropped as sign-identical to queue imbalance.")
    print()

    # ---- exploratory A and B ----------------------------------------------
    print("-" * 74)
    print("EXPLORATORY -- candidates A and B, considered and NOT selected")
    print("-" * 74)
    ha, sa = hit(df, "ofi")
    a_pt, a_se, a_lo, a_hi = interval(ha, sa, num(gen, "block_A"),
                                      args.bootstrap, args.seed)
    print(f"  A  OFI directional   {a_pt:.5f}  "
          f"[{a_lo:.5f}, {a_hi:.5f}]  bar {bar_a}")
    dmoved = gated.moved(df)
    cols = gated.summand(dmoved, "ofi", "r2")
    b_pt = float(cols["d"].mean() / cols["b"].mean())
    b_se = gated.stationary_bootstrap_se(
        cols, dmoved["session"].to_numpy(), num(gen, "block_B"),
        lambda acc, tot: acc["d"] / acc["b"] if acc["b"] else float("nan"),
        replications=args.bootstrap, seed=args.seed)
    print(f"  B  OFI out-of-sample R2  {b_pt:.5f}  "
          f"[{b_pt - 1.959963985 * b_se:.5f}, {b_pt + 1.959963985 * b_se:.5f}]  "
          f"bar {bar_b}")
    print()
    print("  EXPLORATORY ONLY. Outside every confirmatory claim and outside")
    print("  the Holm family. A number here that clears a bar is still")
    print("  exploratory and is never reported as a tested hypothesis.")
    print()

    # ---- strategy ---------------------------------------------------------
    print("-" * 74)
    print("STRATEGY -- maker, base tier, with the top-tier sensitivity")
    print("-" * 74)
    if args.skip_strategy:
        print("  SKIPPED by --skip-strategy (rehearsal).")
    else:
        registered_signal_threshold()   # raises Missing; never returns
    print()
    print("=" * 74)
    print(f"wrote {args.out}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Missing as exc:
        print(f"\nREGISTERED CONSTANT MISSING\n{exc}\n", file=sys.stderr)
        print("Refusing to proceed. Choosing a registered parameter here, with "
              "held-out data on disk, is the failure the registration exists "
              "to prevent.", file=sys.stderr)
        sys.exit(3)
