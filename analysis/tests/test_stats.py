import numpy as np
import pandas as pd

from systempulse_analysis import compare_runs, load_csv, load_many, resample, summarize, to_wide
from systempulse_analysis.stats import linear_trend, per_worker_table, rolling_stats


def test_summarize_percentiles(steady_csv):
    s = summarize(load_csv(steady_csv), ["cpu/all/busy_pct"])
    row = s.iloc[0]
    assert row["count"] == 60
    assert 18 < row["mean"] < 22
    assert row["min"] <= row["p50"] <= row["p95"] <= row["p99"] <= row["max"]


def test_resample_aligns_series(steady_csv):
    wide = to_wide(load_csv(steady_csv))
    # Collectors write a few ms apart, so the raw pivot is sparse...
    assert wide["cpu/all/busy_pct"].isna().any()
    # ...but resampling to 1s puts every series on the same grid.
    r = resample(wide, "1s")
    assert len(r) == 60
    assert r[["cpu/all/busy_pct", "mem/system/used_pct"]].notna().all().all()


def test_rolling_stats_shapes(steady_csv):
    r = resample(to_wide(load_csv(steady_csv)), "1s")
    out = rolling_stats(r, window=10)
    assert set(out) == {"mean", "std", "max"}
    assert out["mean"].shape == r.shape


def test_linear_trend_slope():
    idx = pd.date_range("2024-01-01", periods=11, freq="1s")
    s = pd.Series(np.arange(11) * 3.0 + 5, index=idx)
    assert abs(linear_trend(s) - 3.0) < 1e-9


def test_per_worker_table_detects_growth(busy_csv):
    t = per_worker_table(load_csv(busy_csv))
    assert len(t) == 2
    assert (t["rss_trend_kb_per_s"] > 4000).all()
    assert (t["cpu_mean_pct"] > 40).all()


def test_compare_runs_has_column_per_run(steady_csv, busy_csv):
    cmp = compare_runs(load_many([steady_csv, busy_csv]))
    assert "busy:mean" in cmp.columns and "steady:p95" in cmp.columns
    assert cmp.loc["cpu/all/busy_pct", "busy:mean"] > cmp.loc["cpu/all/busy_pct", "steady:mean"]
