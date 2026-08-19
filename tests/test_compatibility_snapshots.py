"""Tests for immutable 1.x compatibility snapshot validation."""

from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import check_compatibility_snapshots as snapshots  # noqa: E402


def make_repo(root: Path) -> tuple[Path, dict[str, object]]:
  headers = {
      "stable": [
          "include/tess/pathfinding.h",
          "include/tess/simulation.h",
          "include/tess/tess.h",
      ],
      "optional-stable": ["include/tess/optional.h"],
      "experimental": [],
      "implementation-only": [],
  }
  contents = {
      "include/tess/pathfinding.h": """#pragma once
namespace tess {
struct StableSymbol {
  int max_entries = 4;
  void set_limit(int value);
 private:
  int revision = 0;
};
}  // namespace tess
""",
      "include/tess/simulation.h": """#pragma once
namespace tess { void simulate(int steps); }
""",
      "include/tess/tess.h": """#pragma once
#include <tess/pathfinding.h>
#include <tess/simulation.h>
#define TESS_COMPAT_LIMIT 8
namespace tess {
struct StableOptions { int max_steps = 1; };
[[nodiscard]] auto stable_route(int start, int goal) -> bool;
}  // namespace tess
""",
      "include/tess/optional.h": """#pragma once
namespace tess { void optional_route(int value); }
""",
  }
  for header, text in contents.items():
    path = root / header
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
  header_path = root / "cmake/tess-headers.json"
  header_path.parent.mkdir(parents=True)
  header_path.write_text(json.dumps(headers), encoding="utf-8")
  public_headers = headers["stable"] + headers["optional-stable"]
  payload: dict[str, object] = {
      "version": "1.0.0-rc.1",
      "headers": {
          "stable": headers["stable"],
          "optional-stable": headers["optional-stable"],
      },
      "aggregate_membership": snapshots.aggregate_membership(root),
      "public_symbols": snapshots.current_symbols(root, public_headers),
      "consumer": "consumer/main.cc",
      "consumer_project": "consumer",
      "consumer_target": "consumer",
      "archive_consumer": "archives/load.cc",
      "archive_consumer_target": "archive_consumer",
      "archives": [
          {
              "path": "archives/one.bin",
              "format": 1,
              "producer_version": "1.0.0-rc.1",
              "schema": "fixed-test-schema-v1",
          }
      ],
  }
  return header_path, payload


def write_snapshot(root: Path, payload: dict[str, object]) -> Path:
  directory = root / "compatibility/1.0.0-rc.1"
  (directory / "consumer").mkdir(parents=True)
  (directory / "archives").mkdir()
  (directory / "consumer/main.cc").write_text("int main() {}\n")
  (directory / "consumer/CMakeLists.txt").write_text(
      """cmake_minimum_required(VERSION 3.25)
project(tess_compatibility_consumer LANGUAGES CXX)
find_package(tess CONFIG REQUIRED)
add_executable(consumer main.cc)
target_link_libraries(consumer PRIVATE tess::tess)
add_executable(archive_consumer ../archives/load.cc)
target_link_libraries(archive_consumer PRIVATE tess::tess)
enable_testing()
add_test(NAME consumer COMMAND consumer)
add_test(NAME archive_consumer
  COMMAND archive_consumer "${TESS_SNAPSHOT_DIR}"
)
""",
      encoding="utf-8",
  )
  (directory / "archives/load.cc").write_text("int main() {}\n")
  (directory / "archives/one.bin").write_bytes(b"fixture")
  (directory / "manifest.json").write_text(
      json.dumps(payload), encoding="utf-8"
  )
  return root / "compatibility"


def check_previous(
    root: Path, snapshot_root: Path, header_path: Path
) -> list[str]:
  return snapshots.check_snapshots(
      root, snapshot_root, header_path, "1.1.1"
  )


def mutate_header(root: Path, old: str, new: str) -> None:
  header = root / "include/tess/tess.h"
  text = header.read_text(encoding="utf-8")
  assert old in text
  header.write_text(text.replace(old, new), encoding="utf-8")


def test_valid_snapshot_matches_current_sources(tmp_path):
  header_path, payload = make_repo(tmp_path)
  snapshot_root = write_snapshot(tmp_path, payload)

  assert snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.0.0-rc.1"
  ) == []


def test_rc1_requires_its_exact_snapshot(tmp_path):
  header_path, _ = make_repo(tmp_path)

  assert snapshots.check_snapshots(
      tmp_path, tmp_path / "compatibility", header_path, "1.0.0-rc.1"
  ) == ["release 1.0.0-rc.1: compatibility snapshot is missing"]


def test_current_snapshot_must_match_current_inventories(tmp_path):
  header_path, payload = make_repo(tmp_path)
  payload["headers"] = {"stable": [], "optional-stable": []}
  payload["aggregate_membership"] = {
      aggregate: [] for aggregate in snapshots.AGGREGATES
  }
  payload["public_symbols"] = {}
  snapshot_root = write_snapshot(tmp_path, payload)

  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.0.0-rc.1"
  )

  assert any("current stable header inventory" in item for item in failures)
  assert any(
      "current optional-stable header inventory" in item
      for item in failures
  )
  assert any("current aggregate membership" in item for item in failures)
  assert any("current public symbol inventory" in item for item in failures)


def test_removed_public_symbol_fails(tmp_path):
  header_path, payload = make_repo(tmp_path)
  snapshot_root = write_snapshot(tmp_path, payload)
  mutate_header(
      tmp_path,
      "stable_route",
      "renamed_route",
  )

  failures = check_previous(tmp_path, snapshot_root, header_path)

  assert any(
      "public symbol removed" in failure and "stable_route" in failure
      for failure in failures
  )


def test_public_symbols_are_preserved_per_header(tmp_path):
  header_path, payload = make_repo(tmp_path)
  tess = tmp_path / "include/tess/tess.h"
  optional = tmp_path / "include/tess/optional.h"
  tess.write_text(
      tess.read_text(encoding="utf-8").replace(
          "struct StableOptions", "struct SharedName"
      ),
      encoding="utf-8",
  )
  optional.write_text(
      optional.read_text(encoding="utf-8").replace(
          "void optional_route", "struct SharedName {}; void optional_route"
      ),
      encoding="utf-8",
  )
  public_headers = [
      "include/tess/pathfinding.h",
      "include/tess/simulation.h",
      "include/tess/tess.h",
      "include/tess/optional.h",
  ]
  payload["public_symbols"] = snapshots.current_symbols(
      tmp_path, public_headers
  )
  snapshot_root = write_snapshot(tmp_path, payload)
  tess.write_text(
      tess.read_text(encoding="utf-8").replace("SharedName", "RenamedName"),
      encoding="utf-8",
  )

  failures = check_previous(tmp_path, snapshot_root, header_path)

  assert any(
      "public symbol removed from include/tess/tess.h: SharedName" in failure
      for failure in failures
  )


def test_removed_header_and_aggregate_member_fail(tmp_path):
  header_path, payload = make_repo(tmp_path)
  snapshot_root = write_snapshot(tmp_path, payload)
  manifest = json.loads(header_path.read_text(encoding="utf-8"))
  manifest["stable"].remove("include/tess/pathfinding.h")
  header_path.write_text(json.dumps(manifest), encoding="utf-8")
  mutate_header(tmp_path, "#include <tess/pathfinding.h>\n", "")

  failures = check_previous(tmp_path, snapshot_root, header_path)

  assert any(
      "stable header removed or reclassified" in item for item in failures
  )
  assert any("aggregate member removed" in item for item in failures)


def test_archive_fixture_requires_fixed_schema_metadata(tmp_path):
  header_path, payload = make_repo(tmp_path)
  payload["archives"][0]["schema"] = ""
  snapshot_root = write_snapshot(tmp_path, payload)

  assert snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.0.0-rc.1"
  ) == ["1.0.0-rc.1: invalid or missing archive fixture metadata"]


def test_snapshot_consumer_must_use_installed_package_contract(tmp_path):
  header_path, payload = make_repo(tmp_path)
  snapshot_root = write_snapshot(tmp_path, payload)
  project = snapshot_root / "1.0.0-rc.1/consumer/CMakeLists.txt"
  project.write_text("add_executable(consumer main.cc)\n", encoding="utf-8")

  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.0.0-rc.1"
  )

  assert any(
      "consumer project must discover tess CONFIG" in item
      for item in failures
  )


def test_cmake_contract_cannot_be_satisfied_by_comments(tmp_path):
  header_path, payload = make_repo(tmp_path)
  snapshot_root = write_snapshot(tmp_path, payload)
  project = snapshot_root / "1.0.0-rc.1/consumer/CMakeLists.txt"
  project.write_text(
      """cmake_minimum_required(VERSION 3.25)
project(empty LANGUAGES CXX)
#[[
find_package(tess CONFIG REQUIRED)
add_executable(consumer main.cc)
target_link_libraries(consumer PRIVATE tess::tess)
add_executable(archive_consumer ../archives/load.cc)
target_link_libraries(archive_consumer PRIVATE tess::tess)
add_test(NAME consumer COMMAND consumer)
add_test(NAME archive_consumer
  COMMAND archive_consumer TESS_SNAPSHOT_DIR)
]]
""",
      encoding="utf-8",
  )

  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.0.0-rc.1"
  )

  assert any(
      "consumer project must discover tess CONFIG" in item
      for item in failures
  )


def test_cmake_contract_rejects_commands_in_dead_branch(tmp_path):
  header_path, payload = make_repo(tmp_path)
  snapshot_root = write_snapshot(tmp_path, payload)
  project = snapshot_root / "1.0.0-rc.1/consumer/CMakeLists.txt"
  original = project.read_text(encoding="utf-8")
  project.write_text(f"if(FALSE)\n{original}endif()\n", encoding="utf-8")

  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.0.0-rc.1"
  )

  assert any(
      "consumer project must discover tess CONFIG" in item
      for item in failures
  )


def test_cmake_contract_requires_enable_testing(tmp_path):
  header_path, payload = make_repo(tmp_path)
  snapshot_root = write_snapshot(tmp_path, payload)
  project = snapshot_root / "1.0.0-rc.1/consumer/CMakeLists.txt"
  text = project.read_text(encoding="utf-8")
  project.write_text(text.replace("enable_testing()\n", ""), encoding="utf-8")

  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.0.0-rc.1"
  )

  assert any(
      "consumer project must discover tess CONFIG" in item
      for item in failures
  )


def test_cmake_contract_requires_exact_snapshot_argument(tmp_path):
  header_path, payload = make_repo(tmp_path)
  snapshot_root = write_snapshot(tmp_path, payload)
  project = snapshot_root / "1.0.0-rc.1/consumer/CMakeLists.txt"
  text = project.read_text(encoding="utf-8")
  project.write_text(
      text.replace("${TESS_SNAPSHOT_DIR}", "NOT_TESS_SNAPSHOT_DIR"),
      encoding="utf-8",
  )

  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.0.0-rc.1"
  )

  assert any(
      "consumer project must discover tess CONFIG" in item
      for item in failures
  )


def test_cmake_contract_rejects_snapshot_variable_as_test_property(tmp_path):
  header_path, payload = make_repo(tmp_path)
  snapshot_root = write_snapshot(tmp_path, payload)
  project = snapshot_root / "1.0.0-rc.1/consumer/CMakeLists.txt"
  text = project.read_text(encoding="utf-8")
  project.write_text(
      text.replace(
          'COMMAND archive_consumer "${TESS_SNAPSHOT_DIR}"',
          'COMMAND archive_consumer WORKING_DIRECTORY "${TESS_SNAPSHOT_DIR}"',
      ),
      encoding="utf-8",
  )

  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.0.0-rc.1"
  )

  assert any(
      "consumer project must discover tess CONFIG" in item
      for item in failures
  )


def test_cmake_contract_rejects_literal_snapshot_argument(tmp_path):
  header_path, payload = make_repo(tmp_path)
  snapshot_root = write_snapshot(tmp_path, payload)
  project = snapshot_root / "1.0.0-rc.1/consumer/CMakeLists.txt"
  text = project.read_text(encoding="utf-8")
  project.write_text(
      text.replace(
          '"${TESS_SNAPSHOT_DIR}"', '[=[${TESS_SNAPSHOT_DIR}]=]'
      ),
      encoding="utf-8",
  )

  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.0.0-rc.1"
  )

  assert any(
      "consumer project must discover tess CONFIG" in item
      for item in failures
  )


def test_cmake_contract_rejects_interface_only_linkage(tmp_path):
  header_path, payload = make_repo(tmp_path)
  snapshot_root = write_snapshot(tmp_path, payload)
  project = snapshot_root / "1.0.0-rc.1/consumer/CMakeLists.txt"
  text = project.read_text(encoding="utf-8")
  project.write_text(text.replace(" PRIVATE ", " INTERFACE "), encoding="utf-8")

  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.0.0-rc.1"
  )

  assert any(
      "consumer project must discover tess CONFIG" in item
      for item in failures
  )


def test_cmake_contract_rejects_inactive_test_configuration(tmp_path):
  header_path, payload = make_repo(tmp_path)
  snapshot_root = write_snapshot(tmp_path, payload)
  project = snapshot_root / "1.0.0-rc.1/consumer/CMakeLists.txt"
  text = project.read_text(encoding="utf-8")
  project.write_text(
      text.replace(
          "add_test(NAME consumer COMMAND consumer)",
          "add_test(NAME consumer COMMAND consumer CONFIGURATIONS Never)",
      ),
      encoding="utf-8",
  )

  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.0.0-rc.1"
  )

  assert any(
      "consumer project must discover tess CONFIG" in item
      for item in failures
  )


def test_cmake_contract_rejects_link_scope_switch(tmp_path):
  header_path, payload = make_repo(tmp_path)
  snapshot_root = write_snapshot(tmp_path, payload)
  project = snapshot_root / "1.0.0-rc.1/consumer/CMakeLists.txt"
  text = project.read_text(encoding="utf-8")
  project.write_text(
      text.replace(
          "PRIVATE tess::tess", "PRIVATE other INTERFACE tess::tess"
      ),
      encoding="utf-8",
  )

  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.0.0-rc.1"
  )

  assert any(
      "consumer project must discover tess CONFIG" in item
      for item in failures
  )


def test_cmake_contract_rejects_extra_executable_source(tmp_path):
  header_path, payload = make_repo(tmp_path)
  snapshot_root = write_snapshot(tmp_path, payload)
  project = snapshot_root / "1.0.0-rc.1/consumer/CMakeLists.txt"
  text = project.read_text(encoding="utf-8")
  project.write_text(
      text.replace("consumer main.cc)", "consumer main.cc extra.cc)"),
      encoding="utf-8",
  )

  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.0.0-rc.1"
  )

  assert any(
      "consumer project must discover tess CONFIG" in item
      for item in failures
  )


def test_cmake_contract_requires_distinct_targets(tmp_path):
  header_path, payload = make_repo(tmp_path)
  payload["archive_consumer_target"] = "consumer"
  snapshot_root = write_snapshot(tmp_path, payload)
  project = snapshot_root / "1.0.0-rc.1/consumer/CMakeLists.txt"
  text = project.read_text(encoding="utf-8")
  project.write_text(
      text.replace("archive_consumer", "consumer"), encoding="utf-8"
  )

  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.0.0-rc.1"
  )

  assert any(
      "consumer project must discover tess CONFIG" in item
      for item in failures
  )


def test_cmake_contract_requires_distinct_sources(tmp_path):
  header_path, payload = make_repo(tmp_path)
  payload["archive_consumer"] = "consumer/main.cc"
  snapshot_root = write_snapshot(tmp_path, payload)
  project = snapshot_root / "1.0.0-rc.1/consumer/CMakeLists.txt"
  text = project.read_text(encoding="utf-8")
  project.write_text(
      text.replace("../archives/load.cc", "main.cc"), encoding="utf-8"
  )

  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.0.0-rc.1"
  )

  assert any(
      "consumer project must discover tess CONFIG" in item
      for item in failures
  )


def test_cmake_contract_binds_sources_to_recorded_targets(tmp_path):
  header_path, payload = make_repo(tmp_path)
  snapshot_root = write_snapshot(tmp_path, payload)
  project = snapshot_root / "1.0.0-rc.1/consumer/CMakeLists.txt"
  project.write_text(
      """find_package(tess CONFIG REQUIRED)
add_executable(consumer ../archives/load.cc)
target_link_libraries(consumer PRIVATE tess::tess)
add_executable(archive_consumer main.cc)
target_link_libraries(archive_consumer PRIVATE tess::tess)
add_test(NAME consumer COMMAND archive_consumer)
add_test(NAME archive_consumer
  COMMAND consumer TESS_SNAPSHOT_DIR)
""",
      encoding="utf-8",
  )

  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.0.0-rc.1"
  )

  assert any(
      "consumer project must discover tess CONFIG" in item
      for item in failures
  )


def test_cmake_contract_distinguishes_equal_source_basenames(tmp_path):
  header_path, payload = make_repo(tmp_path)
  snapshot_root = write_snapshot(tmp_path, payload)
  archive = snapshot_root / "1.0.0-rc.1/archives/load.cc"
  renamed = archive.with_name("main.cc")
  archive.rename(renamed)
  payload["archive_consumer"] = "archives/main.cc"
  manifest = snapshot_root / "1.0.0-rc.1/manifest.json"
  manifest.write_text(json.dumps(payload), encoding="utf-8")
  project = snapshot_root / "1.0.0-rc.1/consumer/CMakeLists.txt"
  project.write_text(
      """find_package(tess CONFIG REQUIRED)
add_executable(consumer main.cc)
target_link_libraries(consumer PRIVATE tess::tess)
add_executable(archive_consumer main.cc)
target_link_libraries(archive_consumer PRIVATE tess::tess)
add_test(NAME consumer COMMAND consumer)
add_test(NAME archive_consumer
  COMMAND archive_consumer TESS_SNAPSHOT_DIR)
""",
      encoding="utf-8",
  )

  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.0.0-rc.1"
  )

  assert any(
      "consumer project must discover tess CONFIG" in item
      for item in failures
  )


def test_missing_consumer_cmakelists_is_reported(tmp_path):
  header_path, payload = make_repo(tmp_path)
  snapshot_root = write_snapshot(tmp_path, payload)
  project = snapshot_root / "1.0.0-rc.1/consumer/CMakeLists.txt"
  project.unlink()

  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.0.0-rc.1"
  )

  assert any(
      "consumer project must discover tess CONFIG" in item
      for item in failures
  )


def test_snapshot_paths_cannot_escape_version_directory(tmp_path):
  header_path, payload = make_repo(tmp_path)
  payload["consumer"] = "../outside.cc"
  snapshot_root = write_snapshot(tmp_path, payload)

  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.0.0-rc.1"
  )

  assert any("consumer path must stay inside" in item for item in failures)


@pytest.mark.parametrize(
    "archive_path",
    (
        "archives\\one.bin",
        "archives/bad:name.bin",
        "archives/CON.bin",
        "archives/one.bin.",
    ),
)
def test_snapshot_paths_must_be_portable_posix_relative(
    tmp_path, archive_path
):
  header_path, payload = make_repo(tmp_path)
  snapshot_root = write_snapshot(tmp_path, payload)
  directory = snapshot_root / "1.0.0-rc.1"
  source = directory / "archives/one.bin"
  destination = directory / archive_path
  destination.parent.mkdir(parents=True, exist_ok=True)
  source.rename(destination)
  payload["archives"][0]["path"] = archive_path
  (directory / "manifest.json").write_text(
      json.dumps(payload), encoding="utf-8"
  )

  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.0.0-rc.1"
  )

  assert any("archive path must stay inside" in item for item in failures)


@pytest.mark.parametrize("archive_format", (True, 1.0))
def test_archive_format_must_be_an_integer(tmp_path, archive_format):
  header_path, payload = make_repo(tmp_path)
  payload["archives"][0]["format"] = archive_format
  snapshot_root = write_snapshot(tmp_path, payload)

  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.0.0-rc.1"
  )

  assert any(
      "invalid or missing archive fixture metadata" in item
      for item in failures
  )


def git(root: Path, *arguments: str) -> subprocess.CompletedProcess[str]:
  return subprocess.run(
      ["git", *arguments], cwd=root, check=True, capture_output=True, text=True
  )


def initialize_git_snapshot(root: Path) -> tuple[Path, Path]:
  header_path, payload = make_repo(root)
  snapshot_root = write_snapshot(root, payload)
  git(root, "init")
  git(root, "config", "user.name", "Snapshot Test")
  git(root, "config", "user.email", "snapshot")
  git(root, "config", "commit.gpgsign", "false")
  git(root, "config", "core.hooksPath", "/dev/null")
  git(root, "add", ".")
  git(root, "commit", "-m", "snapshot")
  git(root, "tag", "v1.0.0-rc.1")
  return header_path, snapshot_root


def test_released_snapshot_must_match_its_release_tag(tmp_path):
  _, snapshot_root = initialize_git_snapshot(tmp_path)
  manifest = snapshot_root / "1.0.0-rc.1/manifest.json"
  manifest.write_text("{}\n", encoding="utf-8")

  assert snapshots.check_snapshot_immutability(
      tmp_path, snapshot_root, "1.1.1"
  ) == [
      "1.0.0-rc.1: released snapshot differs from tag v1.0.0-rc.1"
  ]


def test_released_snapshot_directory_cannot_be_deleted(tmp_path):
  _, snapshot_root = initialize_git_snapshot(tmp_path)
  subprocess.run(
      ["git", "rm", "-r", "compatibility/1.0.0-rc.1"],
      cwd=tmp_path,
      check=True,
      capture_output=True,
      text=True,
  )

  assert snapshots.check_snapshot_immutability(
      tmp_path, snapshot_root, "1.1.1"
  ) == ["1.0.0-rc.1: released snapshot directory is missing"]


def test_future_unmerged_tag_does_not_require_its_snapshot(tmp_path):
  _, snapshot_root = initialize_git_snapshot(tmp_path)
  initial_branch = git(tmp_path, "branch", "--show-current").stdout.strip()
  git(tmp_path, "switch", "--orphan", "future")
  future = tmp_path / "future.txt"
  future.write_text("future\n", encoding="utf-8")
  git(tmp_path, "add", "future.txt")
  git(tmp_path, "commit", "-m", "future")
  git(tmp_path, "tag", "v1.1.0")
  git(tmp_path, "switch", initial_branch)

  assert snapshots.check_snapshot_immutability(
      tmp_path, snapshot_root, "1.0.1"
  ) == []


def test_version_policy():
  assert not snapshots.release_requires_snapshot("0.13.0")
  assert snapshots.release_requires_snapshot("1.0.0-rc.1")
  assert snapshots.release_requires_snapshot("1.0.0")
  assert snapshots.release_requires_snapshot("1.2.0")
  assert not snapshots.release_requires_snapshot("1.2.1")
