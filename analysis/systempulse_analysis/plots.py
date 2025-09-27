"""Matplotlib figures for SystemPulse runs (rendered headless)."""

from __future__ import annotations

from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import pandas as pd  # noqa: E402

from .loader import to_wide  # noqa: E402
from .stats import resample  # noqa: E402

_MB = 1024.0 * 1024.0


def _elapsed_index(wide: pd.DataFrame) -> pd.DataFrame:
    if wide.empty:
        return wide
    out = wide.copy()
    out.index = (out.index - out.index[0]).total_seconds()
    return out


def _panel(ax, wide: pd.DataFrame, title: str, ylabel: str, scale: float = 1.0) -> None:
    ax.set_title(title, fontsize=10, loc="left")
    ax.set_ylabel(ylabel, fontsize=9)
    ax.grid(True, alpha=0.3)
    if wide.empty:
        ax.text(0.5, 0.5, "no data", ha="center", va="center", transform=ax.transAxes)
        return
    for col in wide.columns:
        ax.plot(wide.index, wide[col] / scale, label=col.split("/", 1)[1], linewidth=1.2)
    if len(wide.columns) <= 8:
        ax.legend(fontsize=7, loc="upper right")


def plot_overview(df: pd.DataFrame, run: str, out_dir: Path, freq: str = "1s") -> Path:
    """Four-panel overview: CPU, memory, disk throughput, worker CPU."""
    sub = df[df["run"] == run]

    def wide_for(pattern: str) -> pd.DataFrame:
        part = sub[sub["series"].str.fullmatch(pattern, na=False)]
        return _elapsed_index(resample(to_wide(part), freq))

    cpu = wide_for(r"cpu/all/(busy|user|system|iowait)_pct")
    mem = wide_for(r"mem/system/used_pct|load/system/load1")
    io = wide_for(r"io/[^/]+/(read|write)_bps")
    workers = wide_for(r"proc/pid:[^/]+/cpu_pct")

    fig, axes = plt.subplots(4, 1, figsize=(11, 11), sharex=True)
    _panel(axes[0], cpu, f"{run}: CPU utilisation", "%")
    _panel(axes[1], mem, "Memory used % / load average", "")
    _panel(axes[2], io, "Disk throughput", "MiB/s", scale=_MB)
    _panel(axes[3], workers, "Per-worker CPU", "% of one core")
    axes[-1].set_xlabel("elapsed (s)")
    fig.tight_layout()

    out_dir.mkdir(parents=True, exist_ok=True)
    path = out_dir / f"{run}_overview.png"
    fig.savefig(path, dpi=110)
    plt.close(fig)
    return path


def plot_per_core(df: pd.DataFrame, run: str, out_dir: Path, freq: str = "1s") -> Path | None:
    """Heatmap of per-core busy % over time, which shows scheduler placement."""
    sub = df[(df["run"] == run) & (df["source"] == "cpu") & (df["metric"] == "busy_pct")
             & (df["entity"] != "all")]
    if sub.empty:
        return None
    wide = _elapsed_index(resample(to_wide(sub), freq))
    cols = sorted(wide.columns, key=lambda c: int(c.split("/")[1].removeprefix("cpu") or 0))
    wide = wide[cols]

    fig, ax = plt.subplots(figsize=(11, max(2.5, 0.3 * len(cols))))
    im = ax.imshow(
        wide.T.to_numpy(),
        aspect="auto",
        cmap="viridis",
        vmin=0,
        vmax=100,
        extent=[wide.index[0], wide.index[-1], len(cols) - 0.5, -0.5],
        interpolation="nearest",
    )
    ax.set_yticks(range(len(cols)))
    ax.set_yticklabels([c.split("/")[1] for c in cols], fontsize=7)
    ax.set_xlabel("elapsed (s)")
    ax.set_title(f"{run}: per-core busy %", fontsize=10, loc="left")
    fig.colorbar(im, ax=ax, label="busy %")
    fig.tight_layout()

    path = out_dir / f"{run}_per_core.png"
    fig.savefig(path, dpi=110)
    plt.close(fig)
    return path


def plot_run_comparison(summary: pd.DataFrame, out_dir: Path) -> Path | None:
    """Bar chart of mean vs p95 CPU busy % across runs."""
    rows = summary[summary["series"] == "cpu/all/busy_pct"]
    if rows.empty:
        return None
    rows = rows.set_index("run")[["mean", "p95"]]
    fig, ax = plt.subplots(figsize=(8, 4))
    rows.plot.bar(ax=ax, rot=0)
    ax.set_ylabel("CPU busy %")
    ax.set_title("CPU utilisation by scenario", fontsize=10, loc="left")
    ax.grid(True, axis="y", alpha=0.3)
    fig.tight_layout()
    path = out_dir / "comparison_cpu.png"
    fig.savefig(path, dpi=110)
    plt.close(fig)
    return path
