"""Command-line entry point: ``python -m systempulse_analysis run1.csv run2.csv --out report/``."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from .loader import load_many
from .report import build_report
from .stats import compare_runs, summarize, HEADLINE_SERIES


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    p = argparse.ArgumentParser(
        prog="systempulse_analysis",
        description="Time-series analysis of SystemPulse CSV files.",
    )
    p.add_argument("csv", nargs="+", type=Path, help="one or more SystemPulse CSV files")
    p.add_argument("--out", type=Path, default=Path("report"), help="output directory")
    p.add_argument("--freq", default="1s", help="resample interval (pandas offset, default 1s)")
    p.add_argument("--z-window", type=int, default=30, help="rolling baseline window (samples)")
    p.add_argument("--z-threshold", type=float, default=3.5, help="anomaly z-score threshold")
    p.add_argument("--cpu-saturation", type=float, default=90.0, help="CPU saturation threshold %%")
    p.add_argument("--no-plots", action="store_true", help="skip PNG generation")
    p.add_argument("--summary-only", action="store_true", help="print stats and exit")
    return p.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    missing = [str(p) for p in args.csv if not p.exists()]
    if missing:
        print(f"error: file(s) not found: {', '.join(missing)}", file=sys.stderr)
        return 2

    df = load_many(args.csv)
    if df.empty:
        print("error: no samples found", file=sys.stderr)
        return 1

    if args.summary_only:
        s = summarize(df, HEADLINE_SERIES)
        print(s.to_string(index=False, float_format=lambda v: f"{v:,.2f}"))
        if df["run"].nunique() > 1:
            print()
            print(compare_runs(df).to_string(float_format=lambda v: f"{v:,.2f}"))
        return 0

    path = build_report(
        df,
        args.out,
        freq=args.freq,
        z_window=args.z_window,
        z_threshold=args.z_threshold,
        cpu_saturation_pct=args.cpu_saturation,
        make_plots=not args.no_plots,
    )
    print(f"report written to {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
