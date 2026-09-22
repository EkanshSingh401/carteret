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
}
MODEL_ORDER = ["exact", "conservative", "optimistic", "proportional"]

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
        for section, values in {
            "fill_rate": bias_by_depth(bias, args.out, args.venue, args.date, rule4),
            "time_to_fill": time_to_fill(ttf, args.out, args.venue, args.date, rule4),
            "value_per_fill_price4_units": value_table(bias, rule4),
        }.items():
            print(f"[{section}]")
            for k, v in values.items():
                print(f"  {k:34s} {v:,.4f}" if isinstance(v, float) else f"  {k:34s} {v:,}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
