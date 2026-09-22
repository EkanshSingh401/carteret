#!/usr/bin/env python3
"""Figures and findings from the aggregates written by ``export_micro``.

Stage 6. Reads the four CSV files ``export_micro`` produces and writes figures
to ``docs/figures``. The CSVs derive from market data and are gitignored; the
figures are aggregates over many symbols and are committed.

Each figure covers one venue. BX and NASDAQ are never drawn on the same axes:
BX is taker-maker and NASDAQ maker-taker, the two attract different order
flow, and a combined panel would invite exactly the pooling that
``docs/design.md`` record 022 forbids.

    usage: research/microstructure.py <aggregate-dir> --venue BX --date 2019-01-30
"""

from __future__ import annotations

import argparse
import pathlib
import sys

import matplotlib

matplotlib.use("Agg")  # no display on the machines this runs on
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402
import pandas as pd  # noqa: E402

# Figures are read in both light and dark contexts and printed. A single
# colour cycle chosen once keeps every figure in this directory consistent.
INK = "#1b1b1b"
ACCENT = "#0b6e99"
ACCENT2 = "#b8500e"
MUTED = "#8a8a8a"

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


def _stamp(fig, venue: str, date: str) -> None:
    """Every figure carries its venue and session, so it cannot be misfiled."""
    fig.text(
        0.99,
        -0.02,
        f"{venue} {date} · carteret",
        ha="right",
        va="top",
        fontsize=7,
        color=MUTED,
    )


def spread_and_depth(agg: pathlib.Path, out: pathlib.Path, venue: str, date: str) -> dict:
    """Spread and depth by minute of the session, averaged across the universe.

    Weighted by sample count rather than by symbol, so a symbol that is
    two-sided for only part of the day does not carry a whole minute.
    """
    df = pd.read_csv(agg / "micro_spread_depth.csv")
    df = df[df["two_sided_samples"] > 0]

    g = df.groupby("minute_of_session")
    w = df["two_sided_samples"]

    def wmean(col: str) -> pd.Series:
        num = (df[col] * w).groupby(df["minute_of_session"]).sum()
        den = w.groupby(df["minute_of_session"]).sum()
        return num / den

    spread = wmean("mean_spread_ticks")
    one_tick = wmean("one_tick_fraction")
    bid_sh = wmean("mean_bid_shares")
    ask_sh = wmean("mean_ask_shares")
    n_sym = g["symbol"].nunique()

    fig, axes = plt.subplots(3, 1, figsize=(7.2, 7.6), sharex=True)
    hours = spread.index / 60.0 + 9.5

    axes[0].plot(hours, spread.to_numpy(), color=ACCENT, lw=1.4)
    axes[0].set_ylabel("mean spread (ticks)")
    axes[0].set_title(f"Inside spread and depth by time of day — {venue} {date}")
    axes[0].grid(True, axis="y")

    axes[1].plot(hours, one_tick.to_numpy() * 100, color=ACCENT2, lw=1.4)
    axes[1].set_ylabel("one-tick spread (% of samples)")
    axes[1].grid(True, axis="y")

    axes[2].plot(hours, bid_sh.to_numpy(), color=ACCENT, lw=1.3, label="bid")
    axes[2].plot(hours, ask_sh.to_numpy(), color=ACCENT2, lw=1.3, label="ask")
    axes[2].set_ylabel("mean shares at inside")
    axes[2].set_xlabel("time of day (exchange hours)")
    axes[2].legend(frameon=False, loc="upper center", ncol=2)
    axes[2].grid(True, axis="y")
    axes[2].set_xticks([9.5, 11, 12.5, 14, 15.5, 16])
    axes[2].set_xticklabels(["09:30", "11:00", "12:30", "14:00", "15:30", "16:00"])

    _stamp(fig, venue, date)
    fig.savefig(out / f"spread_depth_{venue.lower()}_{date}.png")
    plt.close(fig)

    # The numbers the README quotes, so prose and figure cannot drift.
    first30 = spread.loc[spread.index < 30]
    last30 = spread.loc[spread.index >= 360]
    midday = spread.loc[(spread.index >= 150) & (spread.index < 240)]
    return {
        "symbols": int(n_sym.max()),
        "spread_open_30min": float(first30.mean()),
        "spread_midday": float(midday.mean()),
        "spread_close_30min": float(last30.mean()),
        "spread_session": float((spread * n_sym).sum() / n_sym.sum()),
        "one_tick_session_pct": float(one_tick.mean() * 100),
        "depth_open_30min": float(((bid_sh + ask_sh) / 2).loc[spread.index < 30].mean()),
        "depth_close_30min": float(((bid_sh + ask_sh) / 2).loc[spread.index >= 360].mean()),
    }


def lifetimes(agg: pathlib.Path, out: pathlib.Path, venue: str, date: str) -> dict:
    """Order lifetime distribution, split by how the order left the book."""
    df = pd.read_csv(agg / "micro_lifetimes.csv")
    total = df["count"].sum()

    order = ["cancelled", "replaced", "filled", "partial_then_gone"]
    labels = {
        "cancelled": "cancelled untouched",
        "replaced": "replaced",
        "filled": "fully filled",
        "partial_then_gone": "partly filled, then gone",
    }
    colours = {
        "cancelled": MUTED,
        "replaced": "#5b8db8",
        "filled": ACCENT2,
        "partial_then_gone": "#d8a13a",
    }

    fig, ax = plt.subplots(figsize=(7.2, 4.0))
    for exit_kind in order:
        sub = df[df["exit"] == exit_kind].sort_values("bucket")
        if sub.empty:
            continue
        ax.step(
            sub["low_ns"] / 1e9,
            sub["count"] / total * 100,
            where="post",
            lw=1.5,
            color=colours[exit_kind],
            label=f"{labels[exit_kind]} ({sub['count'].sum() / total * 100:.1f}%)",
        )
    ax.set_xscale("log")
    ax.set_xlabel("time resting in the book (seconds, log scale)")
    ax.set_ylabel("% of all removed orders")
    ax.set_title(f"Order lifetime by exit reason — {venue} {date}")
    ax.legend(frameon=False, fontsize=8)
    ax.grid(True, axis="y")
    _stamp(fig, venue, date)
    fig.savefig(out / f"lifetimes_{venue.lower()}_{date}.png")
    plt.close(fig)

    by_exit = df.groupby("exit")["count"].sum()

    def share(name: str) -> float:
        return float(by_exit.get(name, 0) / total * 100)

    # Median lifetime, interpolated within the log bucket that contains it.
    def median_ns(sub: pd.DataFrame) -> float:
        if sub.empty:
            return float("nan")
        s = sub.sort_values("bucket")
        cum = s["count"].cumsum() / s["count"].sum()
        row = s[cum >= 0.5].iloc[0]
        return float(np.sqrt(row["low_ns"] * row["high_ns"]))

    return {
        "removed_orders": int(total),
        "pct_cancelled": share("cancelled"),
        "pct_replaced": share("replaced"),
        "pct_filled": share("filled"),
        "pct_partial": share("partial_then_gone"),
        "median_life_cancelled_s": median_ns(df[df["exit"] == "cancelled"]) / 1e9,
        "median_life_filled_s": median_ns(df[df["exit"] == "filled"]) / 1e9,
        "pct_under_1s": float(df[df["low_ns"] < 1e9]["count"].sum() / total * 100),
    }


def order_sizes(agg: pathlib.Path, out: pathlib.Path, venue: str, date: str) -> dict:
    """Order size distribution, with the round-lot structure kept visible."""
    df = pd.read_csv(agg / "micro_order_sizes.csv")
    df = df.dropna(subset=["shares_high"])
    total = df["count"].sum()

    # A histogram is the wrong form here: one size holds most of the mass and
    # a 1-share-wide bar on a 1,000-wide axis renders as a single pixel, so the
    # dominant value disappears. The interesting structure is *which* sizes
    # occur, so the common ones are named.
    top = df.sort_values("count", ascending=False).head(12).sort_values("count")

    fig, axes = plt.subplots(1, 2, figsize=(9.6, 4.0))

    pct = top["count"] / total * 100
    colours = [
        ACCENT if int(v) % 100 == 0 else MUTED for v in top["shares_low"]
    ]
    axes[0].barh([str(int(v)) for v in top["shares_low"]], pct, color=colours, linewidth=0)
    axes[0].set_xlabel("% of orders")
    axes[0].set_ylabel("order size (shares)")
    axes[0].set_title("The twelve most common sizes")
    axes[0].grid(True, axis="x")
    for y, v in enumerate(pct):
        axes[0].text(v + max(pct) * 0.01, y, f"{v:.1f}%", va="center", fontsize=7.5)
    axes[0].set_xlim(0, max(pct) * 1.18)

    # Round lots as a share of everything, which is the stylized fact of
    # interest and is invisible on a log-binned histogram.
    round_lots = df[(df["shares_low"] % 100 == 0) & (df["shares_low"] > 0)]["count"].sum()
    odd_lots = df[df["shares_low"] < 100]["count"].sum()
    mixed = total - round_lots - odd_lots

    bars = axes[1].bar(
        ["odd lot\n(<100)", "round lot\n(multiple of 100)", "mixed lot"],
        [odd_lots / total * 100, round_lots / total * 100, mixed / total * 100],
        color=[MUTED, ACCENT, ACCENT2],
        linewidth=0,
    )
    for b in bars:
        axes[1].text(
            b.get_x() + b.get_width() / 2,
            b.get_height() + 1.5,
            f"{b.get_height():.1f}%",
            ha="center",
            fontsize=8,
        )
    axes[1].set_ylabel("% of orders")
    axes[1].set_ylim(0, 105)
    axes[1].set_title("Lot structure")
    axes[1].grid(True, axis="y")

    fig.suptitle(f"Order size distribution — {venue} {date}", fontweight="bold")
    _stamp(fig, venue, date)
    fig.savefig(out / f"order_sizes_{venue.lower()}_{date}.png")
    plt.close(fig)

    # The most common single size, which the round-lot story predicts is 100.
    modal = df.sort_values("count", ascending=False).iloc[0]
    return {
        "orders": int(total),
        "pct_odd_lot": float(odd_lots / total * 100),
        "pct_round_lot": float(round_lots / total * 100),
        "pct_mixed_lot": float(mixed / total * 100),
        "modal_size": int(modal["shares_low"]),
        "modal_pct": float(modal["count"] / total * 100),
        "pct_exactly_100": float(
            df[df["shares_low"] == 100]["count"].sum() / total * 100
        ),
    }


def cancel_to_trade(agg: pathlib.Path, out: pathlib.Path, venue: str, date: str) -> dict:
    """Cancel-to-trade ratio across the symbol universe.

    Counted in orders, not shares, and with replaces separated from cancels:
    a replace removes an order without removing the interest behind it, so
    pooling the two overstates cancellation.
    """
    df = pd.read_csv(agg / "micro_symbols.csv")
    df = df[df["adds"] > 0]
    active = df[(df["executes"] > 0) & (df["adds"] >= 1000)].copy()
    active["ctt"] = (active["deletes"] + active["cancels"]) / active["executes"]
    active["ctt_with_replace"] = (
        active["deletes"] + active["cancels"] + active["replaces"]
    ) / active["executes"]

    fig, ax = plt.subplots(figsize=(7.2, 4.0))
    bins = np.logspace(0, 3.2, 45)
    ax.hist(
        active["ctt"],
        bins=bins,
        color=ACCENT,
        alpha=0.85,
        linewidth=0,
        label="cancels and deletes",
    )
    ax.hist(
        active["ctt_with_replace"],
        bins=bins,
        histtype="step",
        color=ACCENT2,
        lw=1.4,
        label="including replaces",
    )
    ax.set_xscale("log")
    ax.set_xlabel("cancel-to-trade ratio (orders removed per execution, log scale)")
    ax.set_ylabel("symbols")
    ax.set_title(
        f"Cancel-to-trade ratio across {len(active)} symbols — {venue} {date}"
    )
    ax.legend(frameon=False, fontsize=8)
    ax.grid(True, axis="y")
    _stamp(fig, venue, date)
    fig.savefig(out / f"cancel_to_trade_{venue.lower()}_{date}.png")
    plt.close(fig)

    return {
        "symbols_considered": int(len(active)),
        "ctt_median": float(active["ctt"].median()),
        "ctt_p25": float(active["ctt"].quantile(0.25)),
        "ctt_p75": float(active["ctt"].quantile(0.75)),
        "ctt_with_replace_median": float(active["ctt_with_replace"].median()),
        "symbols_no_execution": int((df["executes"] == 0).sum()),
        "symbols_total": int(len(df)),
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("aggregates", type=pathlib.Path)
    ap.add_argument("--venue", required=True)
    ap.add_argument("--date", required=True)
    ap.add_argument("--out", type=pathlib.Path, default=pathlib.Path("docs/figures"))
    args = ap.parse_args()

    args.out.mkdir(parents=True, exist_ok=True)

    findings: dict[str, dict] = {
        "spread_depth": spread_and_depth(args.aggregates, args.out, args.venue, args.date),
        "lifetimes": lifetimes(args.aggregates, args.out, args.venue, args.date),
        "order_sizes": order_sizes(args.aggregates, args.out, args.venue, args.date),
        "cancel_to_trade": cancel_to_trade(args.aggregates, args.out, args.venue, args.date),
    }

    print(f"venue {args.venue}  session {args.date}")
    for section, values in findings.items():
        print(f"\n[{section}]")
        for k, v in values.items():
            print(f"  {k:28s} {v:,.4f}" if isinstance(v, float) else f"  {k:28s} {v:,}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
