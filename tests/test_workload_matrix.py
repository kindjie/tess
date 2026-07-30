"""Tests for the benchmark workload-matrix drift checker."""

from __future__ import annotations

import json
import sys
from pathlib import Path

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
              pattern=r"^path/retired_bench$",
              captures={}),
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

  assert any("pool_w2" in e and "contradicts" in e for e in errors)


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
            "payload": "1000x4 (move requests x options)"
          }
        },
      )
    ]
  )

  errors = cwm.check(catalog, {"spatial/local_coordination_1000x4"})

  assert not any("not reflected" in e for e in errors)


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
  # threads:N changes concurrency: it is identity-bearing and must
  # be classified through executor dimensions, never collapsed.
  assert cwm.canonical("path/x/threads:4") == "path/x/threads:4"
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
  """The shipped catalog classifies the full static universe.

  Static = threshold manifests + lab literals; the compiled
  registration union (with expanded /arg names) is checked by the
  bench CI job against the same catalog.
  """
  repo = Path(__file__).resolve().parents[1]
  catalog = json.loads(
    (repo / "bench" / "workload-matrix.json").read_text(encoding="utf-8")
  )
  universe = cwm.threshold_registrations(
    repo / "bench" / "thresholds"
  ) | cwm.lab_registrations(repo / "bench")

  errors = cwm.check(catalog, universe)

  assert errors == []


def test_extent_token_contradicting_the_cell_fails():
  # A name asserting 1024x1024 while the family default says 512x512
  # and nothing captures the token must fail, not silently default.
  catalog = _catalog(
    families=[
      _family(
        pattern=r"^path/astar_open_2d_1024x1024$",
        captures={},
      )
    ]
  )

  errors = cwm.check(catalog, {"path/astar_open_2d_1024x1024"})

  assert any("1024x1024" in e and "not reflected" in e for e in errors)


def test_partial_extent_capture_still_fails():
  # Capturing only one half of an extent token must not count as
  # consumption (the value-agreement rule requires the full token).
  catalog = _catalog(
    families=[
      _family(
        pattern=r"^path/astar_open_2d_(1024)x1024$",
        captures={"payload": 1},
      )
    ]
  )

  errors = cwm.check(catalog, {"path/astar_open_2d_1024x1024"})

  assert any("not reflected" in e for e in errors)


def test_partial_executor_capture_still_fails():
  # Capturing the kind but defaulting the width must fail: pool_w4
  # with worker_count defaulting to 1 is a silent lie.
  catalog = _catalog(
    families=[
      _family(
        family="parallel/fill",
        pattern=r"^parallel/chunk_fill_(pool)_w4$",
        captures={"executor_kind": 1},
        defaults={
          "world_extent": "512x512",
          "chunk_extent": "32x32",
          "layout": "not_applicable",
          "storage": "always_resident",
          "executor_kind": "pool",
          "worker_count": "1",
          "payload": "not_applicable",
        },
      )
    ]
  )

  errors = cwm.check(catalog, {"parallel/chunk_fill_pool_w4"})

  assert any("contradicts" in e for e in errors)


def test_policy_selector_is_exempt_from_retirement():
  catalog = _catalog(
    families=[_family()],
    unmeasured=[
      {
        "selector": {"layout": "open"},
        "reason": "open-ended scope statement",
        "policy": True,
      }
    ],
  )

  errors = cwm.check(catalog, {"path/astar_open_2d"})

  assert errors == []


def test_family_selector_scopes_retirement():
  # A gap can be family-scoped: another family supplying the same
  # dimensions must not retire it.
  families = [
    _family(),
    _family(
      family="path/other",
      pattern=r"^path/other_open$",
      captures={},
    ),
  ]
  catalog = _catalog(
    families=families,
    unmeasured=[
      {
        "selector": {"family": "path/other", "layout": "room_portals"},
        "reason": "room portals unmeasured for this operation",
      }
    ],
  )

  errors = cwm.check(
    catalog, {"path/astar_open_2d", "path/other_open"}
  )

  assert errors == []


def test_selector_vocabulary_typo_fails():
  catalog = _catalog(
    families=[_family()],
    unmeasured=[
      {
        "selector": {"executor_kind": "pools"},
        "reason": "typo can never retire",
      }
    ],
  )

  errors = cwm.check(catalog, {"path/astar_open_2d"})

  assert any("vocabulary" in e or "typo" in e for e in errors)


def test_unknown_family_in_selector_fails():
  catalog = _catalog(
    families=[_family()],
    unmeasured=[
      {
        "selector": {"family": "path/nonexistent"},
        "reason": "names a family that does not exist",
      }
    ],
  )

  errors = cwm.check(catalog, {"path/astar_open_2d"})

  assert any("path/nonexistent" in e for e in errors)


def test_unanchored_pattern_fails():
  catalog = _catalog(
    families=[_family(pattern=r"path/astar_open_2d")]
  )

  errors = cwm.check(catalog, {"path/astar_open_2d"})

  assert any("anchored" in e for e in errors)


def test_stale_override_key_fails():
  catalog = _catalog(
    families=[
      _family(
        overrides={"path/astar_open_2d_renamed": {"layout": "open"}}
      )
    ]
  )

  errors = cwm.check(catalog, {"path/astar_open_2d"})

  assert any("override key" in e for e in errors)


def test_removed_override_target_fails():
  # Override key matches the pattern but the registration is gone.
  catalog = _catalog(
    families=[
      _family(
        overrides={
          "path/astar_open_2d_64x64": {"chunk_extent": "8x8"}
        }
      )
    ]
  )

  errors = cwm.check(catalog, {"path/astar_open_2d"})

  assert any("matches no registration" in e and "override" in e
             for e in errors)


def test_bad_regex_reports_instead_of_raising():
  catalog = _catalog(families=[_family(pattern=r"^path/astar[$")])

  errors = cwm.check(catalog, {"path/astar_open_2d"})

  assert any("bad pattern" in e for e in errors)


def test_malformed_catalog_exits_nonzero_without_traceback(
  tmp_path, capsys
):
  catalog_file = tmp_path / "matrix.json"
  catalog_file.write_text(
    json.dumps({"families": [{"pattern": 42}]}), encoding="utf-8"
  )
  thresholds = tmp_path / "thresholds"
  thresholds.mkdir()
  (thresholds / "x.json").write_text(
    json.dumps({"benchmarks": [{"name": "path/x"}]}), encoding="utf-8"
  )

  code = cwm.main(
    [f"--catalog={catalog_file}", f"--thresholds-dir={thresholds}"]
  )

  assert code == 1


def test_misspelled_capture_dimension_fails():
  catalog = _catalog(
    families=[_family(captures={"world_extnet": 1})]
  )

  errors = cwm.check(catalog, {"path/astar_open_2d"})

  assert any("world_extnet" in e and "not a dimension" in e
             for e in errors)


def test_duplicate_family_identifier_fails():
  catalog = _catalog(families=[_family(), _family()])

  errors = cwm.check(catalog, {"path/astar_open_2d"})

  assert any("more than once" in e for e in errors)


def test_lab_literals_in_comments_are_ignored(tmp_path):
  source = tmp_path / "tess_lab_bench.cc"
  source.write_text(
    'RegisterBenchmark("lab/probe_active", fn);\n'
    '// RegisterBenchmark("lab/probe_disabled", fn);\n'
    '/* historical: "lab/probe_removed" was retired */\n',
    encoding="utf-8",
  )

  names = cwm.lab_registrations(tmp_path)

  assert names == {"lab/probe_active"}


def test_top_level_alternation_cannot_escape_anchoring():
  # fullmatch is structural: the right branch of a top-level
  # alternation cannot match as an unanchored substring.
  catalog = _catalog(
    families=[
      _family(
        pattern=r"^path/astar_open_2d|special$",
        captures={},
      )
    ]
  )

  errors = cwm.check(
    catalog, {"path/astar_open_2d", "path/very_special_thing"}
  )

  assert any("path/very_special_thing" in e and "no family rule" in e
             for e in errors)


def test_non_string_selector_value_fails():
  # A JSON integer worker_count would never equal the string cell
  # value, silently disabling retirement forever.
  catalog = _catalog(
    families=[_family()],
    unmeasured=[
      {
        "selector": {"executor_kind": "pool", "worker_count": 8},
        "reason": "written the natural JSON way",
      }
    ],
  )

  errors = cwm.check(catalog, {"path/astar_open_2d"})

  assert any("worker_count" in e or "vocabulary" in e for e in errors)


def test_min_time_suffixes_are_control_suffixes():
  assert cwm.canonical("path/x/min_time:2.000") == "path/x"
  assert cwm.canonical("path/x/min_warmup_time:0.5") == "path/x"
