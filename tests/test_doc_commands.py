"""Tests for the documented-build-command checker."""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import check_doc_commands as doc_commands  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parents[1]


def _repo(tmp_path: Path, doc: str) -> Path:
  """A miniature repository: one preset file, one target, one document."""
  (tmp_path / "CMakePresets.json").write_text(
    '{"version": 6, "configurePresets": [{"name": "dev"}]}', encoding="utf-8"
  )
  (tmp_path / "CMakeLists.txt").write_text(
    "add_executable(tess_smoke smoke.cc)\n", encoding="utf-8"
  )
  (tmp_path / "guide.md").write_text(doc, encoding="utf-8")
  return tmp_path


def test_resolvable_preset_and_target_pass(tmp_path):
  root = _repo(
    tmp_path,
    "```sh\ncmake --preset dev --target tess_smoke\n```\n",
  )

  assert doc_commands.check(root) == []


def test_unknown_preset_is_reported(tmp_path):
  root = _repo(tmp_path, "```sh\ncmake --preset nope\n```\n")

  problems = doc_commands.check(root)

  assert len(problems) == 1
  assert "--preset 'nope'" in problems[0]


def test_unknown_target_is_reported(tmp_path):
  root = _repo(tmp_path, "```sh\ncmake --build . --target tess_gone\n```\n")

  problems = doc_commands.check(root)

  assert len(problems) == 1
  assert "--target 'tess_gone'" in problems[0]


def test_cpp_fences_are_not_scanned(tmp_path):
  # The snippet checker owns C++ fences. A preset name quoted inside one is
  # prose about a command, not a command, and reporting it would make the
  # two checkers disagree about the same block.
  root = _repo(tmp_path, "```cpp\n// cmake --preset imaginary\n```\n")

  assert doc_commands.check(root) == []


def test_placeholders_are_not_resolved(tmp_path):
  root = _repo(
    tmp_path,
    "```sh\ncmake --build --preset bench --target tess_bench_<suite>\n```\n",
  )

  problems = doc_commands.check(root)

  # `bench` is genuinely absent from this miniature preset file, so it is
  # reported; the placeholder target is not.
  assert len(problems) == 1
  assert "--preset 'bench'" in problems[0]


def test_multiple_targets_on_one_flag_are_all_checked(tmp_path):
  root = _repo(
    tmp_path,
    "```sh\ncmake --build . --target tess_smoke tess_gone\n```\n",
  )

  problems = doc_commands.check(root)

  assert len(problems) == 1
  assert "tess_gone" in problems[0]


def test_generated_threshold_targets_resolve(tmp_path):
  root = _repo(
    tmp_path,
    "```sh\ncmake --build . --target tess_bench_queued_thresholds\n```\n",
  )
  thresholds = root / "bench" / "thresholds"
  thresholds.mkdir(parents=True)
  (thresholds / "queued.json").write_text("{}", encoding="utf-8")

  assert doc_commands.check(root) == []


def test_the_real_repository_documents_only_resolvable_commands():
  assert doc_commands.check(REPO_ROOT) == []
