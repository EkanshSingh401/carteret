"""Shared machinery for the pre-registered signal study.

Named ``study`` rather than ``signal``: a module named ``signal.py`` beside a
script shadows the standard library's ``signal`` module for anything on that
path, including libraries that import it indirectly.

Loading, metrics, cluster-robust standard errors, a session block bootstrap,
and the minimum detectable effect. Kept in one module so that the development
power analysis and the held-out run compute the same quantities the same way —
if they diverged, the registered threshold would not be the one tested.

Nothing here reads a held-out session. `research/run_heldout.sh` is the only
entry point that may, and it refuses until the registration is locked.
"""

from __future__ import annotations

import dataclasses
import pathlib
import subprocess
from typing import Iterable

import numpy as np
import pandas as pd

# Features, as registered in docs/preregistration.md section 5. Micro-price
# deviation is in ticks, not half-spreads: normalised it is algebraically
# identical to queue imbalance (docs/design.md record 031).
FEATURES = (
    "ofi",
    "queue_imbalance",
    "micro_dev_ticks",
    "trade_sign",
)

LABEL = "label_ticks"


def export_features(session: pathlib.Path, out_csv: pathlib.Path, window: int = 50,
                    symbols: int = 50,
                    binary: pathlib.Path = pathlib.Path("build/release/export_features")) -> None:
    """Runs the C++ exporter. Kept here so the Python side never reimplements
    a feature definition that the registered document points at the C++ for."""
    out_csv.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        [str(binary), "--window", str(window), "--symbols", str(symbols),
         "--out", str(out_csv), str(session)],
        check=True,
    )


def load(paths: Iterable[pathlib.Path]) -> pd.DataFrame:
    """Loads feature files, tagging each with the session it came from.

    The session tag is the cluster unit. Intraday rows within a session are
    heavily autocorrelated, so treating them as independent would understate
    every standard error by an order of magnitude and is the single easiest
    way to manufacture a significant result from nothing.
    """
    frames = []
    for p in paths:
        df = pd.read_csv(p)
        df["session"] = pathlib.Path(p).stem
        frames.append(df)
    if not frames:
        raise SystemExit("no feature files given")
    return pd.concat(frames, ignore_index=True)


@dataclasses.dataclass
class Estimate:
    """A metric with both standard errors the registration requires."""

    value: float
    n: int
    clusters: int
    se_clustered: float
    se_bootstrap: float
    ci_low: float
    ci_high: float

    def excludes(self, null: float) -> bool:
        return (self.ci_low > null) or (self.ci_high < null)


def _per_row_contribution(df: pd.DataFrame, feature: str, metric: str) -> np.ndarray:
    """Turns a metric into a per-row quantity whose mean is the metric.

    Writing every metric this way means one clustering routine covers all of
    them, rather than a separate variance derivation per metric.
    """
    if metric == "directional_accuracy":
        # Rows with a zero label carry no directional information and are
        # excluded rather than counted as half-right, which would pull every
        # accuracy toward 50% by construction.
        mask = df[LABEL] != 0
        sub = df[mask]
        return (np.sign(sub[feature]) == np.sign(sub[LABEL])).to_numpy(dtype=float), mask
    raise ValueError(f"unknown metric {metric!r}")


def direct_value(df: pd.DataFrame, feature: str) -> tuple[np.ndarray, int, int]:
    """The direct-value secondary's per-window summand, section 3.

        V = mean( Δ · sign(feature) )   over windows with a nonzero move

    Returns the per-window terms, the number of windows, and how many of them
    had a feature of exactly zero.

    **Zero-feature windows contribute zero and stay in the denominator.** That
    is what the formula says and it is what the economic requirement means: the
    requirement is 0.10 half-spreads *per window*, and a window in which the
    signal declined to take a side earned nothing in it. Dropping such windows
    would measure the value of the signal *when it fires*, which is a different
    and easier quantity -- the strategy still sat through those windows.

    This differs deliberately from the primary directional metric, which scores
    a zero feature as a miss (see the leakage audit). Both treatments are
    conservative in the same direction; neither flatters the signal. They
    differ because a miss and a zero are the right answers to different
    questions: "was the side right" has no answer when no side was taken, while
    "what was earned" has the answer zero.

    **Zero-LABEL windows also stay in the denominator**, contributing zero,
    for the same reason. This corrects the section-3 definition, which said
    "over windows with a nonzero move": that conditions the average on the
    move while testing it against a requirement stated *per window*, and so
    divides by the fraction of windows that move -- the identical error that
    was corrected in *m*. Both corrections are in the stricter direction.
    """
    sign = np.sign(df[feature].to_numpy(dtype=float))   # exactly 0 stays 0
    delta = df["label_halfspreads"].to_numpy(dtype=float)  # zero when no move
    terms = delta * sign
    return terms, len(terms), int((sign == 0).sum())


def clustered_mean(values: np.ndarray, clusters: np.ndarray) -> tuple[float, float, int]:
    """Mean with a cluster-robust standard error.

    SE = sqrt( sum_g ( sum_{i in g} (x_i - xbar) )^2 ) / n, the standard
    one-way cluster-robust estimator for a sample mean. With one cluster it is
    undefined and returns NaN rather than a small number that looks like
    precision.
    """
    n = len(values)
    if n == 0:
        return float("nan"), float("nan"), 0
    xbar = float(values.mean())
    frame = pd.DataFrame({"d": values - xbar, "g": clusters})
    per_cluster = frame.groupby("g", sort=False)["d"].sum().to_numpy()
    g = len(per_cluster)
    if g < 2:
        return xbar, float("nan"), g
    # Small-cluster correction, as used by common cluster-robust software.
    correction = g / (g - 1)
    se = float(np.sqrt(correction * np.sum(per_cluster ** 2)) / n)
    return xbar, se, g


def session_block_bootstrap(df: pd.DataFrame, values: np.ndarray, sessions: np.ndarray,
                            replications: int = 10000, seed: int = 20190130) -> float:
    """Standard error from resampling whole sessions with replacement.

    Resampling rows would destroy the dependence the clustering exists to
    respect. Resampling whole sessions keeps each session's internal structure
    intact and treats the session as the unit of independent information,
    which is what the effective sample size actually is.
    """
    del df
    rng = np.random.default_rng(seed)
    unique = np.unique(sessions)
    if len(unique) < 2:
        return float("nan")
    by_session = {s: values[sessions == s] for s in unique}
    draws = np.empty(replications, dtype=float)
    for r in range(replications):
        picked = rng.choice(unique, size=len(unique), replace=True)
        pooled = np.concatenate([by_session[s] for s in picked])
        draws[r] = pooled.mean()
    return float(draws.std(ddof=1))


def estimate(df: pd.DataFrame, feature: str, metric: str = "directional_accuracy",
             cluster_on: str = "session_symbol", bootstrap: int = 10000) -> Estimate:
    """The registered metric with both standard errors and a 95% interval."""
    values, mask = _per_row_contribution(df, feature, metric)
    sub = df[mask]

    if cluster_on == "session_symbol":
        clusters = (sub["session"].astype(str) + "|" + sub["symbol"].astype(str)).to_numpy()
    elif cluster_on == "session":
        clusters = sub["session"].astype(str).to_numpy()
    else:
        raise ValueError(f"unknown cluster unit {cluster_on!r}")

    value, se, n_clusters = clustered_mean(values, clusters)
    se_boot = session_block_bootstrap(sub, values, sub["session"].astype(str).to_numpy(),
                                      replications=bootstrap)

    # The interval uses the larger of the two standard errors. They estimate
    # the same quantity by different routes, and taking the smaller one
    # whenever they disagree would be choosing the answer.
    se_use = np.nanmax([se, se_boot])
    return Estimate(
        value=value,
        n=len(values),
        clusters=n_clusters,
        se_clustered=se,
        se_bootstrap=se_boot,
        ci_low=value - 1.959963985 * se_use,
        ci_high=value + 1.959963985 * se_use,
    )


def minimum_detectable_effect(se_development: float, clusters_development: int,
                              clusters_heldout: int, alpha: float = 0.05,
                              power: float = 0.80) -> float:
    """MDE at the planned held-out size, two-sided.

    The development standard error is scaled by the square root of the ratio of
    cluster counts, because the standard error of a mean falls with the square
    root of the number of INDEPENDENT units, and the independent unit here is
    the cluster rather than the row.
    """
    if not np.isfinite(se_development) or clusters_heldout <= 0 or clusters_development <= 0:
        return float("nan")
    z_alpha = 1.959963985  # two-sided 0.05
    z_power = 0.841621234  # 0.80
    se_heldout = se_development * np.sqrt(clusters_development / clusters_heldout)
    return float((z_alpha + z_power) * se_heldout)


def describe(name: str, est: Estimate, null: float) -> str:
    return (
        f"  {name:28s} {est.value:9.5f}  n={est.n:>9,}  clusters={est.clusters:>5,}\n"
        f"  {'':28s} SE clustered {est.se_clustered:.5f}  "
        f"SE bootstrap {est.se_bootstrap:.5f}\n"
        f"  {'':28s} 95% CI [{est.ci_low:.5f}, {est.ci_high:.5f}]  "
        f"excludes {null:g}: {'yes' if est.excludes(null) else 'no'}"
    )
