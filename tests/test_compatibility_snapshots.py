"""Tests for immutable 1.x compatibility snapshot validation."""

from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import check_compatibility_snapshots as snapshots  # noqa: E402
from api_contract import extract_api_contract  # noqa: E402


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
  for header in headers["stable"] + headers["optional-stable"]:
    path = root / header
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("#pragma once\nstruct StableSymbol {};\n", encoding="utf-8")
  aggregate = root / "include/tess/tess.h"
  aggregate.write_text(
      """#pragma once
#include <tess/pathfinding.h>
#define TESS_COMPAT_LIMIT 8
namespace tess {
enum class StableMode { First = 1, Second = 2 };
struct StableSymbol {
  int max_entries = 4;
  void set_limit(int value = 8);
  auto operator=(const StableSymbol& other) -> StableSymbol& {
    max_entries = other.max_entries;
    return *this;
  }
 private:
  int internal_revision = 1;
};
struct StableOptions {
  int max_steps = 1;
};
[[nodiscard]] auto stable_route(int start, int goal = 2) -> bool;
constexpr auto stable_inline(int value) -> int { return value + 1; }
template <class T = int>
constexpr auto stable_template(T value) -> T { return value + 1; }
inline auto stable_braced(StableSymbol value = {}) -> bool { return true; }
}  // namespace tess
""",
      encoding="utf-8",
  )
  header_path = root / "cmake/tess-headers.json"
  header_path.parent.mkdir(parents=True)
  header_path.write_text(json.dumps(headers), encoding="utf-8")
  payload: dict[str, object] = {
      "version": "1.0.0-rc.1",
      "headers": {
          "stable": headers["stable"],
          "optional-stable": headers["optional-stable"],
      },
      "aggregate_membership": snapshots.aggregate_membership(root),
      "public_symbols": sorted(
          snapshots.current_symbols(
              root, headers["stable"] + headers["optional-stable"]
          )
      ),
      "api_contract": snapshots.current_api_contract(
          root, headers["stable"] + headers["optional-stable"]
      ),
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
add_test(
  NAME archive_consumer
  COMMAND archive_consumer "${TESS_SNAPSHOT_DIR}"
)
"""
  )
  (directory / "archives/load.cc").write_text("int main() {}\n")
  (directory / "archives/one.bin").write_bytes(b"fixture")
  (directory / "manifest.json").write_text(json.dumps(payload))
  return root / "compatibility"


def test_valid_snapshot_is_a_subset_of_current_sources(tmp_path):
  header_path, payload = make_repo(tmp_path)
  snapshot_root = write_snapshot(tmp_path, payload)
  assert any(
      contract.startswith("function tess:template < class T = int >")
      for contract in payload["api_contract"]["include/tess/tess.h"]
  )

  assert snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.0.0-rc.1"
  ) == []


def test_removed_contract_and_fixture_fail_deterministically(tmp_path):
  header_path, payload = make_repo(tmp_path)
  payload["headers"]["stable"].append("include/tess/removed.h")
  payload["aggregate_membership"]["include/tess/tess.h"].append(
      "include/tess/removed.h"
  )
  payload["public_symbols"].append("RemovedSymbol")
  payload["api_contract"]["include/tess/tess.h"].append(
      "data-member tess::StableSymbol::removed_member:int"
  )
  snapshot_root = write_snapshot(tmp_path, payload)
  (snapshot_root / "1.0.0-rc.1/archives/one.bin").unlink()

  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.1.1"
  )

  assert failures == [
      "1.0.0-rc.1: stable header removed or reclassified: "
      "include/tess/removed.h",
      "1.0.0-rc.1: aggregate member removed from include/tess/tess.h: "
      "include/tess/removed.h",
      "1.0.0-rc.1: public symbol removed: RemovedSymbol",
      "1.0.0-rc.1: api_contract for include/tess/removed.h is missing",
      "1.0.0-rc.1: API declaration changed or removed: "
      "include/tess/tess.h: data-member "
      "tess::StableSymbol::removed_member:int",
      "1.0.0-rc.1: invalid or missing archive fixture metadata",
  ]


def test_rc1_requires_its_exact_snapshot(tmp_path):
  header_path, _ = make_repo(tmp_path)

  assert snapshots.check_snapshots(
      tmp_path, tmp_path / "compatibility", header_path, "1.0.0-rc.1"
  ) == ["release 1.0.0-rc.1: compatibility snapshot is missing"]


def test_current_release_snapshot_must_match_current_inventories(tmp_path):
  header_path, payload = make_repo(tmp_path)
  payload["headers"] = {"stable": [], "optional-stable": []}
  payload["aggregate_membership"] = {
      aggregate: [] for aggregate in snapshots.AGGREGATES
  }
  payload["public_symbols"] = []
  payload["api_contract"] = {}
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
  assert any("current API contract" in item for item in failures)


def test_archive_fixture_requires_fixed_schema_metadata(tmp_path):
  header_path, payload = make_repo(tmp_path)
  payload["archives"][0]["schema"] = ""
  snapshot_root = write_snapshot(tmp_path, payload)

  assert snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.0.0-rc.1"
  ) == ["1.0.0-rc.1: invalid or missing archive fixture metadata"]


def test_api_contract_catches_source_incompatible_declaration_changes(
    tmp_path,
):
  header_path, payload = make_repo(tmp_path)
  snapshot_root = write_snapshot(tmp_path, payload)
  header = tmp_path / "include/tess/tess.h"

  replacements = (
      ("TESS_COMPAT_LIMIT 8", "TESS_COMPAT_LIMIT 9", "macro"),
      ("Second = 2", "Second = 3", "enumerator"),
      ("max_entries = 4", "entry_limit = 4", "member"),
      ("goal = 2", "goal = 3", "function"),
  )
  original = header.read_text(encoding="utf-8")
  for before, after, declaration_kind in replacements:
    header.write_text(original.replace(before, after), encoding="utf-8")

    failures = snapshots.check_snapshots(
        tmp_path, snapshot_root, header_path, "1.1.1"
    )

    assert any(
        "API declaration changed or removed" in failure
        and declaration_kind in failure
        for failure in failures
    ), failures
    header.write_text(original, encoding="utf-8")


def test_existing_public_type_cannot_gain_a_data_member(tmp_path):
  header_path, payload = make_repo(tmp_path)
  snapshot_root = write_snapshot(tmp_path, payload)
  header = tmp_path / "include/tess/tess.h"
  original = header.read_text(encoding="utf-8")
  for addition, name in (
      ("int additive_field = 0;", "additive_field"),
      ("void (*callback)(int);", "callback"),
      ("std::function<void(int)> handler;", "handler"),
      ("alignas(16) int aligned_field;", "aligned_field"),
      ("decltype(stable_inline(0)) derived_field;", "derived_field"),
  ):
    header.write_text(
        original.replace(
            "  int max_entries = 4;",
            f"  int max_entries = 4;\n  {addition}",
        ),
        encoding="utf-8",
    )

    failures = snapshots.check_snapshots(
        tmp_path, snapshot_root, header_path, "1.1.1"
    )

    assert any(
        "public data member added to existing type" in failure
        and name in failure
        for failure in failures
    ), failures


def test_existing_public_type_can_gain_methods_aliases_and_static_data(
    tmp_path,
):
  header_path, payload = make_repo(tmp_path)
  snapshot_root = write_snapshot(tmp_path, payload)
  header = tmp_path / "include/tess/tess.h"
  text = header.read_text(encoding="utf-8").replace(
      "  int max_entries = 4;",
      """  int max_entries = 4;
  using AddedAlias = int;
  static constexpr int added_constant = 1;
  void additive_method() const;""",
  )
  header.write_text(text, encoding="utf-8")

  assert snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.1.1"
  ) == []


def test_existing_callable_cannot_gain_an_ambiguous_overload(tmp_path):
  header_path, payload = make_repo(tmp_path)
  snapshot_root = write_snapshot(tmp_path, payload)
  header = tmp_path / "include/tess/tess.h"
  text = header.read_text(encoding="utf-8").replace(
      "  void set_limit(int value = 8);",
      "  void set_limit(int value = 8);\n  void set_limit(double value);",
  )
  header.write_text(text, encoding="utf-8")

  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.1.1"
  )

  assert any(
      "overload added to existing callable" in failure
      and "set_limit" in failure
      for failure in failures
  ), failures


def test_attributed_callable_cannot_gain_an_ambiguous_overload(tmp_path):
  header_path, payload = make_repo(tmp_path)
  header = tmp_path / "include/tess/tess.h"
  text = header.read_text(encoding="utf-8").replace(
      "[[nodiscard]] auto stable_route",
      '[[nodiscard("why")]] auto stable_route',
  )
  header.write_text(text, encoding="utf-8")
  payload["api_contract"] = snapshots.current_api_contract(
      tmp_path,
      payload["headers"]["stable"]
      + payload["headers"]["optional-stable"],
  )
  snapshot_root = write_snapshot(tmp_path, payload)
  header.write_text(
      text.replace(
          "[[nodiscard(\"why\")]] auto stable_route(int start, "
          "int goal = 2) -> bool;",
          "[[nodiscard(\"why\")]] auto stable_route(int start, "
          "int goal = 2) -> bool;\n"
          "auto stable_route(double start, double goal) -> bool;",
      ),
      encoding="utf-8",
  )

  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.1.1"
  )

  assert any(
      "overload added to existing callable" in failure
      and "stable_route" in failure
      for failure in failures
  ), failures


def test_snapshotted_macro_cannot_be_undefined(tmp_path):
  header_path, payload = make_repo(tmp_path)
  snapshot_root = write_snapshot(tmp_path, payload)
  header = tmp_path / "include/tess/tess.h"
  original = header.read_text(encoding="utf-8")

  for undefinition in (
      "#undef TESS_COMPAT_LIMIT\n",
      "#if TESS_DISABLE_COMPAT_LIMIT\n"
      "#undef TESS_COMPAT_LIMIT\n"
      "#endif\n",
  ):
    header.write_text(
        original.replace(
            "#define TESS_COMPAT_LIMIT 8\n",
            "#define TESS_COMPAT_LIMIT 8\n" + undefinition,
        ),
        encoding="utf-8",
    )

    failures = snapshots.check_snapshots(
        tmp_path, snapshot_root, header_path, "1.1.1"
    )

    assert any(
        "stable macro undefined" in failure
        and "TESS_COMPAT_LIMIT" in failure
        for failure in failures
    ), failures


def test_existing_aggregate_cannot_gain_constructor_or_private_state(
    tmp_path,
):
  header_path, payload = make_repo(tmp_path)
  snapshot_root = write_snapshot(tmp_path, payload)
  header = tmp_path / "include/tess/tess.h"
  original = header.read_text(encoding="utf-8")

  for addition in (
      "  StableOptions() = default;\n",
      " private:\n  int internal_state = 0;\n",
      " private:\n  alignas(16) int aligned_state;\n",
      " private:\n  decltype(stable_inline(0)) derived_state;\n",
  ):
    header.write_text(
        original.replace(
            "struct StableOptions {\n", "struct StableOptions {\n" + addition
        ),
        encoding="utf-8",
    )

    failures = snapshots.check_snapshots(
        tmp_path, snapshot_root, header_path, "1.1.1"
    )

    assert any(
        "aggregate compatibility changed" in failure
        and "StableOptions" in failure
        for failure in failures
    ), failures


def test_existing_aggregate_rejects_parenthesized_constructor_specifiers(
    tmp_path,
):
  header_path, payload = make_repo(tmp_path)
  snapshot_root = write_snapshot(tmp_path, payload)
  header = tmp_path / "include/tess/tess.h"
  original = header.read_text(encoding="utf-8")

  for constructor in (
      "  explicit(true) StableOptions(int value = 0);\n",
      '  [[deprecated("old")]] StableOptions() = default;\n',
  ):
    header.write_text(
        original.replace(
            "struct StableOptions {\n",
            "struct StableOptions {\n" + constructor,
        ),
        encoding="utf-8",
    )

    failures = snapshots.check_snapshots(
        tmp_path, snapshot_root, header_path, "1.1.1"
    )

    assert any(
        "aggregate compatibility changed" in failure
        and "StableOptions" in failure
        for failure in failures
    ), failures


def test_existing_derived_aggregate_cannot_gain_constructor(tmp_path):
  header_path, payload = make_repo(tmp_path)
  header = tmp_path / "include/tess/tess.h"
  text = header.read_text(encoding="utf-8").replace(
      "struct StableOptions {",
      """struct StableBase {
  int base_value = 1;
};
struct StableOptions : StableBase {""",
  )
  header.write_text(text, encoding="utf-8")
  payload["public_symbols"] = sorted(
      snapshots.current_symbols(
          tmp_path,
          payload["headers"]["stable"]
          + payload["headers"]["optional-stable"],
      )
  )
  payload["api_contract"] = snapshots.current_api_contract(
      tmp_path,
      payload["headers"]["stable"]
      + payload["headers"]["optional-stable"],
  )
  snapshot_root = write_snapshot(tmp_path, payload)
  header.write_text(
      text.replace(
          "struct StableOptions : StableBase {",
          "struct StableOptions : StableBase {\n"
          "  StableOptions() = default;",
      ),
      encoding="utf-8",
  )

  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.1.1"
  )

  assert any(
      "aggregate compatibility changed" in failure
      and "StableOptions" in failure
      for failure in failures
  ), failures


def test_existing_derived_aggregate_cannot_inherit_constructors(tmp_path):
  header_path, payload = make_repo(tmp_path)
  header = tmp_path / "include/tess/tess.h"
  text = header.read_text(encoding="utf-8").replace(
      "struct StableOptions {",
      """struct StableBase {
  explicit StableBase(int value);
};
struct StableOptions : StableBase {""",
  )
  header.write_text(text, encoding="utf-8")
  payload["public_symbols"] = sorted(
      snapshots.current_symbols(
          tmp_path,
          payload["headers"]["stable"]
          + payload["headers"]["optional-stable"],
      )
  )
  payload["api_contract"] = snapshots.current_api_contract(
      tmp_path,
      payload["headers"]["stable"]
      + payload["headers"]["optional-stable"],
  )
  snapshot_root = write_snapshot(tmp_path, payload)
  header.write_text(
      text.replace(
          "struct StableOptions : StableBase {",
          "struct StableOptions : StableBase {\n"
          "  using StableBase::StableBase;",
      ),
      encoding="utf-8",
  )

  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.1.1"
  )

  assert any(
      "aggregate compatibility changed" in failure
      and "StableOptions" in failure
      for failure in failures
  ), failures


def test_template_aggregate_cannot_inherit_dependent_constructors(tmp_path):
  header_path, payload = make_repo(tmp_path)
  header = tmp_path / "include/tess/tess.h"
  text = header.read_text(encoding="utf-8").replace(
      "struct StableOptions {",
      """template <class T>
struct StableBase {
  explicit StableBase(T value);
};
template <class T>
struct StableOptions : StableBase<T> {""",
  )
  header.write_text(text, encoding="utf-8")
  payload["public_symbols"] = sorted(
      snapshots.current_symbols(
          tmp_path,
          payload["headers"]["stable"]
          + payload["headers"]["optional-stable"],
      )
  )
  payload["api_contract"] = snapshots.current_api_contract(
      tmp_path,
      payload["headers"]["stable"]
      + payload["headers"]["optional-stable"],
  )
  snapshot_root = write_snapshot(tmp_path, payload)
  header.write_text(
      text.replace(
          "struct StableOptions : StableBase<T> {",
          "struct StableOptions : StableBase<T> {\n"
          "  using StableBase<T>::StableBase;",
      ),
      encoding="utf-8",
  )

  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.1.1"
  )

  assert any(
      "aggregate compatibility changed" in failure
      and "StableOptions" in failure
      for failure in failures
  ), failures


def test_conditional_class_additions_keep_the_enclosing_scope(tmp_path):
  header_path, payload = make_repo(tmp_path)
  snapshot_root = write_snapshot(tmp_path, payload)
  header = tmp_path / "include/tess/tess.h"
  original = header.read_text(encoding="utf-8")

  additions = (
      (
          "#if TESS_EXTRA_STATE\n  int extra_state = 0;\n#endif\n",
          "public data member added to existing type",
      ),
      (
          "#if TESS_EXTRA_CONSTRUCTOR\n"
          "  StableOptions() = default;\n"
          "#endif\n",
          "aggregate compatibility changed",
      ),
      (
          "#if TESS_EXTRA_OVERLOAD\n"
          "  void set_limit(double value);\n"
          "#endif\n",
          "overload added to existing callable",
      ),
  )
  for addition, expected in additions:
    insertion = (
        "struct StableOptions {\n"
        if "StableOptions" in addition
        else "struct StableSymbol {\n"
    )
    header.write_text(
        original.replace(insertion, insertion + addition),
        encoding="utf-8",
    )

    failures = snapshots.check_snapshots(
        tmp_path, snapshot_root, header_path, "1.1.1"
    )

    assert any(expected in failure for failure in failures), failures


def test_conditional_access_labels_retain_branch_identity(tmp_path):
  header_path, payload = make_repo(tmp_path)
  header = tmp_path / "include/tess/tess.h"
  text = header.read_text(encoding="utf-8").replace(
      "struct StableOptions {\n  int max_steps = 1;",
      """class StableOptions {
#if TESS_PUBLIC_OPTIONS
 public:
#endif
  int max_steps = 1;""",
  )
  header.write_text(text, encoding="utf-8")
  payload["api_contract"] = snapshots.current_api_contract(
      tmp_path,
      payload["headers"]["stable"]
      + payload["headers"]["optional-stable"],
  )
  snapshot_root = write_snapshot(tmp_path, payload)
  header.write_text(
      text.replace("TESS_PUBLIC_OPTIONS", "TESS_RENAMED_OPTIONS"),
      encoding="utf-8",
  )

  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.1.1"
  )

  assert any(
      "API declaration changed or removed" in failure
      and "max_steps" in failure
      for failure in failures
  ), failures


def test_conditional_access_join_retains_each_public_branch(tmp_path):
  header_path, payload = make_repo(tmp_path)
  header = tmp_path / "include/tess/tess.h"
  text = header.read_text(encoding="utf-8").replace(
      "struct StableOptions {\n  int max_steps = 1;",
      """class StableOptions {
#if TESS_FIRST_PUBLIC
 public:
#else
 public:
#endif
  int max_steps = 1;""",
  )
  header.write_text(text, encoding="utf-8")
  payload["api_contract"] = snapshots.current_api_contract(
      tmp_path,
      payload["headers"]["stable"]
      + payload["headers"]["optional-stable"],
  )
  snapshot_root = write_snapshot(tmp_path, payload)
  header.write_text(
      text.replace("#else\n public:", "#else\n private:"),
      encoding="utf-8",
  )

  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.1.1"
  )

  assert any(
      "API declaration changed or removed" in failure
      and "max_steps" in failure
      for failure in failures
  ), failures


def test_conditional_public_aggregate_cannot_gain_constructor(tmp_path):
  header_path, payload = make_repo(tmp_path)
  header = tmp_path / "include/tess/tess.h"
  text = header.read_text(encoding="utf-8").replace(
      "struct StableOptions {\n  int max_steps = 1;",
      """class StableOptions {
#if TESS_PUBLIC_OPTIONS
 public:
#endif
  int max_steps = 1;""",
  )
  header.write_text(text, encoding="utf-8")
  payload["api_contract"] = snapshots.current_api_contract(
      tmp_path,
      payload["headers"]["stable"]
      + payload["headers"]["optional-stable"],
  )
  snapshot_root = write_snapshot(tmp_path, payload)
  header.write_text(
      text.replace(
          "  int max_steps = 1;",
          """#if TESS_PUBLIC_OPTIONS
  StableOptions() = default;
#endif
  int max_steps = 1;""",
      ),
      encoding="utf-8",
  )

  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.1.1"
  )

  assert any(
      "aggregate compatibility changed" in failure
      and "StableOptions" in failure
      for failure in failures
  ), failures


def test_conditional_enums_and_enumerators_retain_branch_identity(tmp_path):
  header_path, payload = make_repo(tmp_path)
  header = tmp_path / "include/tess/tess.h"
  original = header.read_text(encoding="utf-8")

  variants = (
      original.replace(
          "enum class StableMode {",
          "#if TESS_STABLE_MODE\nenum class StableMode {",
      ).replace(
          "};\nstruct StableSymbol", "};\n#endif\nstruct StableSymbol", 1
      ),
      original.replace(
          "enum class StableMode { First = 1, Second = 2 };",
          """enum class StableMode {
#if TESS_FIRST_MODE
  First = 1,
#endif
  Second = 2
};""",
      ),
  )
  snapshot_root = tmp_path / "compatibility"
  for index, text in enumerate(variants):
    header.write_text(text, encoding="utf-8")
    payload["api_contract"] = snapshots.current_api_contract(
        tmp_path,
        payload["headers"]["stable"]
        + payload["headers"]["optional-stable"],
    )
    if index == 0:
      snapshot_root = write_snapshot(tmp_path, payload)
    else:
      manifest = snapshot_root / "1.0.0-rc.1/manifest.json"
      manifest.write_text(json.dumps(payload), encoding="utf-8")
    header.write_text(
        text.replace("TESS_STABLE_MODE", "TESS_RENAMED_MODE").replace(
            "TESS_FIRST_MODE", "TESS_RENAMED_FIRST_MODE"
        ),
        encoding="utf-8",
    )

    failures = snapshots.check_snapshots(
        tmp_path, snapshot_root, header_path, "1.1.1"
    )

    assert any(
        "API declaration changed or removed" in failure
        and "StableMode" in failure
        for failure in failures
    ), failures


def test_conditional_enum_else_branches_keep_exact_members_and_ordinals():
  contract = extract_api_contract(
      """enum class StableMode {
  First,
#if TESS_SECOND
  Second,
#else
  Alternative,
#endif
  Last
};
enum class CommaAfter {
#if TESS_LEFT
  Left
#else
  Right
#endif
  , Last
};
"""
  )

  last = next(item for item in contract if "StableMode::Last:" in item)
  comma_last = next(item for item in contract if "CommaAfter::Last:" in item)
  assert last.endswith("Last @index 2")
  assert comma_last.endswith("Last @index 1")
  assert any("CommaAfter::Left:" in item for item in contract)
  assert any("CommaAfter::Right:" in item for item in contract)


def test_implicit_enum_values_are_append_only(tmp_path):
  header_path, payload = make_repo(tmp_path)
  header = tmp_path / "include/tess/tess.h"
  text = header.read_text(encoding="utf-8").replace(
      "enum class StableMode { First = 1, Second = 2 };",
      "enum class ImplicitMode { First, Second };",
  )
  header.write_text(text, encoding="utf-8")
  payload["public_symbols"] = sorted(
      snapshots.current_symbols(
          tmp_path,
          payload["headers"]["stable"]
          + payload["headers"]["optional-stable"],
      )
  )
  payload["api_contract"] = snapshots.current_api_contract(
      tmp_path,
      payload["headers"]["stable"]
      + payload["headers"]["optional-stable"],
  )
  snapshot_root = write_snapshot(tmp_path, payload)
  header.write_text(
      text.replace(
          "enum class ImplicitMode { First, Second };",
          "enum class ImplicitMode { Inserted, First, Second };",
      ),
      encoding="utf-8",
  )

  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.1.1"
  )

  assert any(
      "API declaration changed or removed" in failure
      and "enumerator" in failure
      for failure in failures
  ), failures


def test_aggregate_membership_rejects_disabled_or_commented_include(tmp_path):
  header_path, payload = make_repo(tmp_path)
  aggregate = tmp_path / "include/tess/tess.h"
  original = aggregate.read_text(encoding="utf-8").replace(
      "#pragma once", "#pragma once\n#include <tess/pathfinding.h>"
  )
  aggregate.write_text(original, encoding="utf-8")
  payload["aggregate_membership"] = snapshots.aggregate_membership(tmp_path)
  snapshot_root = write_snapshot(tmp_path, payload)

  for replacement in (
      "#if 0\n#include <tess/pathfinding.h>\n#endif",
      "/* #include <tess/pathfinding.h> */",
  ):
    aggregate.write_text(
        original.replace(
            "#include <tess/pathfinding.h>", replacement
        ),
        encoding="utf-8",
    )
    failures = snapshots.check_snapshots(
        tmp_path, snapshot_root, header_path, "1.1.1"
    )
    assert any(
        "aggregate member removed from include/tess/tess.h" in failure
        and "include/tess/pathfinding.h" in failure
        for failure in failures
    ), failures


def test_released_snapshot_must_match_its_release_tag(tmp_path):
  header_path, payload = make_repo(tmp_path)
  snapshot_root = write_snapshot(tmp_path, payload)
  subprocess.run(["git", "init"], cwd=tmp_path, check=True)
  subprocess.run(
      [
          "git",
          "config",
          "user.email",
          "compatibility" + chr(64) + "example.invalid",
      ],
      cwd=tmp_path,
      check=True,
  )
  subprocess.run(
      ["git", "config", "user.name", "Compatibility Tests"],
      cwd=tmp_path,
      check=True,
  )
  subprocess.run(["git", "add", "."], cwd=tmp_path, check=True)
  subprocess.run(
      ["git", "-c", "commit.gpgsign=false", "commit", "-m", "snapshot"],
      cwd=tmp_path,
      check=True,
  )
  subprocess.run(
      ["git", "tag", "v1.0.0-rc.1"], cwd=tmp_path, check=True
  )
  manifest = snapshot_root / "1.0.0-rc.1/manifest.json"
  manifest.write_text(manifest.read_text() + "\n", encoding="utf-8")

  assert snapshots.check_snapshot_immutability(
      tmp_path, snapshot_root, "1.1.0"
  ) == [
      "1.0.0-rc.1: released snapshot differs from tag v1.0.0-rc.1"
  ]


def test_released_snapshot_directory_cannot_be_deleted(tmp_path):
  _, payload = make_repo(tmp_path)
  snapshot_root = write_snapshot(tmp_path, payload)
  subprocess.run(["git", "init"], cwd=tmp_path, check=True)
  subprocess.run(
      [
          "git",
          "config",
          "user.email",
          "compatibility" + chr(64) + "example.invalid",
      ],
      cwd=tmp_path,
      check=True,
  )
  subprocess.run(
      ["git", "config", "user.name", "Compatibility Tests"],
      cwd=tmp_path,
      check=True,
  )
  subprocess.run(["git", "add", "."], cwd=tmp_path, check=True)
  subprocess.run(
      ["git", "-c", "commit.gpgsign=false", "commit", "-m", "snapshot"],
      cwd=tmp_path,
      check=True,
  )
  subprocess.run(
      ["git", "tag", "v1.0.0-rc.1"], cwd=tmp_path, check=True
  )
  for path in sorted(
      (snapshot_root / "1.0.0-rc.1").rglob("*"), reverse=True
  ):
    if path.is_file():
      path.unlink()
    else:
      path.rmdir()
  (snapshot_root / "1.0.0-rc.1").rmdir()

  assert snapshots.check_snapshot_immutability(
      tmp_path, snapshot_root, "1.1.0"
  ) == [
      "1.0.0-rc.1: released snapshot directory is missing"
  ]


def test_future_unmerged_release_tag_does_not_require_its_snapshot(tmp_path):
  _, payload = make_repo(tmp_path)
  snapshot_root = write_snapshot(tmp_path, payload)
  subprocess.run(["git", "init"], cwd=tmp_path, check=True)
  subprocess.run(
      [
          "git",
          "config",
          "user.email",
          "compatibility" + chr(64) + "example.invalid",
      ],
      cwd=tmp_path,
      check=True,
  )
  subprocess.run(
      ["git", "config", "user.name", "Compatibility Tests"],
      cwd=tmp_path,
      check=True,
  )
  subprocess.run(["git", "add", "."], cwd=tmp_path, check=True)
  subprocess.run(
      ["git", "-c", "commit.gpgsign=false", "commit", "-m", "snapshot"],
      cwd=tmp_path,
      check=True,
  )
  subprocess.run(
      ["git", "tag", "v1.0.0-rc.1"], cwd=tmp_path, check=True
  )
  subprocess.run(
      ["git", "switch", "--orphan", "future-release"],
      cwd=tmp_path,
      check=True,
  )
  marker = tmp_path / "future-release.txt"
  marker.write_text("future\n", encoding="utf-8")
  subprocess.run(["git", "add", "future-release.txt"], cwd=tmp_path, check=True)
  subprocess.run(
      ["git", "-c", "commit.gpgsign=false", "commit", "-m", "future"],
      cwd=tmp_path,
      check=True,
  )
  subprocess.run(["git", "tag", "v1.1.0"], cwd=tmp_path, check=True)
  subprocess.run(["git", "switch", "master"], cwd=tmp_path, check=True)

  assert snapshots.check_snapshot_immutability(
      tmp_path, snapshot_root, "1.0.0-rc.1"
  ) == []


def test_current_release_snapshot_can_precede_its_tag(tmp_path):
  _, payload = make_repo(tmp_path)
  snapshot_root = write_snapshot(tmp_path, payload)
  subprocess.run(["git", "init"], cwd=tmp_path, check=True)

  assert snapshots.check_snapshot_immutability(
      tmp_path, snapshot_root, "1.0.0-rc.1"
  ) == []


def test_snapshot_consumer_must_use_installed_package_contract(tmp_path):
  header_path, payload = make_repo(tmp_path)
  snapshot_root = write_snapshot(tmp_path, payload)
  cmake_project = snapshot_root / "1.0.0-rc.1/consumer/CMakeLists.txt"
  cmake_project.write_text(
      "add_executable(consumer main.cc)\n", encoding="utf-8"
  )

  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.0.0-rc.1"
  )

  assert failures == [
      "1.0.0-rc.1: consumer project must discover tess CONFIG and link "
      "tess::tess, build both recorded consumers, and test them"
  ]


def test_api_contract_excludes_bodies_and_private_members(tmp_path):
  header_path, payload = make_repo(tmp_path)
  snapshot_root = write_snapshot(tmp_path, payload)
  header = tmp_path / "include/tess/tess.h"
  text = header.read_text(encoding="utf-8")
  text = text.replace("internal_revision = 1", "implementation_epoch = 2")
  text = text.replace("return value + 1", "return value + 2")
  text = text.replace("return true", "return false")
  text = text.replace(
      "max_entries = other.max_entries", "max_entries = other.max_entries + 1"
  )
  header.write_text(text, encoding="utf-8")

  assert snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.1.1"
  ) == []


def test_api_contract_retains_preprocessor_condition_context(tmp_path):
  header_path, payload = make_repo(tmp_path)
  snapshot_root = write_snapshot(tmp_path, payload)
  header = tmp_path / "include/tess/tess.h"
  text = header.read_text(encoding="utf-8")
  text = text.replace(
      "[[nodiscard]] auto stable_route",
      "#if 0\n[[nodiscard]] auto stable_route",
  ).replace(
      "constexpr auto stable_inline", "#endif\nconstexpr auto stable_inline"
  )
  header.write_text(text, encoding="utf-8")

  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.1.1"
  )

  assert any(
      "API declaration changed or removed" in failure
      and "stable_route" in failure
      for failure in failures
  ), failures


def test_api_contract_excludes_constructor_initializer_and_body(tmp_path):
  header_path, payload = make_repo(tmp_path)
  header = tmp_path / "include/tess/tess.h"
  text = header.read_text(encoding="utf-8").replace(
      "void set_limit(int value = 8);",
      """StableSymbol(int value) : max_entries{value} {
    max_entries += 1;
  }
  void set_limit(int value = 8);""",
  )
  header.write_text(text, encoding="utf-8")
  payload["api_contract"] = snapshots.current_api_contract(
      tmp_path,
      payload["headers"]["stable"] + payload["headers"]["optional-stable"],
  )
  snapshot_root = write_snapshot(tmp_path, payload)
  header.write_text(
      text.replace(
          "max_entries{value}", "max_entries{value + 1}"
      ).replace("max_entries += 1", "max_entries += 2"),
      encoding="utf-8",
  )

  assert snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.1.1"
  ) == []


def test_api_contract_excludes_conditionally_private_members(tmp_path):
  header_path, payload = make_repo(tmp_path)
  header = tmp_path / "include/tess/tess.h"
  text = header.read_text(encoding="utf-8").replace(
      "  int internal_revision = 1;",
      """#if TESS_PRIVATE_LAYOUT
  int internal_revision = 1;
#endif""",
  )
  header.write_text(text, encoding="utf-8")
  payload["api_contract"] = snapshots.current_api_contract(
      tmp_path,
      payload["headers"]["stable"] + payload["headers"]["optional-stable"],
  )
  snapshot_root = write_snapshot(tmp_path, payload)
  header.write_text(
      text.replace("internal_revision", "implementation_epoch"),
      encoding="utf-8",
  )

  assert snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.1.1"
  ) == []


def test_snapshot_paths_cannot_escape_version_directory(tmp_path):
  header_path, payload = make_repo(tmp_path)
  snapshot_root = write_snapshot(tmp_path, payload)
  shared = tmp_path / "compatibility/shared.cc"
  shared.write_text("int main() {}\n", encoding="utf-8")
  payload["consumer"] = "../shared.cc"
  manifest = snapshot_root / "1.0.0-rc.1/manifest.json"
  manifest.write_text(json.dumps(payload), encoding="utf-8")

  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.0.0-rc.1"
  )

  assert failures == [
      "1.0.0-rc.1: consumer path must stay inside the snapshot directory"
  ]


def test_snapshot_symlink_cannot_escape_version_directory(tmp_path):
  header_path, payload = make_repo(tmp_path)
  snapshot_root = write_snapshot(tmp_path, payload)
  outside = tmp_path / "outside.cc"
  outside.write_text("int main() {}\n", encoding="utf-8")
  link = snapshot_root / "1.0.0-rc.1/consumer/linked.cc"
  link.symlink_to(outside)
  payload["consumer"] = "consumer/linked.cc"
  manifest = snapshot_root / "1.0.0-rc.1/manifest.json"
  manifest.write_text(json.dumps(payload), encoding="utf-8")

  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.0.0-rc.1"
  )

  assert failures == [
      "1.0.0-rc.1: consumer path must stay inside the snapshot directory"
  ]


def test_snapshot_project_cannot_escape_version_directory(tmp_path):
  header_path, payload = make_repo(tmp_path)
  snapshot_root = write_snapshot(tmp_path, payload)
  outside = tmp_path / "outside-project"
  outside.mkdir()
  original = snapshot_root / "1.0.0-rc.1/consumer/CMakeLists.txt"
  (outside / "CMakeLists.txt").write_text(
      original.read_text(encoding="utf-8"), encoding="utf-8"
  )
  payload["consumer_project"] = "../../outside-project"
  manifest = snapshot_root / "1.0.0-rc.1/manifest.json"
  manifest.write_text(json.dumps(payload), encoding="utf-8")

  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.0.0-rc.1"
  )

  assert failures == [
      "1.0.0-rc.1: consumer project path must stay inside the "
      "snapshot directory"
  ]


def test_archive_fixture_cannot_escape_version_directory(tmp_path):
  header_path, payload = make_repo(tmp_path)
  snapshot_root = write_snapshot(tmp_path, payload)
  outside = tmp_path / "compatibility/outside.bin"
  outside.write_bytes(b"outside fixture")
  payload["archives"][0]["path"] = "../outside.bin"
  manifest = snapshot_root / "1.0.0-rc.1/manifest.json"
  manifest.write_text(json.dumps(payload), encoding="utf-8")

  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.0.0-rc.1"
  )

  assert failures == [
      "1.0.0-rc.1: archive path must stay inside the snapshot directory"
  ]


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
]]
# add_test(NAME consumer COMMAND consumer)
# add_test(NAME archive_consumer COMMAND archive_consumer TESS_SNAPSHOT_DIR)
""",
      encoding="utf-8",
  )

  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.0.0-rc.1"
  )

  assert failures == [
      "1.0.0-rc.1: consumer project must discover tess CONFIG and link "
      "tess::tess, build both recorded consumers, and test them",
  ]
