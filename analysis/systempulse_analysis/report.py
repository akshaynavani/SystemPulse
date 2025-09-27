"""Markdown report generation."""

from __future__ import annotations

from pathlib import Path
from typing import Sequence

import pandas as pd

from . import plots
from .anomaly import detect_saturation, detect_zscore_anomalies
from .loader import to_wide
from .stats import HEADLINE_SERIES, compare_runs, per_worker_table, resample, summarize


def _md_table(df: pd.DataFrame, floatfmt: str = "{:.2f}") -> str:
    """Render a DataFrame as a GitHub-flavoured Markdown table without extra deps."""
    if df.empty:
        return "_no data_\n"
    cols = list(df.columns)
    lines = ["| " + " | ".join(str(c) for c in cols) + " |", "|" + "---|" * len(cols)]
    for _, row in df.iterrows():
        cells = []
        for c in cols:
            v = row[c]
            if isinstance(v, float):
                cells.append(floatfmt.format(v) if pd.notna(v) else "")
            else:
                cells.append(str(v))
        lines.append("| " + " | ".join(cells) + " |")
    return "\n".join(lines) + "\n"


def build_report(
    df: pd.DataFrame,
    out_dir: Path,
    freq: str = "1s",
    z_window: int = 30,
    z_threshold: float = 3.5,
    cpu_saturation_pct: float = 90.0,
    make_plots: bool = True,
) -> Path:
    """Analyse every run in ``df`` and write ``report.md`` (+ PNGs, CSVs) to out_dir."""
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    runs: Sequence[str] = sorted(df["run"].unique())

    summary = summarize(df)
    summary.to_csv(out_dir / "summary_stats.csv", index=False)
    workers = per_worker_table(df)
    if not workers.empty:
        workers.to_csv(out_dir / "workers.csv", index=False)

    md = ["# SystemPulse analysis report", ""]
    md.append(f"Runs analysed: {', '.join(runs)}  ")
    md.append(f"Resample interval: `{freq}`, anomaly z-score threshold: `{z_threshold}`")
    md.append("")

    if len(runs) > 1:
        md += ["## Scenario comparison", ""]
        cmp = compare_runs(df).reset_index()
        md.append(_md_table(cmp))
        if make_plots:
            p = plots.plot_run_comparison(summary, out_dir)
            if p:
                md.append(f"![comparison]({p.name})\n")

    all_anomalies = []
    for run in runs:
        md += [f"## Run `{run}`", ""]
        run_df = df[df["run"] == run]
        span = run_df["elapsed_s"].max() if len(run_df) else 0
        md.append(f"Duration: {span:.1f}s, samples: {len(run_df)}\n")

        headline = summary[(summary["run"] == run) & (summary["series"].isin(HEADLINE_SERIES))]
        md += ["### Headline metrics", "", _md_table(headline.drop(columns=["run"]))]

        disks = summary[(summary["run"] == run) & summary["series"].str.match(r"io/.+/util_pct")]
        if not disks.empty:
            md += ["### Disk utilisation", "", _md_table(disks.drop(columns=["run"]))]

        run_workers = workers[workers["run"] == run] if not workers.empty else workers
        if not run_workers.empty:
            md += ["### Workers", "", _md_table(run_workers.drop(columns=["run"]))]
            leaking = run_workers[run_workers["rss_trend_kb_per_s"] > 1024]
            for _, w in leaking.iterrows():
                md.append(
                    f"- **{w['worker']}** RSS grew ~{w['rss_trend_kb_per_s'] / 1024:.1f} MiB/s "
                    "over the run (possible leak or intentional growth)."
                )
            md.append("")

        wide = resample(to_wide(run_df[run_df["series"].isin(HEADLINE_SERIES)]), freq)
        if "cpu/all/busy_pct" in wide:
            sat = detect_saturation(wide["cpu/all/busy_pct"], cpu_saturation_pct, min_duration=5)
            md += [f"### CPU saturation (>= {cpu_saturation_pct:.0f}% for 5+ samples)", ""]
            md.append(_md_table(sat) if not sat.empty else "_none detected_\n")

        anomalies = detect_zscore_anomalies(wide, window=z_window, threshold=z_threshold)
        md += ["### Anomalies", ""]
        if anomalies.empty:
            md.append("_none detected_\n")
        else:
            anomalies.insert(0, "run", run)
            all_anomalies.append(anomalies)
            top = anomalies.reindex(
                anomalies["zscore"].abs().sort_values(ascending=False).index
            ).head(15)
            md.append(_md_table(top.drop(columns=["run"])))

        if make_plots:
            ov = plots.plot_overview(df, run, out_dir, freq)
            md.append(f"![{run} overview]({ov.name})\n")
            pc = plots.plot_per_core(df, run, out_dir, freq)
            if pc:
                md.append(f"![{run} per-core]({pc.name})\n")

    if all_anomalies:
        pd.concat(all_anomalies, ignore_index=True).to_csv(out_dir / "anomalies.csv", index=False)

    path = out_dir / "report.md"
    path.write_text("\n".join(md))
    return path
