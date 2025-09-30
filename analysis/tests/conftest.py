"""Synthetic SystemPulse CSV fixtures."""

from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

HEADER = "timestamp_ms,source,entity,metric,value\n"
T0 = 1_700_000_000_000


def write_run(
    path: Path,
    seconds: int = 60,
    cpu_level: float = 20.0,
    spike_at: int | None = None,
    workers: int = 2,
    rss_growth_kb: float = 0.0,
    seed: int = 0,
) -> Path:
    rng = np.random.default_rng(seed)
    rows = [HEADER]
    for s in range(seconds):
        ts = T0 + s * 1000
        cpu = cpu_level + rng.normal(0, 1.0)
        if spike_at is not None and s == spike_at:
            cpu = 99.0
        rows.append(f"{ts},cpu,all,busy_pct,{cpu:.3f}\n")
        rows.append(f"{ts},cpu,all,iowait_pct,{rng.uniform(0, 2):.3f}\n")
        for core in range(2):
            rows.append(f"{ts + 1},cpu,cpu{core},busy_pct,{cpu + rng.normal(0, 2):.3f}\n")
        rows.append(f"{ts + 2},mem,system,used_pct,{40 + rng.normal(0, 0.2):.3f}\n")
        rows.append(f"{ts + 2},load,system,load1,{1.0 + rng.normal(0, 0.05):.3f}\n")
        rows.append(f"{ts + 3},io,sda,read_bps,{abs(rng.normal(1e6, 1e5)):.1f}\n")
        rows.append(f"{ts + 3},io,sda,write_bps,{abs(rng.normal(2e6, 1e5)):.1f}\n")
        rows.append(f"{ts + 3},io,sda,util_pct,{rng.uniform(5, 15):.3f}\n")
        total_cpu = 0.0
        for w in range(workers):
            ent = f"pid:{1000 + w}:worker-{w}"
            wcpu = cpu_level / workers + rng.normal(0, 0.5)
            total_cpu += wcpu
            rows.append(f"{ts + 4},proc,{ent},cpu_pct,{wcpu:.3f}\n")
            rows.append(f"{ts + 4},proc,{ent},rss_kb,{10240 + rss_growth_kb * s:.1f}\n")
            rows.append(f"{ts + 4},proc,{ent},disk_write_bps,{1e5:.1f}\n")
        rows.append(f"{ts + 5},proc,workers,cpu_pct,{total_cpu:.3f}\n")
    path.write_text("".join(rows))
    return path


@pytest.fixture
def steady_csv(tmp_path: Path) -> Path:
    return write_run(tmp_path / "steady.csv", cpu_level=20.0, seed=1)


@pytest.fixture
def spiky_csv(tmp_path: Path) -> Path:
    return write_run(tmp_path / "spiky.csv", cpu_level=20.0, spike_at=45, seed=2)


@pytest.fixture
def busy_csv(tmp_path: Path) -> Path:
    return write_run(tmp_path / "busy.csv", cpu_level=95.0, rss_growth_kb=4096, seed=3)
