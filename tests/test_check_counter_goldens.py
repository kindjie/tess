"""Tests for the counter-golden checker."""

from __future__ import annotations

import json
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import check_counter_goldens  # noqa: E402


def _doc(values):
  return {"schema": 1, "workloads": values}


def test_identical_documents_have_no_drift():
  doc = _doc({"astar": {"path": {"heap_pushes": 12}}})

  assert check_counter_goldens.diff_counters(doc, doc) == []


def test_value_drift_is_reported_per_counter():
  golden = _doc({"astar": {"path": {"heap_pushes": 12, "heap_pops": 9}}})
  observed = _doc({"astar": {"path": {"heap_pushes": 14, "heap_pops": 9}}})

  rows = check_counter_goldens.diff_counters(golden, observed)

  assert rows == [("astar", "path", "heap_pushes", 12, 14)]


def test_missing_and_unexpected_entries_are_drift():
  golden = _doc({"astar": {"path": {"heap_pushes": 12}}})
  observed = _doc(
    {
      "astar": {"path": {}},
      "extra": {"queued": {"phase_calls": 1}},
    }
  )

  rows = check_counter_goldens.diff_counters(golden, observed)

  assert ("astar", "path", "heap_pushes", 12, None) in rows
  assert ("extra", "-", "-", False, True) in rows


def _write(path, doc):
  path.write_text(json.dumps(doc), encoding="utf-8")


def test_shadow_mode_reports_but_exits_zero(tmp_path, capsys, monkeypatch):
  monkeypatch.delenv("TESS_COUNTER_GOLDENS_STRICT", raising=False)
  golden = tmp_path / "golden.json"
  observed = tmp_path / "observed.json"
  report = tmp_path / "drift.md"
  _write(golden, _doc({"astar": {"path": {"heap_pushes": 12}}}))
  _write(observed, _doc({"astar": {"path": {"heap_pushes": 13}}}))

  code = check_counter_goldens.main(
    (
      "--observed", str(observed),
      "--golden", str(golden),
      "--drift-report", str(report),
    )
  )

  assert code == 0
  assert "heap_pushes" in report.read_text()
  assert "shadow mode" in capsys.readouterr().out


def test_strict_mode_fails_on_drift(tmp_path, monkeypatch):
  monkeypatch.setenv("TESS_COUNTER_GOLDENS_STRICT", "1")
  golden = tmp_path / "golden.json"
  observed = tmp_path / "observed.json"
  _write(golden, _doc({"astar": {"path": {"heap_pushes": 12}}}))
  _write(observed, _doc({"astar": {"path": {"heap_pushes": 13}}}))

  code = check_counter_goldens.main(
    ("--observed", str(observed), "--golden", str(golden))
  )

  assert code == 1


def test_matching_documents_pass_in_strict_mode(tmp_path, monkeypatch):
  monkeypatch.setenv("TESS_COUNTER_GOLDENS_STRICT", "1")
  golden = tmp_path / "golden.json"
  observed = tmp_path / "observed.json"
  doc = _doc({"astar": {"path": {"heap_pushes": 12}}})
  _write(golden, doc)
  _write(observed, doc)

  code = check_counter_goldens.main(
    ("--observed", str(observed), "--golden", str(golden))
  )

  assert code == 0


def test_update_rewrites_the_golden(tmp_path):
  golden = tmp_path / "golden.json"
  observed = tmp_path / "observed.json"
  _write(golden, _doc({"astar": {"path": {"heap_pushes": 12}}}))
  _write(observed, _doc({"astar": {"path": {"heap_pushes": 13}}}))

  code = check_counter_goldens.main(
    ("--observed", str(observed), "--golden", str(golden), "--update")
  )

  assert code == 0
  assert json.loads(golden.read_text()) == _doc(
    {"astar": {"path": {"heap_pushes": 13}}}
  )


def test_malformed_observed_fails_closed(tmp_path):
  golden = tmp_path / "golden.json"
  observed = tmp_path / "observed.json"
  _write(golden, _doc({}))
  observed.write_text("not json", encoding="utf-8")

  with pytest.raises(SystemExit):
    check_counter_goldens.main(
      ("--observed", str(observed), "--golden", str(golden))
    )


@pytest.mark.parametrize(
  "doc",
  (
    {"schema": 2, "workloads": {}},
    {"schema": True, "workloads": {}},
    {"workloads": {}},
    {"schema": 1, "workloads": []},
    {"schema": 1, "workloads": {"astar": []}},
    {"schema": 1, "workloads": {"astar": {"path": {"heap_pushes": True}}}},
    {"schema": 1, "workloads": {"astar": {"path": {"heap_pushes": -1}}}},
    {"schema": 1, "workloads": {"astar": {"path": {"heap_pushes": 1.5}}}},
  ),
)
def test_structurally_invalid_documents_fail_closed(tmp_path, doc):
  golden = tmp_path / "golden.json"
  observed = tmp_path / "observed.json"
  _write(golden, _doc({}))
  _write(observed, doc)

  with pytest.raises(SystemExit):
    check_counter_goldens.main(
      ("--observed", str(observed), "--golden", str(golden))
    )


def test_matching_rerun_removes_a_stale_drift_report(tmp_path, monkeypatch):
  monkeypatch.delenv("TESS_COUNTER_GOLDENS_STRICT", raising=False)
  golden = tmp_path / "golden.json"
  observed = tmp_path / "observed.json"
  report = tmp_path / "drift.md"
  doc = _doc({"astar": {"path": {"heap_pushes": 12}}})
  _write(golden, doc)
  _write(observed, doc)
  report.write_text("stale drift", encoding="utf-8")

  code = check_counter_goldens.main(
    (
      "--observed", str(observed),
      "--golden", str(golden),
      "--drift-report", str(report),
    )
  )

  assert code == 0
  assert not report.exists()


def test_report_cells_escape_markdown_pipes():
  rows = [("a|b", "path", "heap|pushes", 1, 2)]

  report = check_counter_goldens.render_report(rows)

  assert "a\\|b" in report
  assert "heap\\|pushes" in report


def test_strict_reports_are_labeled_strict():
  rows = [("astar", "path", "heap_pushes", 1, 2)]

  assert "strict mode" in check_counter_goldens.render_report(
    rows, strict=True
  )
  assert "shadow mode" in check_counter_goldens.render_report(rows)


def test_drift_report_points_at_the_profiling_protocol():
  report = check_counter_goldens.render_report(
    [("w", "fam", "c", 1, 2)]
  )

  assert "profiling protocol" in report
  assert "step 1" in report
