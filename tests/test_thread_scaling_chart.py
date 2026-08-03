"""Tests for the thread-scaling crossover chart."""

from __future__ import annotations

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import thread_scaling_chart  # noqa: E402


REPO = Path(__file__).resolve().parents[1]


def _series():
  return [
    thread_scaling_chart.Series(
      label="pinned",
      points=[(11.4, 0.34), (46.9, 0.95), (93.6, 1.60), (6472.7, 3.06)],
    ),
    thread_scaling_chart.Series(
      label="unpinned",
      points=[(11.4, 0.30), (46.9, 0.90), (93.6, 1.31), (6472.7, 3.02)],
    ),
  ]


def _svg():
  return thread_scaling_chart.render(
    _series(), band=(45.0, 95.0), title="crossover"
  )


def test_renders_wellformed_svg():
  svg = _svg()
  assert svg.startswith("<svg")
  assert svg.rstrip().endswith("</svg>")
  assert svg.count("<svg") == 1


def test_has_no_external_references():
  # The docs site must render it offline; a remote font or image would
  # silently break in the published page.
  svg = _svg()
  # The xmlns URI is an identifier, never fetched; anything else is.
  external = [
    ref
    for ref in re.findall(r'https?://[^"\' >]+', svg)
    if ref != "http://www.w3.org/2000/svg"
  ]
  assert external == []
  assert "<image" not in svg
  assert "@import" not in svg


def test_plots_every_point_of_every_series():
  svg = _svg()
  # One marker per point.
  assert svg.count('class="pt"') == 8


def test_draws_the_unity_reference_line():
  # Without it the reader cannot see where the pool stops losing, which
  # is the entire point of the chart.
  assert 'class="unity"' in _svg()


def test_draws_the_crossover_band():
  assert 'class="band"' in _svg()


def test_log_scale_places_a_decade_evenly():
  # x is log10 because the work axis spans 11 ns to 6.5 us; a linear axis
  # would collapse every interesting point onto the left edge.
  x1 = thread_scaling_chart.log_x(10.0, 10.0, 10000.0, 0.0, 300.0)
  x2 = thread_scaling_chart.log_x(100.0, 10.0, 10000.0, 0.0, 300.0)
  x3 = thread_scaling_chart.log_x(1000.0, 10.0, 10000.0, 0.0, 300.0)
  assert x2 - x1 == pytest.approx(x3 - x2)


def test_axis_endpoints_map_to_the_plot_edges():
  assert thread_scaling_chart.log_x(10.0, 10.0, 1000.0, 20.0, 220.0) == (
    pytest.approx(20.0)
  )
  assert thread_scaling_chart.log_x(1000.0, 10.0, 1000.0, 20.0, 220.0) == (
    pytest.approx(220.0)
  )


def test_rejects_a_non_positive_x():
  # Guards the log: a zero would be an infinity in the coordinate space.
  with pytest.raises(thread_scaling_chart.ChartError):
    thread_scaling_chart.log_x(0.0, 10.0, 1000.0, 0.0, 100.0)


def test_rejects_empty_input():
  with pytest.raises(thread_scaling_chart.ChartError):
    thread_scaling_chart.render([], band=(45.0, 95.0), title="x")


def test_is_deterministic():
  assert _svg() == _svg()


def test_theme_aware_so_the_docs_dark_mode_is_readable():
  # docs/ ships a dark mode; a chart hard-coded to black-on-white
  # becomes an unreadable slab in it.
  assert "prefers-color-scheme: dark" in _svg()


def test_series_labels_appear_in_the_legend():
  svg = _svg()
  assert "pinned" in svg
  assert "unpinned" in svg


def test_escapes_markup_in_labels():
  svg = thread_scaling_chart.render(
    [thread_scaling_chart.Series(label="a<b>&c", points=[(10.0, 1.0)])],
    band=(45.0, 95.0),
    title="t",
  )
  assert "<b>" not in svg
  assert "&lt;b&gt;" in svg


def test_reads_a_sweep_artifact(tmp_path):
  import json

  benchmarks = []
  for workload, serial, pool in (("chunk_fill", 1000.0, 400.0),):
    for _ in range(3):
      benchmarks.append({
        "name": f"lab/thread_scaling/{workload}/serial/real_time",
        "run_type": "iteration",
        "real_time": serial,
      })
      benchmarks.append({
        "name": f"lab/thread_scaling/{workload}/4/real_time",
        "run_type": "iteration",
        "real_time": pool,
      })
  path = tmp_path / "sweep.json"
  path.write_text(json.dumps({"benchmarks": benchmarks}))
  points = thread_scaling_chart.points_from_sweep(path, workers=4, chunks=4096)
  assert points == [(pytest.approx(1000.0 / 4096), pytest.approx(2.5))]


def test_pairs_serial_per_process(tmp_path):
  """A merged multi-process artifact must not pool serial baselines.

  The campaign runs one process per width and each re-measures its own
  serial run; pooling them divides a width's pool median by baselines
  from every other process, which is the confounding the paired runs
  exist to remove.
  """
  import json

  benchmarks = []
  for width, serial in ((2, 1000.0), (4, 2000.0)):
    for _ in range(3):
      benchmarks.append({
        "name": "lab/thread_scaling/chunk_fill/serial/real_time",
        "run_type": "iteration",
        "real_time": serial,
        "tess_run_group": f"w{width}",
      })
      benchmarks.append({
        "name": f"lab/thread_scaling/chunk_fill/{width}/real_time",
        "run_type": "iteration",
        "real_time": serial / 2.0,
        "tess_run_group": f"w{width}",
      })
  path = tmp_path / "merged.json"
  path.write_text(json.dumps({"benchmarks": benchmarks}))

  # Width 4's speedup must use its own 2000.0 baseline (2.0x), not the
  # pooled median of 1000 and 2000.
  points = thread_scaling_chart.points_from_sweep(path, workers=4, chunks=4096)
  assert points == [(pytest.approx(2000.0 / 4096), pytest.approx(2.0))]
