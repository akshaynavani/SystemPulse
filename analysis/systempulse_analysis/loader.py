"""Loading SystemPulse long-format CSVs and reshaping them into time series.

CSV schema (one row per measurement)::

    timestamp_ms,source,entity,metric,value
"""

from __future__ import annotations

from pathlib import Path
from typing import Iterable, Optional

import pandas as pd

COLUMNS = ["timestamp_ms", "source", "entity", "metric", "value"]


def series_name(source: str, entity: str, metric: str) -> str:
    """Canonical column name for one series, e.g. ``cpu/all/busy_pct``."""
    return f"{source}/{entity}/{metric}"


def load_csv(path: str | Path, run: Optional[str] = None) -> pd.DataFrame:
    """Read one SystemPulse CSV into a tidy long DataFrame.

    Adds ``time`` (UTC datetime), ``elapsed_s`` (seconds since first sample),
    ``series`` and ``run`` columns. Malformed rows are dropped.
    """
    path = Path(path)
    df = pd.read_csv(
        path,
        dtype={"source": "string", "entity": "string", "metric": "string"},
        on_bad_lines="skip",
    )
    missing = set(COLUMNS) - set(df.columns)
    if missing:
        raise ValueError(f"{path}: not a SystemPulse CSV (missing columns {sorted(missing)})")

    df["timestamp_ms"] = pd.to_numeric(df["timestamp_ms"], errors="coerce")
    df["value"] = pd.to_numeric(df["value"], errors="coerce")
    df = df.dropna(subset=["timestamp_ms", "value"]).copy()
    df["timestamp_ms"] = df["timestamp_ms"].astype("int64")

    df["time"] = pd.to_datetime(df["timestamp_ms"], unit="ms", utc=True)
    start = df["timestamp_ms"].min() if len(df) else 0
    df["elapsed_s"] = (df["timestamp_ms"] - start) / 1000.0
    df["series"] = df["source"] + "/" + df["entity"] + "/" + df["metric"]
    df["run"] = run if run is not None else path.stem
    return df.sort_values("timestamp_ms").reset_index(drop=True)


def load_many(paths: Iterable[str | Path]) -> pd.DataFrame:
    """Load several runs into one long frame distinguished by the ``run`` column."""
    frames = [load_csv(p) for p in paths]
    if not frames:
        return pd.DataFrame(columns=COLUMNS + ["time", "elapsed_s", "series", "run"])
    return pd.concat(frames, ignore_index=True)


def to_wide(
    df: pd.DataFrame,
    source: Optional[str] = None,
    metric: Optional[str] = None,
    index: str = "time",
) -> pd.DataFrame:
    """Pivot a long frame into a wide one: one column per series.

    Collectors run on independent threads, so samples of different series are
    a few milliseconds apart; they stay as separate index rows here and callers
    can :func:`systempulse_analysis.stats.resample` to align them.
    """
    sub = df
    if source is not None:
        sub = sub[sub["source"] == source]
    if metric is not None:
        sub = sub[sub["metric"] == metric]
    if sub.empty:
        return pd.DataFrame()
    wide = sub.pivot_table(index=index, columns="series", values="value", aggfunc="mean")
    wide.columns.name = None
    return wide.sort_index()


def filter_series(df: pd.DataFrame, pattern: str) -> pd.DataFrame:
    """Rows whose ``series`` matches a regular expression."""
    return df[df["series"].str.contains(pattern, regex=True, na=False)]
