#!/usr/bin/env python3
"""Bias of the market-by-price queue models against exact queue position.

Stage 7. Reads the CSVs written by ``queue_study`` and produces the bias table
and figures. The exact market-by-order model is the reference throughout: the
question is not whether it works — it is a known model, see ``docs/design.md``
record 021 — but how far each approximation departs from it.

    usage: research/queue_bias.py <study-dir> --venue BX --date 2019-01-30
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
MUTED = "#8a8a8a"
MODEL_COLOUR = {
    "exact": "#1b1b1b",
    "conservative": "#0b6e99",
    "optimistic": "#b8500e",
    "proportional": "#4a9e5c",
    "bernoulli-prop": "#7a5ea8",
}
MODEL_ORDER = ["exact", "conservative", "optimistic", "proportional", "bernoulli-prop"]

plt.rcParams.update(
    {
        "figure.dpi": 140,
        "savefig.dpi": 140,
        "savefig.bbox": "tight",
        "font.size": 9,
        "axes.edgecolor": INK,
        "axes.labelcolor": INK,
        "axes.titlesize": 10,
        "axes.titleweight": "bold",
        "axes.spines.top": False,
        "axes.spines.right": False,
        "text.color": INK,
        "xtick.color": INK,
        "ytick.color": INK,
        "grid.color": "#dddddd",
        "grid.linewidth": 0.6,
    }
)


def _stamp(fig, venue: str, date: str, rule4: bool) -> None:
    fig.text(
        0.99,
        -0.02,
        f"{venue} {date} · rule 4 {'on' if rule4 else 'off'} · carteret",
        ha="right",
        va="top",
        fontsize=7,
        color=MUTED,
    )


def _depth_label(low: int, high: int) -> str:
    return f"{low:,}+" if high < 0 else f"{low:,}–{high:,}"


def bias_by_depth(bias: pd.DataFrame, out: pathlib.Path, venue: str, date: str,
                  rule4: bool) -> dict:
    """Fill rate by queue depth at entry, one line per model."""
    d = bias[bias["rule4"] == int(rule4)]
    depths = sorted(d["depth_bucket"].unique())
    labels = [
        _depth_label(
            int(d[d["depth_bucket"] == b]["depth_low"].iloc[0]),
            int(d[d["depth_bucket"] == b]["depth_high"].iloc[0]),
        )
        for b in depths
    ]

    fig, axes = plt.subplots(1, 2, figsize=(10.0, 4.0))
    x = np.arange(len(depths))

    for model in MODEL_ORDER:
        sub = d[d["model"] == model].set_index("depth_bucket").reindex(depths)
        axes[0].plot(
            x,
            sub["fill_rate"].to_numpy() * 100,
            marker="o",
            ms=4,
            lw=1.6 if model == "exact" else 1.2,
            ls="-" if model == "exact" else "--",
            color=MODEL_COLOUR[model],
            label=model,
        )
    axes[0].set_xticks(x)
    axes[0].set_xticklabels(labels, rotation=20, ha="right")
    axes[0].set_xlabel("shares ahead at entry")
    axes[0].set_ylabel("fill rate (%)")
    axes[0].set_title("Fill rate by queue position")
    axes[0].legend(frameon=False, fontsize=8)
    axes[0].grid(True, axis="y")

    exact = d[d["model"] == "exact"].set_index("depth_bucket").reindex(depths)
    for model in MODEL_ORDER[1:]:
        sub = d[d["model"] == model].set_index("depth_bucket").reindex(depths)
        rel = np.where(
            exact["fill_rate"].to_numpy() > 0,
            (sub["fill_rate"].to_numpy() - exact["fill_rate"].to_numpy())
            / np.where(exact["fill_rate"].to_numpy() > 0, exact["fill_rate"].to_numpy(), 1)
            * 100,
            np.nan,
        )
        axes[1].plot(x, rel, marker="o", ms=4, lw=1.3, color=MODEL_COLOUR[model], label=model)
    axes[1].axhline(0, color=INK, lw=1.0)
    axes[1].set_xticks(x)
    axes[1].set_xticklabels(labels, rotation=20, ha="right")
    axes[1].set_xlabel("shares ahead at entry")
    axes[1].set_ylabel("fill-rate error vs exact (%)")
    axes[1].set_title("Bias against exact queue position")
    axes[1].legend(frameon=False, fontsize=8)
    axes[1].grid(True, axis="y")

    fig.suptitle(
        f"Market-by-price queue models against exact — {venue} {date}", fontweight="bold"
    )
    _stamp(fig, venue, date, rule4)
    suffix = "rule4on" if rule4 else "rule4off"
    fig.savefig(out / f"queue_bias_{venue.lower()}_{date}_{suffix}.png")
    plt.close(fig)

    totals = d.groupby("model").apply(
        lambda g: g["filled"].sum() / g["placed"].sum(), include_groups=False
    )
    e = float(totals["exact"])
    return {
        "placed": int(d[d["model"] == "exact"]["placed"].sum()),
        "exact_fill_rate_pct": e * 100,
        **{
            f"{m}_fill_rate_pct": float(totals[m]) * 100 for m in MODEL_ORDER[1:]
        },
        **{
            f"{m}_relative_bias_pct": (float(totals[m]) - e) / e * 100
            for m in MODEL_ORDER[1:]
        },
    }


def time_to_fill(ttf: pd.DataFrame, out: pathlib.Path, venue: str, date: str,
                 rule4: bool) -> dict:
    """Cumulative time-to-fill, which is where the approximations differ most."""
    d = ttf[ttf["rule4"] == int(rule4)]

    fig, ax = plt.subplots(figsize=(7.2, 4.0))
    medians = {}
    for model in MODEL_ORDER:
        sub = d[d["model"] == model].sort_values("bucket")
        if sub.empty:
            continue
        total = sub["count"].sum()
        cum = sub["count"].cumsum() / total
        ax.step(
            sub["low_ns"] / 1e9,
            cum * 100,
            where="post",
            lw=1.8 if model == "exact" else 1.2,
            ls="-" if model == "exact" else "--",
            color=MODEL_COLOUR[model],
            label=f"{model} (n={total:,})",
        )
        # Interpolated within the log bucket that straddles the median.
        # Taking the bucket's geometric midpoint instead reports the same
        # value for every model whenever they share a bucket, which at two
        # buckets per decade they usually do -- and that looks like agreement
        # rather than like a resolution limit.
        cum_arr = cum.to_numpy()
        lo = sub["low_ns"].to_numpy(dtype=float)
        hi = sub["high_ns"].to_numpy(dtype=float)
        idx = int(np.searchsorted(cum_arr, 0.5))
        if idx >= len(cum_arr):
            medians[model] = float("nan")
        else:
            below = cum_arr[idx - 1] if idx > 0 else 0.0
            span = cum_arr[idx] - below
            frac = (0.5 - below) / span if span > 0 else 0.0
            medians[model] = float(lo[idx] * (hi[idx] / lo[idx]) ** frac / 1e9)
    ax.set_xscale("log")
    ax.set_xlabel("time to fill (seconds, log scale)")
    ax.set_ylabel("% of filled orders")
    ax.set_title(f"Time to fill, given a fill — {venue} {date}")
    ax.legend(frameon=False, fontsize=8)
    ax.grid(True, axis="y")
    _stamp(fig, venue, date, rule4)
    suffix = "rule4on" if rule4 else "rule4off"
    fig.savefig(out / f"queue_time_to_fill_{venue.lower()}_{date}_{suffix}.png")
    plt.close(fig)

    return {f"median_ttf_{m}_s": v for m, v in medians.items()}


def symbol_bootstrap(study: pathlib.Path, rule4: bool, replications: int = 10000,
                     seed: int = 20190130) -> dict:
    """Confidence intervals for the fill rate and the bias, clustering by symbol.

    Orders placed on one symbol share that symbol's book, spread and flow, so
    they are not independent observations. Resampling individual placements
    would treat them as if they were and produce intervals far too narrow;
    resampling whole symbols with replacement keeps each symbol's internal
    structure intact and treats the symbol as the unit of independent
    information.

    The bias is a paired quantity -- every model sees the identical placements
    -- so a replication resamples symbols ONCE and recomputes every model's
    rate on that same draw. Resampling per model would add variance that the
    paired design removes.
    """
    suffix = "rule4on" if rule4 else "rule4off"
    df = pd.read_csv(study / f"queue_by_symbol_{suffix}.csv")

    wide = df.pivot_table(index="symbol", columns="model",
                          values=["placed", "filled"], aggfunc="sum").fillna(0)
    symbols = wide.index.to_numpy()
    placed = {m: wide[("placed", m)].to_numpy(dtype=float) for m in MODEL_ORDER}
    filled = {m: wide[("filled", m)].to_numpy(dtype=float) for m in MODEL_ORDER}

    rng = np.random.default_rng(seed)
    n = len(symbols)
    draws = {m: np.empty(replications) for m in MODEL_ORDER}
    bias = {m: np.empty(replications) for m in MODEL_ORDER[1:]}

    for r in range(replications):
        idx = rng.integers(0, n, size=n)
        rates = {}
        for m in MODEL_ORDER:
            p_sum = placed[m][idx].sum()
            rates[m] = filled[m][idx].sum() / p_sum if p_sum else np.nan
            draws[m][r] = rates[m]
        for m in MODEL_ORDER[1:]:
            bias[m][r] = (rates[m] - rates["exact"]) / rates["exact"] if rates["exact"] else np.nan

    out = {"symbols": int(n), "replications": int(replications)}
    for m in MODEL_ORDER:
        point = filled[m].sum() / placed[m].sum()
        lo, hi = np.percentile(draws[m], [2.5, 97.5])
        out[f"fill_{m}"] = point * 100
        out[f"fill_{m}_ci"] = f"[{lo * 100:.2f}, {hi * 100:.2f}]"
    exact_point = filled["exact"].sum() / placed["exact"].sum()
    for m in MODEL_ORDER[1:]:
        point = (filled[m].sum() / placed[m].sum() - exact_point) / exact_point
        lo, hi = np.percentile(bias[m], [2.5, 97.5])
        out[f"bias_{m}"] = point * 100
        out[f"bias_{m}_ci"] = f"[{lo * 100:.2f}, {hi * 100:.2f}]"
        out[f"bias_{m}_excludes_zero"] = bool(lo > 0 or hi < 0)
    return out


def seed_spread(study: pathlib.Path, rule4: bool) -> dict:
    """Range of the fill rate across placement sequences.

    Separate from the bootstrap and answering a different question: the
    bootstrap asks how much the result depends on WHICH SYMBOLS were sampled,
    this asks how much it depends on WHICH ORDERS were placed. A result whose
    seed range is comparable to the effect is not a result.
    """
    suffix = "rule4on" if rule4 else "rule4off"
    path = study / f"queue_seeds_{suffix}.csv"
    if not path.exists():
        return {}
    df = pd.read_csv(path)
    out = {"seeds": int(df["seed_index"].nunique())}
    for m in MODEL_ORDER:
        sub = df[df["model"] == m]["fill_rate"]
        out[f"seed_mean_{m}"] = float(sub.mean()) * 100
        out[f"seed_range_{m}"] = float(sub.max() - sub.min()) * 100

    # The spread of the BIAS, not of the level, is the one that matters. The
    # bias is paired -- every model sees the identical placements -- so the
    # placement-to-placement variation largely cancels, and comparing a bias
    # against the level's seed range would overstate the noise by an order of
    # magnitude.
    wide = df.pivot(index="seed_index", columns="model", values="fill_rate")
    for m in MODEL_ORDER[1:]:
        b = (wide[m] - wide["exact"]) / wide["exact"] * 100
        out[f"seed_bias_mean_{m}"] = float(b.mean())
        out[f"seed_bias_range_{m}"] = float(b.max() - b.min())
        out[f"seed_bias_all_same_sign_{m}"] = bool((b > 0).all() or (b < 0).all())
    return out


def value_table(bias: pd.DataFrame, rule4: bool) -> dict:
    """Realised value per filled order at each horizon.

    Reported in half-spreads at entry, not in ticks. A synthetic order resting
    at the inside starts half a spread better than the mid, so a value of +1
    half-spread means the mid did not move and the whole of that edge was
    kept, and 0 means the mid moved exactly far enough to give it all back.
    Expressing it in ticks instead makes the pooled figure a weighted average
    across symbols whose spreads differ by two orders of magnitude.

    A positive value for a buy means the mid ended above what was paid.
    Unfilled orders are worth zero by construction; that half is carried by
    the fill rate, not by this number.
    """
    d = bias[bias["rule4"] == int(rule4)]
    out = {}
    for model in MODEL_ORDER:
        sub = d[d["model"] == model]
        for h in ("1s", "10s", "60s"):
            w = sub[f"valued_{h}"]
            if w.sum() == 0:
                out[f"halfspreads_{model}_{h}"] = float("nan")
                out[f"ticks_{model}_{h}"] = float("nan")
                continue
            out[f"halfspreads_{model}_{h}"] = float(
                (sub[f"value_hs_{h}"] * w).sum() / w.sum()
            )
            out[f"ticks_{model}_{h}"] = float((sub[f"value_{h}"] * w).sum() / w.sum() / 100)
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("study", type=pathlib.Path)
    ap.add_argument("--venue", required=True)
    ap.add_argument("--date", required=True)
    ap.add_argument("--out", type=pathlib.Path, default=pathlib.Path("docs/figures"))
    ap.add_argument("--bootstrap", type=int, default=10000)
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)

    frames_b, frames_t = [], []
    for suffix in ("rule4off", "rule4on"):
        frames_b.append(pd.read_csv(args.study / f"queue_bias_{suffix}.csv"))
        frames_t.append(pd.read_csv(args.study / f"queue_time_to_fill_{suffix}.csv"))
    bias = pd.concat(frames_b, ignore_index=True)
    ttf = pd.concat(frames_t, ignore_index=True)

    for rule4 in (False, True):
        print(f"\n=== rule 4 {'ON' if rule4 else 'OFF'} ===")
        sections = {
            "fill_rate": bias_by_depth(bias, args.out, args.venue, args.date, rule4),
            "symbol_cluster_bootstrap": symbol_bootstrap(args.study, rule4,
                                                         replications=args.bootstrap),
            "seed_spread": seed_spread(args.study, rule4),
            "time_to_fill": time_to_fill(ttf, args.out, args.venue, args.date, rule4),
            "value_per_fill": value_table(bias, rule4),
        }
        for section, values in sections.items():
            print(f"[{section}]")
            for k, v in values.items():
                if isinstance(v, float):
                    print(f"  {k:34s} {v:,.4f}")
                elif isinstance(v, (int, np.integer)):
                    print(f"  {k:34s} {v:,}")
                else:
                    print(f"  {k:34s} {v}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
