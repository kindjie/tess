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


def search(budget_ns: int, commit: str = "cafe123") -> dict:
  """A minimal capacity-search summary with confirmed-point evidence."""
  rep = {"stable": True, "cohort_admitted": 100, "cohort_deadline_met": 99,
         "outstanding_growth": 1, "oldest_age_end_ticks": 4}
  return {
      "schema": sbc.SEARCH_SCHEMA,
      "run": {"commit": commit},
      "search": {"scenario_id": "search-v1", "budget_ns": budget_ns},
      "capacity_band": {"confirmed_stable": 172, "lowest_unstable": 174},
      "points": [{"rate": 86, "confirmation": False, "stable": True},
                 {"rate": 174, "confirmation": False, "stable": False},
                 {"rate": 172, "confirmation": True, "stable": True,
                  "reps": [dict(rep), dict(rep)]}],
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


def arrival(budget_ns: int, rate: int) -> dict:
  """A fixed-rate arrival cell at the given budget and rate."""
  return cell("isolated_arrival_rate", budget_ns,
              experiment={"arrival_rate_num": rate, "arrival_rate_den": 1,
                          "pacing": "paced"},
              summary={"deadline_success_rate": 0.995, "flow_stable": True,
                       "starved_items": 0})


def test_capacity_rows_join_nearest_arrival_cell(tmp_path):
  """Bands carry confirmed-point evidence plus borrowed overshoot."""
  documents = [arrival(500000, 150), arrival(500000, 600), search(500000)]
  directory = write_dir(tmp_path, documents)
  cells, searches, _ = sbc.load(directory, strict=True)
  csv_rows, md_rows = sbc.summarize_search(searches, cells)
  # Band edges, the confirmed point's evidence (198/200 met, max
  # growth 1, max oldest age 4), and the overshoot tail p99 borrowed
  # from the arrival cell nearest the confirmed 172/s (the 150/s one).
  assert ("search-v1,0.5,172,174,0.990,1,4,1500,150,3,0,cafe123"
          in csv_rows[1])
  assert "1500 @150/s" in md_rows[2]


def test_missing_coverage_uses_per_kind_budget_axes(tmp_path):
  """Holes compare within a kind's own budget axis, never across kinds."""
  documents = [arrival(500000, 150), arrival(500000, 600),
               arrival(2000000, 150),
               cell("mixed_current_fidelity", 8000000,
                    experiment={"pacing": "paced"},
                    summary={"deadline_success_rate": 1.0,
                             "flow_stable": True, "starved_items": 0}),
               search(500000, commit="beef456")]
  directory = write_dir(tmp_path, documents)
  cells, searches, _ = sbc.load(directory, strict=True)
  notes = sbc.report_missing(cells, searches)
  # The 600/s arrival group misses the 2 ms budget on its own axis.
  assert any("rate 600/1" in note and "['2']" in note for note in notes)
  # The mixed kind's axis is only 8 ms, so no hole is fabricated from
  # the arrival kind's budgets.
  assert not any("mixed_current_fidelity" in note for note in notes)
  # Search summaries participate in commit-pooling detection.
  assert any("2 distinct commits" in note for note in notes)
  # Partial capacity-search coverage is reported against arrival budgets.
  assert any("capacity search covers only" in note and "['2']" in note
             for note in notes)


def test_counter_pass_rows_stay_out_of_markdown(tmp_path):
  """Counter-pass rows land in the CSV but never in published curves."""
  documents = [cell("isolated_saturated", 500000),
               cell("isolated_saturated", 500000,
                    experiment={"pass": "counter"})]
  directory = write_dir(tmp_path, documents)
  cells, _, _ = sbc.load(directory, strict=True)
  csv_rows, md_rows = sbc.summarize_isolated(cells)
  assert sum(",counter," in row for row in csv_rows) == 1
  assert not any("counter" in line for line in md_rows)


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
  assert "confirmed /s" in output

def test_movement_tier_joins_identity_and_columns(tmp_path):
  """Tiers separate coverage groups and appear in demand rows."""
  baseline = cell("mixed_current_fidelity", 8000000,
                  experiment={"pacing": "paced", "population": 100},
                  summary={"deadline_success_rate": 0.9,
                           "flow_stable": False, "starved_items": 3})
  pibt = cell("mixed_current_fidelity", 8000000,
              experiment={"pacing": "paced", "population": 100,
                          "movement_tier": "pibt",
                          "scenario_id": "cell-pibt-v1"},
              summary={"deadline_success_rate": 1.0,
                       "flow_stable": True, "starved_items": 0})
  directory = write_dir(tmp_path, [baseline, pibt])
  cells, _, _ = sbc.load(directory, strict=True)
  csv_rows, _ = sbc.summarize_demand(cells)
  assert any(",baseline," in row and "0.900" in row for row in csv_rows)
  assert any(",pibt," in row and "1.000" in row for row in csv_rows)
  # Same kind, different tiers, each with a full (single-budget) axis:
  # no hole is fabricated across the tier boundary.
  notes = sbc.report_missing(cells, [])
  assert not any("no artifacts" in note for note in notes)
