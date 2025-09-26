"""Time-series analysis for SystemPulse CSV output."""

from .loader import load_csv, load_many, to_wide, series_name
from .stats import summarize, resample, rolling_stats, compare_runs
from .anomaly import detect_zscore_anomalies, detect_saturation

__all__ = [
    "load_csv",
    "load_many",
    "to_wide",
    "series_name",
    "summarize",
    "resample",
    "rolling_stats",
    "compare_runs",
    "detect_zscore_anomalies",
    "detect_saturation",
]

__version__ = "0.1.0"
