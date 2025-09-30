import numpy as np
import pandas as pd

from systempulse_analysis import detect_saturation, detect_zscore_anomalies, load_csv, resample, to_wide


def test_spike_is_flagged(spiky_csv):
    wide = resample(to_wide(load_csv(spiky_csv), source="cpu"), "1s")
    anomalies = detect_zscore_anomalies(wide[["cpu/all/busy_pct"]], window=20, threshold=4.0)
    assert len(anomalies) >= 1
    assert anomalies["value"].max() > 95


def test_steady_series_has_no_anomalies(steady_csv):
    wide = resample(to_wide(load_csv(steady_csv), source="cpu"), "1s")
    anomalies = detect_zscore_anomalies(wide[["cpu/all/busy_pct"]], window=20, threshold=6.0)
    assert anomalies.empty


def test_saturation_episodes():
    idx = pd.date_range("2024-01-01", periods=20, freq="1s")
    values = np.array([10] * 5 + [95] * 6 + [10] * 3 + [99] * 3 + [10] * 3, dtype=float)
    eps = detect_saturation(pd.Series(values, index=idx), threshold=90, min_duration=5)
    assert len(eps) == 1
    assert eps.iloc[0]["samples"] == 6
    assert eps.iloc[0]["peak"] == 95


def test_saturation_on_busy_run(busy_csv):
    wide = resample(to_wide(load_csv(busy_csv), source="cpu"), "1s")
    eps = detect_saturation(wide["cpu/all/busy_pct"], threshold=90)
    assert len(eps) >= 1
