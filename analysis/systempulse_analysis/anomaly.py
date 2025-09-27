"""Simple, explainable anomaly detection on SystemPulse series."""

from __future__ import annotations

import numpy as np
import pandas as pd


def detect_zscore_anomalies(
    wide: pd.DataFrame,
    window: int = 30,
    threshold: float = 3.0,
    min_std: float = 1e-6,
) -> pd.DataFrame:
    """Flag points that deviate from a trailing rolling baseline.

    The baseline for point *t* uses the ``window`` samples *before* t (shifted by
    one) so a spike can't mask itself. Returns a long frame with columns
    ``time, series, value, baseline_mean, baseline_std, zscore``.
    """
    if wide.empty:
        return _empty()
    baseline = wide.shift(1).rolling(window=window, min_periods=max(3, window // 3))
    mean = baseline.mean()
    std = baseline.std().clip(lower=min_std)
    z = (wide - mean) / std

    mask = z.abs() >= threshold
    if not mask.to_numpy().any():
        return _empty()

    rows = []
    for col in wide.columns:
        hits = mask[col].fillna(False)
        for ts in wide.index[hits.to_numpy()]:
            rows.append(
                {
                    "time": ts,
                    "series": col,
                    "value": wide.at[ts, col],
                    "baseline_mean": mean.at[ts, col],
                    "baseline_std": std.at[ts, col],
                    "zscore": z.at[ts, col],
                }
            )
    out = pd.DataFrame(rows)
    return out.sort_values(["time", "series"]).reset_index(drop=True)


def detect_saturation(
    series: pd.Series,
    threshold: float,
    min_duration: int = 5,
) -> pd.DataFrame:
    """Find sustained stretches where ``series >= threshold`` for at least
    ``min_duration`` consecutive samples (e.g. CPU pinned above 90%).

    Returns ``start, end, samples, peak, mean`` per episode.
    """
    s = series.dropna()
    if s.empty:
        return pd.DataFrame(columns=["start", "end", "samples", "peak", "mean"])
    above = (s >= threshold).to_numpy()
    # Label each contiguous run of equal booleans.
    run_id = np.concatenate([[0], np.cumsum(above[1:] != above[:-1])])
    episodes = []
    for rid in np.unique(run_id[above]):
        idx = np.where(run_id == rid)[0]
        if len(idx) < min_duration:
            continue
        chunk = s.iloc[idx]
        episodes.append(
            {
                "start": chunk.index[0],
                "end": chunk.index[-1],
                "samples": len(idx),
                "peak": float(chunk.max()),
                "mean": float(chunk.mean()),
            }
        )
    return pd.DataFrame(episodes, columns=["start", "end", "samples", "peak", "mean"])


def _empty() -> pd.DataFrame:
    return pd.DataFrame(
        columns=["time", "series", "value", "baseline_mean", "baseline_std", "zscore"]
    )
