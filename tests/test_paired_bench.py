"""Tests for the paired base-vs-head sentinel benchmark runner."""

from __future__ import annotations

import json
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import paired_bench  # noqa: E402


REPO = Path(__file__).resolve().parents[1]


def _config():
  return paired_bench.load_config(REPO / "bench" / "sentinels.json")


# --- Sentinel definitions and the section 4.5 completeness contract ---


def test_sentinel_names_exist_in_threshold_files():
  gated = {}
  for path in (REPO / "bench" / "thresholds").glob("*.json"):
    gated.update(json.loads(path.read_text())["benchmarks"])

  missing = [s for s in _config().sentinels if s not in gated]

  assert missing == []


def test_confirmed_catch_sentinels_are_present():
  config = _config()
  required = (
    "path/cached_astar_batch_100_mixed_repeated_room_portals_512x512",
    "path/field_product_cache_hit_replay_room_portals_512x512",
    "path/nearest_target_product_100_starts_room_portals_512x512",
    "path/weighted_batch_planner_100_neargoal_open_512x512",
  )

  for name in required:
    assert name in config.sentinels


def test_every_public_source_directory_is_mapped_or_explained():
  config = _config()
  directories = sorted(
    f"include/tess/{entry.name}/"
    for entry in (REPO / "include" / "tess").iterdir()
    if entry.is_dir()
  )

  unmapped = [d for d in directories if d not in config.source_map]

  assert unmapped == []


def test_root_umbrella_headers_are_mapped():
  config = _config()
  root_headers = [
    entry
    for entry in (REPO / "include" / "tess").iterdir()
    if entry.suffix == ".h"
  ]

  assert root_headers  # tess.h and friends exist
  entry = config.source_map["include/tess/"]
  assert isinstance(entry, list) and entry


def test_source_map_and_perf_classifier_agree():
  """Sentinel-mapped areas are exactly the perf-sensitive ones.

  Section 4.5: a perf-sensitive source area with no sentinel
  representative must fail. The inverse also holds — an area declared
  unrepresented must not trigger the paired run it cannot inform.
  """
  import ci_changes

  config = _config()

  for directory, entry in config.source_map.items():
    probe = directory + "some_header.h"
    if isinstance(entry, dict):
      assert not ci_changes.is_perf_sensitive_path(probe), directory
    else:
      assert ci_changes.is_perf_sensitive_path(probe), directory


def test_mapped_sentinels_are_defined_and_reasons_are_nonempty():
  config = _config()

  for directory, entry in config.source_map.items():
    if isinstance(entry, dict):
      assert entry["unrepresented"].strip(), directory
    else:
      for sentinel in entry:
        assert sentinel in config.sentinels, (directory, sentinel)


def test_sentinel_metrics_are_known_kinds():
  for name, sentinel in _config().sentinels.items():
    assert sentinel.metric in ("cpu_time", "real_time"), name


# --- Benchmark output parsing and the run plan ---


def test_benchmark_filter_anchors_every_name():
  filter_re = paired_bench.benchmark_filter(("a/b", "c/d_10k"))

  assert filter_re == "^(a/b|c/d_10k)$"


def test_parse_results_extracts_the_configured_metric():
  payload = {
    "benchmarks": [
      {"name": "a/b", "cpu_time": 100.0, "real_time": 200.0},
      {"name": "c/d", "cpu_time": 300.0, "real_time": 400.0},
    ]
  }
  metrics = {"a/b": "cpu_time", "c/d": "real_time"}

  values = paired_bench.parse_results(json.dumps(payload), metrics)

  assert values == {"a/b": 100.0, "c/d": 400.0}


def test_parse_results_fails_closed_on_missing_sentinel():
  payload = {"benchmarks": [{"name": "a/b", "cpu_time": 1.0}]}

  with pytest.raises(paired_bench.ToolError):
    paired_bench.parse_results(
      json.dumps(payload), {"a/b": "cpu_time", "missing/one": "cpu_time"}
    )


def test_round_sides_alternate_to_cancel_drift():
  order = [paired_bench.round_sides(i) for i in range(4)]

  assert order == [
    ("base", "head"),
    ("head", "base"),
    ("base", "head"),
    ("head", "base"),
  ]


# --- Statistics ---


def test_bootstrap_flags_a_large_consistent_regression():
  base = [100.0] * 10
  head = [130.0] * 10

  result = paired_bench.evaluate_sentinel(
    "s",
    base,
    head,
    effect_floor=0.08,
    materiality_ns=2.0,
    resamples=500,
    confidence=0.95,
    seed=7,
  )

  assert result.flagged
  assert result.delta_relative == pytest.approx(0.30, abs=0.01)


def test_bootstrap_does_not_flag_noise_straddling_zero():
  base = [100.0, 130.0, 90.0, 120.0, 110.0, 95.0, 125.0, 105.0]
  head = [102.0, 128.0, 92.0, 118.0, 112.0, 93.0, 127.0, 103.0]

  result = paired_bench.evaluate_sentinel(
    "s",
    base,
    head,
    effect_floor=0.08,
    materiality_ns=2.0,
    resamples=500,
    confidence=0.95,
    seed=7,
  )

  assert not result.flagged


def test_materiality_floor_suppresses_tiny_absolute_shifts():
  base = [10.0] * 10
  head = [13.0] * 10  # +30% but only 3 ns absolute

  result = paired_bench.evaluate_sentinel(
    "s",
    base,
    head,
    effect_floor=0.08,
    materiality_ns=2000.0,
    resamples=500,
    confidence=0.95,
    seed=7,
  )

  assert not result.flagged


def test_bootstrap_is_deterministic_for_a_seed():
  base = [100.0, 105.0, 98.0, 110.0, 102.0]
  head = [115.0, 118.0, 112.0, 121.0, 116.0]

  first = paired_bench.evaluate_sentinel(
    "s", base, head,
    effect_floor=0.08, materiality_ns=2.0,
    resamples=500, confidence=0.95, seed=42,
  )
  second = paired_bench.evaluate_sentinel(
    "s", base, head,
    effect_floor=0.08, materiality_ns=2.0,
    resamples=500, confidence=0.95, seed=42,
  )

  assert first == second


# --- Verdicts and rendering ---


def test_verdicts_combine_first_and_confirmation_passes():
  assert paired_bench.sentinel_verdict(False, None) == "pass"
  assert paired_bench.sentinel_verdict(True, False) == "advisory"
  assert paired_bench.sentinel_verdict(True, True) == "regression"


def test_run_verdict_is_the_worst_sentinel_verdict():
  assert paired_bench.run_verdict(["pass", "pass"]) == "pass"
  assert paired_bench.run_verdict(["pass", "advisory"]) == "advisory"
  assert (
    paired_bench.run_verdict(["advisory", "regression"]) == "regression"
  )
  assert paired_bench.run_verdict([]) == "pass"


def test_markdown_summary_names_every_sentinel_and_verdict():
  result = paired_bench.SentinelResult(
    name="path/x",
    base_median=100.0,
    head_median=130.0,
    delta_relative=0.3,
    ci_low=0.25,
    ci_high=0.35,
    flagged=True,
  )

  summary = paired_bench.render_markdown(
    [(result, "advisory")], "advisory", mode="shadow"
  )

  assert "path/x" in summary
  assert "advisory" in summary
  assert "+30.0%" in summary


# --- Orchestration against a fake benchmark binary ---


def _fake_binary(path, values):
  payload = {
    "benchmarks": [
      {"name": name, "cpu_time": value, "real_time": value}
      for name, value in values.items()
    ]
  }
  names = "\n".join(values)
  path.write_text(
    "#!/bin/sh\n"
    'case "$1" in\n'
    "  --benchmark_list_tests=*)\n"
    f"    cat <<'NAMES'\n{names}\nNAMES\n"
    "    exit 0;;\n"
    "esac\n"
    "cat <<'JSON'\n" + json.dumps(payload) + "\nJSON\n",
    encoding="utf-8",
  )
  path.chmod(0o755)


def _sentinel_file(path, names):
  config = {
    "version": 1,
    "sentinels": {name: {"metric": "cpu_time"} for name in names},
    "source_map": {},
    "parameters": {
      "repetitions": 4,
      "effect_floor_relative": 0.08,
      "materiality_floor_ns": 2.0,
      "bootstrap_resamples": 200,
      "confidence": 0.95,
    },
  }
  path.write_text(json.dumps(config), encoding="utf-8")


def test_main_reports_pass_for_identical_binaries(tmp_path, capsys):
  base = tmp_path / "base_bench"
  head = tmp_path / "head_bench"
  _fake_binary(base, {"a/b": 100.0})
  _fake_binary(head, {"a/b": 101.0})
  sentinels = tmp_path / "sentinels.json"
  _sentinel_file(sentinels, ["a/b"])
  summary = tmp_path / "summary.md"
  out = tmp_path / "out.json"

  code = paired_bench.main(
    (
      "--base-binary", str(base),
      "--head-binary", str(head),
      "--sentinels", str(sentinels),
      "--summary", str(summary),
      "--json", str(out),
      "--mode", "shadow",
      "--seed", "7",
    )
  )

  assert code == 0
  report = json.loads(out.read_text())
  assert report["verdict"] == "pass"
  assert summary.read_text().count("a/b") == 1


def test_main_confirms_regressions_and_gates_in_confirm_mode(tmp_path):
  base = tmp_path / "base_bench"
  head = tmp_path / "head_bench"
  _fake_binary(base, {"a/b": 100.0})
  _fake_binary(head, {"a/b": 150.0})
  sentinels = tmp_path / "sentinels.json"
  _sentinel_file(sentinels, ["a/b"])
  out = tmp_path / "out.json"

  code = paired_bench.main(
    (
      "--base-binary", str(base),
      "--head-binary", str(head),
      "--sentinels", str(sentinels),
      "--json", str(out),
      "--mode", "confirm",
      "--seed", "7",
    )
  )

  assert code == 1
  report = json.loads(out.read_text())
  assert report["verdict"] == "regression"
  assert report["sentinels"]["a/b"]["verdict"] == "regression"


def test_main_shadow_mode_never_fails_on_regressions(tmp_path):
  base = tmp_path / "base_bench"
  head = tmp_path / "head_bench"
  _fake_binary(base, {"a/b": 100.0})
  _fake_binary(head, {"a/b": 150.0})
  sentinels = tmp_path / "sentinels.json"
  _sentinel_file(sentinels, ["a/b"])
  out = tmp_path / "out.json"

  code = paired_bench.main(
    (
      "--base-binary", str(base),
      "--head-binary", str(head),
      "--sentinels", str(sentinels),
      "--json", str(out),
      "--mode", "shadow",
      "--seed", "7",
    )
  )

  assert code == 0
  assert json.loads(out.read_text())["verdict"] == "regression"


def test_main_fails_closed_when_a_binary_is_missing(tmp_path):
  sentinels = tmp_path / "sentinels.json"
  _sentinel_file(sentinels, ["a/b"])

  code = paired_bench.main(
    (
      "--base-binary", str(tmp_path / "absent"),
      "--head-binary", str(tmp_path / "absent2"),
      "--sentinels", str(sentinels),
      "--mode", "shadow",
    )
  )

  assert code == 1


def test_pairing_detects_a_shift_marginal_stats_would_miss():
  # Huge round-to-round variance, but head is +15% of base every round:
  # the paired ratios are exact while marginal medians are noise.
  base = [100.0, 900.0, 300.0, 700.0, 500.0, 200.0, 800.0, 400.0]
  head = [round(value * 1.15, 6) for value in base]

  result = paired_bench.evaluate_sentinel(
    "s",
    base,
    head,
    effect_floor=0.08,
    materiality_ns=2.0,
    resamples=500,
    confidence=0.95,
    seed=7,
  )

  assert result.flagged
  assert result.delta_relative == pytest.approx(0.15, abs=0.001)


def test_unpaired_sample_counts_fail_closed():
  with pytest.raises(paired_bench.ToolError):
    paired_bench.evaluate_sentinel(
      "s",
      [1.0, 2.0],
      [1.0],
      effect_floor=0.08,
      materiality_ns=2.0,
      resamples=100,
      confidence=0.95,
      seed=7,
    )


def test_confirm_mode_confidence_is_bonferroni_adjusted():
  assert paired_bench.adjusted_confidence(0.95, 1) == 0.95
  assert paired_bench.adjusted_confidence(0.95, 12) == pytest.approx(
    1.0 - 0.05 / 12
  )


def test_main_skips_sentinels_missing_from_one_side(tmp_path, capsys):
  base = tmp_path / "base_bench"
  head = tmp_path / "head_bench"
  # The head renamed b/old to b/new: neither name is comparable, and the
  # run reports the skip instead of aborting.
  _fake_binary(base, {"a/b": 100.0, "b/old": 50.0})
  _fake_binary(head, {"a/b": 101.0, "b/new": 50.0})
  sentinels = tmp_path / "sentinels.json"
  _sentinel_file(sentinels, ["a/b", "b/old", "b/new"])
  out = tmp_path / "out.json"

  code = paired_bench.main(
    (
      "--base-binary", str(base),
      "--head-binary", str(head),
      "--sentinels", str(sentinels),
      "--json", str(out),
      "--mode", "shadow",
      "--seed", "7",
    )
  )

  assert code == 0
  report = json.loads(out.read_text())
  assert report["verdict"] == "pass"
  assert report["skipped"] == {
    "b/new": "not registered in the base binary",
    "b/old": "not registered in the head binary",
  }
  assert set(report["sentinels"]) == {"a/b"}


def test_main_fails_when_no_sentinel_is_comparable(tmp_path, capsys):
  base = tmp_path / "base_bench"
  head = tmp_path / "head_bench"
  _fake_binary(base, {"only/base": 1.0})
  _fake_binary(head, {"only/head": 1.0})
  sentinels = tmp_path / "sentinels.json"
  _sentinel_file(sentinels, ["only/base", "only/head"])

  code = paired_bench.main(
    (
      "--base-binary", str(base),
      "--head-binary", str(head),
      "--sentinels", str(sentinels),
      "--mode", "shadow",
    )
  )

  assert code == 1


def test_confirm_report_carries_confidence_and_confirmation_pass(tmp_path):
  base = tmp_path / "base_bench"
  head = tmp_path / "head_bench"
  _fake_binary(base, {"a/b": 100.0})
  _fake_binary(head, {"a/b": 150.0})
  sentinels = tmp_path / "sentinels.json"
  _sentinel_file(sentinels, ["a/b"])
  out = tmp_path / "out.json"
  summary = tmp_path / "summary.md"

  code = paired_bench.main(
    (
      "--base-binary", str(base),
      "--head-binary", str(head),
      "--sentinels", str(sentinels),
      "--json", str(out),
      "--summary", str(summary),
      "--mode", "confirm",
      "--seed", "7",
    )
  )

  assert code == 1
  report = json.loads(out.read_text())
  assert report["confidence"] == 0.95  # single sentinel: no adjustment
  assert report["sentinels"]["a/b"]["confirmation"]["flagged"] is True
  assert "95% CI" in summary.read_text()


def test_nonpositive_samples_fail_closed():
  with pytest.raises(paired_bench.ToolError):
    paired_bench.evaluate_sentinel(
      "s",
      [100.0, 0.0],
      [100.0, 100.0],
      effect_floor=0.08,
      materiality_ns=2.0,
      resamples=100,
      confidence=0.95,
      seed=7,
    )


def test_materiality_uses_the_paired_median_difference():
  # Marginal medians differ by 2500 ns, but the paired per-round
  # differences have median 1500 ns — below a 2000 ns floor.
  base = [1000.0, 1000.0, 2000.0]
  head = [1500.0, 3500.0, 3500.0]

  result = paired_bench.evaluate_sentinel(
    "s",
    base,
    head,
    effect_floor=0.08,
    materiality_ns=2000.0,
    resamples=500,
    confidence=0.95,
    seed=7,
  )

  assert not result.flagged
