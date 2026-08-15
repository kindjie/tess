"""Tests for the budgeted-progress curve generator."""

from __future__ import annotations

import json
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import summarize_budgeted_curves as sbc  # noqa: E402


def cell(kind: str, budget_ns: int, **overrides) -> dict:
  """A minimal cell artifact for curve generation."""
  document = {
      "schema": sbc.CELL_SCHEMA,
      "run": {"commit": "cafe123"},
      "experiment": {"kind": kind, "scenario_id": "cell-v1",
                     "budget_ns": budget_ns, "pass": "timing",
                     "sim_tps": 60, "pacing": "unpaced"},
      "summary": {
          "measured_frames": 600, "repetitions": 10,
          "useful_completions": 6000, "consumed_work_units": 600000,
          "overshoot_frame_rate": 0.01,
          "overshoot_quantum_tail_ns": {"p99": 1500, "samples": 6000},
          "overshoot_mandatory_ns": {"p99": None, "samples": 6000},
      },
  }
  document["experiment"].update(overrides.pop("experiment", {}))
  document["summary"].update(overrides.pop("summary", {}))
  return document


def search(budget_ns: int) -> dict:
  """A minimal capacity-search summary."""
  return {
      "schema": sbc.SEARCH_SCHEMA,
      "run": {"commit": "cafe123"},
      "search": {"scenario_id": "search-v1", "budget_ns": budget_ns},
      "capacity_band": {"confirmed_stable": 172, "lowest_unstable": 174},
      "points": [{}, {}, {}],
      "flapping": 0,
  }


def write_dir(tmp_path: Path, documents: list) -> Path:
  """Write documents into a directory as numbered artifacts."""
  for index, document in enumerate(documents):
    (tmp_path / f"artifact-{index}.json").write_text(json.dumps(document),
                                                     encoding="utf-8")
  return tmp_path


def test_isolated_rows_derive_from_artifact_fields(tmp_path):
  """Per-frame and per-completion figures come straight from fields."""
  directory = write_dir(tmp_path, [cell("isolated_saturated", 500000)])
  cells, searches, errors = sbc.load(directory, strict=True)
  csv_rows, md_rows = sbc.summarize_isolated(cells)
  assert len(csv_rows) == 2 and not errors and not searches
  row = csv_rows[1]
  assert ",0.5,timing,1.000,100.0," in row  # 6000/6000 frames; 600000/6000.
  assert "insufficient" in row  # Suppressed mandatory p99 propagates.
  assert "cafe123" in row and "| 0.5 |" in md_rows[2]


def test_demand_rows_cover_arrival_and_mixed(tmp_path):
  """Arrival and mixed cells land in the demand table with wall rates."""
  arrival = cell("isolated_arrival_rate", 2000000,
                 experiment={"arrival_rate_num": 600, "arrival_rate_den": 1,
                             "pacing": "unpaced"},
                 summary={"deadline_success_rate": 0.995,
                          "flow_stable": True, "starved_items": 0})
  mixed = cell("mixed_current_fidelity", 8000000,
               experiment={"population": 250, "pacing": "paced"},
               summary={"deadline_success_rate": 0.97, "flow_stable": False,
                        "starved_items": 3,
                        "useful_per_wall_second": 58.8,
                        "frame_start_lag_ns": {"p99": 12345,
                                               "samples": 6000}})
  directory = write_dir(tmp_path, [arrival, mixed])
  cells, _, _ = sbc.load(directory, strict=True)
  csv_rows, _ = sbc.summarize_demand(cells)
  assert len(csv_rows) == 3
  assert any("600/1 per s" in row and "0.995" in row for row in csv_rows)
  assert any("current_fidelity" in row and "250" in row and "58.8" in row
             and "12345" in row for row in csv_rows)


def test_capacity_rows_and_missing_coverage(tmp_path):
  """Bands render and matrix holes are named, not silently absorbed."""
  documents = [cell("isolated_saturated", 500000),
               cell("mixed_current_fidelity", 8000000,
                    experiment={"pacing": "paced"},
                    summary={"deadline_success_rate": 1.0,
                             "flow_stable": True, "starved_items": 0}),
               search(500000)]
  directory = write_dir(tmp_path, documents)
  cells, searches, _ = sbc.load(directory, strict=True)
  csv_rows, _ = sbc.summarize_search(searches)
  assert "search-v1,0.5,172,174,3,0" in csv_rows[1]
  notes = sbc.report_missing(cells, searches)
  assert any("isolated_saturated" in note and "8" in note for note in notes)
  assert any("mixed_current_fidelity" in note and "0.5" in note
             for note in notes)


def test_strict_empty_directory_fails(tmp_path):
  """--strict makes an empty directory fatal."""
  with pytest.raises(SystemExit):
    sbc.load(tmp_path, strict=True)


def test_cli_writes_csv_files(tmp_path, capsys):
  """The CLI prints Markdown and writes the three CSV files."""
  (tmp_path / "artifacts").mkdir()
  artifact_dir = write_dir(tmp_path / "artifacts",
                           [cell("isolated_saturated", 500000), search(500000)])
  csv_dir = tmp_path / "csv"
  assert sbc.main([str(artifact_dir), "--csv-dir", str(csv_dir)]) == 0
  output = capsys.readouterr().out
  assert "# Budgeted-progress curves" in output
  assert (csv_dir / "isolated.csv").exists()
  assert (csv_dir / "capacity.csv").exists()
  assert "confirmed stable" in output
