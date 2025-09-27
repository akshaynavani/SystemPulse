"""Descriptive statistics and resampling over SystemPulse time series."""

from __future__ import annotations

from typing import Iterable, Optional

import numpy as np
import pandas as pd

from .loader import to_wide

PERCENTILES = (0.5, 0.95, 0.99)


def summarize(df: pd.DataFrame, series: Optional[Iterable[str]] = None) -> pd.DataFrame:
    """Per-(run, series) count/mean/std/min/p50/p95/p99/max.

    ``df`` is a long frame from :func:`load_csv` / :func:`load_many`.
    """
    sub = df if series is None else df[df["series"].isin(list(series))]
    if sub.empty:
        return pd.DataFrame(
            columns=["run", "series", "count", "mean", "std", "min", "p50", "p95", "p99", "max"]
        )

    grouped = sub.groupby(["run", "series"])["value"]
    out = grouped.agg(["count", "mean", "std", "min", "max"])
    for p in PERCENTILES:
        out[f"p{int(p * 100)}"] = grouped.quantile(p)
    out = out[["count", "mean", "std", "min", "p50", "p95", "p99", "max"]]
    out["std"] = out["std"].fillna(0.0)
    return out.reset_index()


def resample(wide: pd.DataFrame, freq: str = "1s", how: str = "mean") -> pd.DataFrame:
    """Align a wide, time-indexed frame onto a regular grid.

    Gaps up to two buckets are forward-filled (a collector might miss a beat
    under load); longer gaps stay NaN so they show up as breaks in plots.
    """
    if wide.empty:
        return wide
    if not isinstance(wide.index, pd.DatetimeIndex):
        raise TypeError("resample() needs a DatetimeIndex; use to_wide(..., index='time')")
    agg = getattr(wide.resample(freq), how)()
    return agg.ffill(limit=2)


def rolling_stats(wide: pd.DataFrame, window: int = 10) -> dict[str, pd.DataFrame]:
    """Rolling mean / std / max over ``window`` samples for every column."""
    roll = wide.rolling(window=window, min_periods=max(1, window // 2))
    return {"mean": roll.mean(), "std": roll.std(), "max": roll.max()}


def rate_of_change(wide: pd.DataFrame) -> pd.DataFrame:
    """Per-second first derivative, useful for spotting memory leaks (RSS growth)."""
    if wide.empty:
        return wide
    dt = wide.index.to_series().diff().dt.total_seconds().replace(0, np.nan)
    return wide.diff().div(dt, axis=0)


def linear_trend(series: pd.Series) -> float:
    """Least-squares slope (units per second) of a time-indexed series."""
    s = series.dropna()
    if len(s) < 2:
        return 0.0
    t = (s.index - s.index[0]).total_seconds().to_numpy()
    if np.ptp(t) == 0:
        return 0.0
    slope, _ = np.polyfit(t, s.to_numpy(dtype=float), 1)
    return float(slope)


HEADLINE_SERIES = [
    "cpu/all/busy_pct",
    "cpu/all/iowait_pct",
    "mem/system/used_pct",
    "load/system/load1",
    "sys/system/ctx_switches_per_sec",
    "proc/workers/cpu_pct",
    "proc/workers/rss_kb",
    "proc/workers/disk_write_bps",
]


def compare_runs(df: pd.DataFrame, series: Iterable[str] = HEADLINE_SERIES) -> pd.DataFrame:
    """Mean and p95 of headline series side by side for each run."""
    summary = summarize(df, series)
    if summary.empty:
        return summary
    table = summary.pivot(index="series", columns="run", values=["mean", "p95"])
    table.columns = [f"{run}:{stat}" for stat, run in table.columns]
    return table.reindex(sorted(table.columns), axis=1)


def per_worker_table(df: pd.DataFrame) -> pd.DataFrame:
    """One row per (run, worker) with mean/max CPU, peak RSS, total IO and RSS trend."""
    proc = df[(df["source"] == "proc") & (df["entity"].str.startswith("pid:"))]
    if proc.empty:
        return pd.DataFrame()
    rows = []
    for (run, entity), g in proc.groupby(["run", "entity"]):
        def metric(name: str) -> pd.Series:
            m = g[g["metric"] == name]
            return pd.Series(m["value"].to_numpy(), index=m["time"])

        cpu = metric("cpu_pct")
        rss = metric("rss_kb")
        wr = metric("disk_write_bps")
        rd = metric("disk_read_bps")
        span = g["elapsed_s"].max() - g["elapsed_s"].min()
        rows.append(
            {
                "run": run,
                "worker": entity,
                "samples": int(len(cpu)),
                "cpu_mean_pct": cpu.mean() if len(cpu) else np.nan,
                "cpu_max_pct": cpu.max() if len(cpu) else np.nan,
                "rss_peak_mb": rss.max() / 1024 if len(rss) else np.nan,
                "rss_trend_kb_per_s": linear_trend(rss),
                "disk_read_mb": rd.mean() * span / 2**20 if len(rd) else 0.0,
                "disk_write_mb": wr.mean() * span / 2**20 if len(wr) else 0.0,
            }
        )
    return pd.DataFrame(rows).sort_values(["run", "worker"]).reset_index(drop=True)


def headline_wide(df: pd.DataFrame, run: str, freq: str = "1s") -> pd.DataFrame:
    """Resampled wide frame of headline series for a single run."""
    sub = df[(df["run"] == run) & (df["series"].isin(HEADLINE_SERIES))]
    return resample(to_wide(sub), freq)
