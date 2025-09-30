from systempulse_analysis import load_many
from systempulse_analysis.cli import main
from systempulse_analysis.report import build_report


def test_build_report_writes_artifacts(tmp_path, steady_csv, spiky_csv):
    out = tmp_path / "report"
    path = build_report(load_many([steady_csv, spiky_csv]), out, z_threshold=4.0)
    text = path.read_text()
    assert "## Scenario comparison" in text
    assert "## Run `spiky`" in text
    assert (out / "summary_stats.csv").exists()
    assert (out / "workers.csv").exists()
    assert (out / "spiky_overview.png").exists()
    assert (out / "steady_per_core.png").exists()


def test_cli_summary_only(capsys, steady_csv):
    assert main([str(steady_csv), "--summary-only"]) == 0
    assert "cpu/all/busy_pct" in capsys.readouterr().out


def test_cli_missing_file(tmp_path):
    assert main([str(tmp_path / "nope.csv")]) == 2
