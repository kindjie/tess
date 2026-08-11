"""Tests for the installed-header stability manifest gate."""

from __future__ import annotations

import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import check_header_manifest as chm  # noqa: E402


def write_manifest(root: Path, classes: dict[str, list[str]]) -> Path:
  path = root / "cmake" / "tess-headers.json"
  path.parent.mkdir(parents=True)
  path.write_text(json.dumps(classes), encoding="utf-8")
  return path


def complete_classes() -> dict[str, list[str]]:
  return {
      "stable": [
          "include/tess/pathfinding.h",
          "include/tess/simulation.h",
          "include/tess/tess.h",
      ],
      "optional-stable": [],
      "experimental": ["include/tess/experimental/tool.h"],
      "implementation-only": ["include/tess/detail/fragment.h"],
  }


def make_headers(root: Path) -> None:
  for header in complete_classes()["stable"]:
    path = root / header
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("#pragma once\n", encoding="utf-8")
  for category in ("experimental", "implementation-only"):
    for header in complete_classes()[category]:
      path = root / header
      path.parent.mkdir(parents=True, exist_ok=True)
      path.write_text("#pragma once\n", encoding="utf-8")


def test_manifest_requires_exactly_one_class_per_header(tmp_path):
  make_headers(tmp_path)
  classes = complete_classes()
  classes["experimental"].append("include/tess/detail/fragment.h")
  extra = tmp_path / "include" / "tess" / "unclassified.h"
  extra.write_text("#pragma once\n", encoding="utf-8")

  failures = chm.check_manifest(tmp_path, write_manifest(tmp_path, classes))

  assert failures == [
      "include/tess/detail/fragment.h: classified as both experimental "
      "and implementation-only",
      "include/tess/unclassified.h: installed header is unclassified",
  ]


def test_stable_aggregate_cannot_import_excluded_classes(tmp_path):
  make_headers(tmp_path)
  aggregate = tmp_path / "include" / "tess" / "tess.h"
  aggregate.write_text(
      "#include <tess/experimental/tool.h>\n"
      "#include <tess/detail/fragment.h>\n",
      encoding="utf-8",
  )

  failures = chm.check_manifest(
      tmp_path, write_manifest(tmp_path, complete_classes())
  )

  assert failures == [
      "include/tess/tess.h: stable aggregate imports implementation-only "
      "header include/tess/detail/fragment.h",
      "include/tess/tess.h: stable aggregate imports experimental header "
      "include/tess/experimental/tool.h",
  ]


def test_real_manifest_is_exhaustive_and_aggregate_safe():
  assert chm.check_manifest(chm.REPO_ROOT, chm.DEFAULT_MANIFEST) == []
