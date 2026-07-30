"""Tests for the benchmark workload-matrix drift checker."""

from __future__ import annotations

import json
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import check_workload_matrix as cwm  # noqa: E402


def _catalog(**kwargs):
  base = {
    "vocabularies": {
      "storage": ["always_resident", "sparse_resident", "not_applicable"],
      "executor_kind": ["serial", "pool", "scoped_threads"],
      "layout": ["open", "room_portals", "sparse_blockers",
                 "not_applicable"],
    },
    "families": [],
    "unmeasured": [],
  }
  base.update(kwargs)
  return base


def _family(**kwargs):
  base = {
    "family": "path/astar",
    "pattern": r"^path/astar_open_2d(?:_(64x64|512x512|1024x1024))?$",
    "captures": {"world_extent": 1},
    "defaults": {
      "world_extent": "512x512",
      "chunk_extent": "32x32",
      "layout": "open",
      "storage": "always_resident",
      "executor_kind": "serial",
      "worker_count": "not_applicable",
      "payload": "not_applicable",
    },
  }
  base.update(kwargs)
  return base


def test_conforming_universe_passes():
  catalog = _catalog(families=[_family()])

  errors = cwm.check(catalog, {"path/astar_open_2d",
                               "path/astar_open_2d_64x64"})

  assert errors == []


def test_unclassified_registration_fails():
  catalog = _catalog(families=[_family()])

  errors = cwm.check(catalog, {"path/astar_open_2d", "path/new_thing"})

  assert any("path/new_thing" in e and "no family rule" in e
             for e in errors)


def test_registration_matching_two_rules_fails():
  catalog = _catalog(
    families=[_family(), _family(family="path/astar-dup")]
  )

  errors = cwm.check(catalog, {"path/astar_open_2d"})

  assert any("exactly one" in e for e in errors)


def test_dead_rule_fails():
  catalog = _catalog(
    families=[
      _family(),
      _family(family="path/retired",
              pattern=r"^path/retired_bench$"),
    ]
  )

  errors = cwm.check(catalog, {"path/astar_open_2d"})

  assert any("path/retired" in e and "matches no registration" in e
             for e in errors)


def test_capture_overrides_default():
  catalog = _catalog(families=[_family()])

  cells = cwm.classify(catalog, {"path/astar_open_2d_1024x1024"})

  assert cells["path/astar_open_2d_1024x1024"]["world_extent"] == (
    "1024x1024"
  )


def test_per_name_override_beats_capture_and_default():
  catalog = _catalog(
    families=[
      _family(
        overrides={
          "path/astar_open_2d_64x64": {"chunk_extent": "8x8"}
        }
      )
    ]
  )

  cells = cwm.classify(catalog, {"path/astar_open_2d_64x64"})

  assert cells["path/astar_open_2d_64x64"]["chunk_extent"] == "8x8"
  assert cells["path/astar_open_2d_64x64"]["world_extent"] == "64x64"


def test_unconsumed_dimension_token_fails():
  # The rule matches but leaves the executor token outside any
  # dimension capture: fail-closed instead of silently defaulting.
  catalog = _catalog(
    families=[
      _family(
        family="parallel/fill",
        pattern=r"^parallel/chunk_fill_pool_w2$",
        captures={},
        defaults={
          "world_extent": "512x512",
          "chunk_extent": "32x32",
          "layout": "not_applicable",
          "storage": "always_resident",
          "executor_kind": "serial",
          "worker_count": "not_applicable",
          "payload": "not_applicable",
        },
      )
    ]
  )

  errors = cwm.check(catalog, {"parallel/chunk_fill_pool_w2"})

  assert any("unconsumed" in e and "pool_w2" in e for e in errors)


def test_consumed_executor_token_passes():
  catalog = _catalog(
    families=[
      _family(
        family="parallel/fill",
        pattern=r"^parallel/chunk_fill_(pool)_w(\d+)$",
        captures={"executor_kind": 1, "worker_count": 2},
        defaults={
          "world_extent": "512x512",
          "chunk_extent": "32x32",
          "layout": "not_applicable",
          "storage": "always_resident",
          "executor_kind": "pool",
          "worker_count": "2",
          "payload": "not_applicable",
        },
      )
    ]
  )

  errors = cwm.check(catalog, {"parallel/chunk_fill_pool_w2"})

  assert errors == []


def test_override_consumes_token_without_capture():
  # Sizes that are not what they look like (spatial/..._1000x4 is
  # requests x options, not a world extent) are consumed by an
  # explicit per-name override on the dimension the token would
  # otherwise assert.
  catalog = _catalog(
    families=[
      _family(
        family="spatial/local_coordination",
        pattern=r"^spatial/local_coordination_1000x4$",
        captures={},
        defaults={
          "world_extent": "512x512",
          "chunk_extent": "32x32",
          "layout": "open",
          "storage": "always_resident",
          "executor_kind": "serial",
          "worker_count": "not_applicable",
          "payload": "unknown",
        },
        overrides={
          "spatial/local_coordination_1000x4": {
            "payload": "1000 requests x 4 options"
          }
        },
      )
    ]
  )

  errors = cwm.check(catalog, {"spatial/local_coordination_1000x4"})

  assert not any("unconsumed" in e for e in errors)


def test_unknown_dimension_value_fails():
  catalog = _catalog(
    families=[_family(defaults={**_family()["defaults"],
                                "chunk_extent": "unknown"})]
  )

  errors = cwm.check(catalog, {"path/astar_open_2d"})

  assert any("unknown" in e and "chunk_extent" in e for e in errors)


def test_vocabulary_violation_fails():
  catalog = _catalog(
    families=[_family(defaults={**_family()["defaults"],
                                "storage": "sparse"})]
  )

  errors = cwm.check(catalog, {"path/astar_open_2d"})

  assert any("vocabulary" in e and "storage" in e for e in errors)


def test_measured_cell_matching_unmeasured_selector_fails():
  # The staleness guard: when the gap fills, the unmeasured entry
  # must retire.
  catalog = _catalog(
    families=[
      _family(
        family="parallel/fill",
        pattern=r"^parallel/chunk_fill_(pool)_w(\d+)$",
        captures={"executor_kind": 1, "worker_count": 2},
        defaults={
          "world_extent": "512x512",
          "chunk_extent": "32x32",
          "layout": "not_applicable",
          "storage": "always_resident",
          "executor_kind": "pool",
          "worker_count": "2",
          "payload": "not_applicable",
        },
      )
    ],
    unmeasured=[
      {
        "selector": {"executor_kind": "pool", "worker_count": "8"},
        "reason": "higher widths deferred to controlled hardware",
      }
    ],
  )

  clean = cwm.check(catalog, {"parallel/chunk_fill_pool_w2"})
  stale = cwm.check(
    catalog, {"parallel/chunk_fill_pool_w2", "parallel/chunk_fill_pool_w8"}
  )

  assert clean == []
  assert any("unmeasured selector" in e and "retire" in e
             for e in stale)


def test_unmeasured_selector_requires_reason():
  catalog = _catalog(
    families=[_family()],
    unmeasured=[{"selector": {"executor_kind": "pool"}}],
  )

  errors = cwm.check(catalog, {"path/astar_open_2d"})

  assert any("reason" in e for e in errors)


def test_unmeasured_selector_with_unknown_dimension_fails():
  catalog = _catalog(
    families=[_family()],
    unmeasured=[
      {"selector": {"executor": "pool"}, "reason": "typo dimension"}
    ],
  )

  errors = cwm.check(catalog, {"path/astar_open_2d"})

  assert any("unknown dimension" in e for e in errors)


def test_composite_registration_is_not_a_measured_cell():
  catalog = _catalog(
    families=[
      _family(
        family="path/batch",
        pattern=r"^path/cached_astar_batch_100_mixed$",
        captures={},
        composite=True,
        defaults={
          "world_extent": "512x512",
          "chunk_extent": "32x32",
          "layout": "open",
          "storage": "always_resident",
          "executor_kind": "serial",
          "worker_count": "not_applicable",
          "payload": "100 mixed queries",
        },
      )
    ],
    unmeasured=[
      {
        "selector": {"layout": "open", "executor_kind": "serial"},
        "reason": "only composite batch evidence for this cell",
      }
    ],
  )

  # A composite match must NOT retire the unmeasured selector.
  errors = cwm.check(catalog, {"path/cached_astar_batch_100_mixed"})

  assert errors == []


def test_canonical_name_strips_control_suffixes():
  assert cwm.canonical(
    "path/x/iterations:5000/manual_time"
  ) == "path/x"
  assert cwm.canonical("path/x/real_time") == "path/x"
  assert cwm.canonical("path/x/threads:4") == "path/x"
  # Meaningful workload arguments are NOT control suffixes.
  assert cwm.canonical("maintenance/flush_budget/256") == (
    "maintenance/flush_budget/256"
  )


def test_registration_file_union(tmp_path):
  catalog = _catalog(families=[_family()])
  listing = tmp_path / "list.txt"
  listing.write_text(
    "path/astar_open_2d\npath/astar_open_2d_64x64\n", encoding="utf-8"
  )

  universe = cwm.load_registrations([listing])

  assert cwm.check(catalog, universe) == []


def test_lab_literals_extracted_from_sources(tmp_path):
  source = tmp_path / "tess_lab_bench.cc"
  source.write_text(
    'BENCHMARK(x)->Name("lab/probe_alpha");\n'
    'RegisterBenchmark("lab/probe_beta", fn);\n'
    '// not a string: lab/commented\n',
    encoding="utf-8",
  )

  names = cwm.lab_registrations(tmp_path)

  assert names == {"lab/probe_alpha", "lab/probe_beta"}


def test_threshold_universe_loads_manifest_names(tmp_path):
  (tmp_path / "path.json").write_text(
    json.dumps(
      {
        "benchmarks": [
          {"name": "path/astar_open_2d"},
          {"name": "path/astar_open_2d_64x64"},
        ]
      }
    ),
    encoding="utf-8",
  )

  names = cwm.threshold_registrations(tmp_path)

  assert names == {"path/astar_open_2d", "path/astar_open_2d_64x64"}


def test_main_reports_errors_and_exits_nonzero(tmp_path, capsys):
  catalog_file = tmp_path / "matrix.json"
  catalog_file.write_text(
    json.dumps(_catalog(families=[_family()])), encoding="utf-8"
  )
  thresholds = tmp_path / "thresholds"
  thresholds.mkdir()
  (thresholds / "path.json").write_text(
    json.dumps({"benchmarks": [{"name": "path/unclassified_thing"}]}),
    encoding="utf-8",
  )
  sources = tmp_path / "bench"
  sources.mkdir()

  code = cwm.main(
    [
      f"--catalog={catalog_file}",
      f"--thresholds-dir={thresholds}",
      f"--bench-sources={sources}",
    ]
  )

  captured = capsys.readouterr()
  assert code == 1
  assert "path/unclassified_thing" in captured.err + captured.out


def test_real_catalog_is_coherent_with_real_manifests():
  """The shipped catalog classifies every real registration."""
  repo = Path(__file__).resolve().parents[1]
  catalog = json.loads(
    (repo / "bench" / "workload-matrix.json").read_text(encoding="utf-8")
  )
  universe = cwm.threshold_registrations(
    repo / "bench" / "thresholds"
  ) | cwm.lab_registrations(repo / "bench")

  errors = cwm.check(catalog, universe)

  assert errors == []
