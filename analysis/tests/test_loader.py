import pandas as pd
import pytest

from systempulse_analysis import load_csv, load_many, series_name, to_wide


def test_load_csv_adds_derived_columns(steady_csv):
    df = load_csv(steady_csv)
    assert {"time", "elapsed_s", "series", "run"} <= set(df.columns)
    assert df["run"].unique().tolist() == ["steady"]
    assert df["elapsed_s"].min() == 0.0
    assert pd.api.types.is_datetime64_any_dtype(df["time"])
    assert "cpu/all/busy_pct" in set(df["series"])


def test_load_csv_skips_bad_rows(tmp_path):
    p = tmp_path / "bad.csv"
    p.write_text(
        "timestamp_ms,source,entity,metric,value\n"
        "1000,cpu,all,busy_pct,10\n"
        "oops,cpu,all,busy_pct,10\n"
        "2000,cpu,all,busy_pct,nan-ish\n"
        '3000,proc,"pid:1:a,b",cpu_pct,5\n'
    )
    df = load_csv(p)
    assert len(df) == 2
    assert "pid:1:a,b" in set(df["entity"])


def test_load_csv_rejects_wrong_schema(tmp_path):
    p = tmp_path / "other.csv"
    p.write_text("a,b\n1,2\n")
    with pytest.raises(ValueError):
        load_csv(p)


def test_to_wide_pivots_series(steady_csv):
    df = load_csv(steady_csv)
    wide = to_wide(df, source="cpu")
    assert series_name("cpu", "all", "busy_pct") in wide.columns
    assert series_name("cpu", "cpu1", "busy_pct") in wide.columns
    assert not any(c.startswith("mem/") for c in wide.columns)


def test_load_many_keeps_runs_apart(steady_csv, spiky_csv):
    df = load_many([steady_csv, spiky_csv])
    assert sorted(df["run"].unique()) == ["spiky", "steady"]
