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


def test_existing_callable_cannot_gain_base_overloads_through_using(tmp_path):
  header_path, payload = make_repo(tmp_path)
  header = tmp_path / "include/tess/tess.h"
  text = header.read_text(encoding="utf-8").replace(
      "struct StableSymbol {",
      "struct StableBase {\n  void set_limit(double value);\n};\n"
      "struct StableSymbol : StableBase {",
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
          "struct StableSymbol : StableBase {",
          "struct StableSymbol : StableBase {\n  using StableBase::set_limit;",
      ),
      encoding="utf-8",
  )

  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.1.1"
  )

  assert any(
      "overload added to existing callable" in failure
      and "set_limit" in failure
      for failure in failures
  ), failures


def test_existing_operator_cannot_gain_base_overloads_through_using(tmp_path):
  header_path, payload = make_repo(tmp_path)
  header = tmp_path / "include/tess/tess.h"
  text = header.read_text(encoding="utf-8").replace(
      "struct StableSymbol {",
      "struct StableBase {\n  int operator+(double value);\n};\n"
      "struct StableSymbol : StableBase {\n  int operator+(int value);",
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
          "struct StableSymbol : StableBase {",
          "struct StableSymbol : StableBase {\n"
          "  using StableBase::operator+;",
      ),
      encoding="utf-8",
  )

  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.1.1"
  )

  assert any(
      "overload added to existing callable" in failure
      and "operator +" in failure
      for failure in failures
  ), failures


def test_imported_operator_identities_match_callable_identities():
  declarations = {
      "member tess::S:using B :: operator +": "tess::S::operator+",
      "member tess::S:using B :: operator <": "tess::S::operator<",
      "member tess::S:using B :: operator ( )": "tess::S::operator()",
      "member tess::S:using B :: operator [ ]": "tess::S::operator[]",
      "member tess::S:using B :: operator bool": "tess::S::operatorbool",
  }

  for declaration, identity in declarations.items():
    assert snapshots._using_callable_identity(declaration) == identity

  direct = "member tess::S:bool operator < ( int value ) const"
  imported = "member tess::S:using B :: operator <"
  assert snapshots._callable_identity(direct) == "tess::S::operator<"
  assert snapshots._using_callable_identity(imported) == (
      snapshots._callable_identity(direct)
  )


def test_less_than_operator_cannot_gain_direct_or_imported_overload(tmp_path):
  additions = (
      "  bool operator<(double value) const;\n",
      "  using StableBase::operator<;\n",
  )
  for index, addition in enumerate(additions):
    repo = tmp_path / f"variant-{index}"
    header_path, payload = make_repo(repo)
    header = repo / "include/tess/tess.h"
    text = header.read_text(encoding="utf-8").replace(
        "struct StableSymbol {",
        "struct StableBase {\n"
        "  bool operator<(double value) const;\n"
        "};\n"
        "struct StableSymbol : StableBase {\n"
        "  bool operator<(int value) const;",
    )
    header.write_text(text, encoding="utf-8")
    payload["public_symbols"] = sorted(
        snapshots.current_symbols(
            repo,
            payload["headers"]["stable"]
            + payload["headers"]["optional-stable"],
        )
    )
    payload["api_contract"] = snapshots.current_api_contract(
        repo,
        payload["headers"]["stable"]
        + payload["headers"]["optional-stable"],
    )
    snapshot_root = write_snapshot(repo, payload)
    header.write_text(
        text.replace(
            "struct StableSymbol : StableBase {\n",
            "struct StableSymbol : StableBase {\n" + addition,
        ),
        encoding="utf-8",
    )

    failures = snapshots.check_snapshots(
        repo, snapshot_root, header_path, "1.1.1"
    )

    assert any(
        "overload added to existing callable" in failure
        and "operator <" in failure
        for failure in failures
    ), failures


def test_compound_operator_cannot_gain_imported_overload(tmp_path):
  operators = ("==", "<=", ">=", "<=>", "+=")
  for index, operator in enumerate(operators):
    repo = tmp_path / f"variant-{index}"
    header_path, payload = make_repo(repo)
    header = repo / "include/tess/tess.h"
    text = header.read_text(encoding="utf-8").replace(
        "struct StableSymbol {",
        "struct StableBase {\n"
        f"  bool operator{operator}(double value) const;\n"
        "};\n"
        "struct StableSymbol : StableBase {\n"
        f"  bool operator{operator}(int value) const;",
    )
    header.write_text(text, encoding="utf-8")
    payload["public_symbols"] = sorted(
        snapshots.current_symbols(
            repo,
            payload["headers"]["stable"]
            + payload["headers"]["optional-stable"],
        )
    )
    payload["api_contract"] = snapshots.current_api_contract(
        repo,
        payload["headers"]["stable"]
        + payload["headers"]["optional-stable"],
    )
    snapshot_root = write_snapshot(repo, payload)
    header.write_text(
        text.replace(
            "struct StableSymbol : StableBase {\n",
            "struct StableSymbol : StableBase {\n"
            f"  using StableBase::operator{operator};\n",
        ),
        encoding="utf-8",
    )

    failures = snapshots.check_snapshots(
        repo, snapshot_root, header_path, "1.1.1"
    )

    assert any(
        "overload added to existing callable" in failure
        and "operator" in failure
        for failure in failures
    ), (operator, failures)


def test_inherited_constructor_matches_derived_constructor_identity(tmp_path):
  header_path, payload = make_repo(tmp_path)
  header = tmp_path / "include/tess/tess.h"
  text = header.read_text(encoding="utf-8").replace(
      "struct StableOptions {",
      "struct StableBase { StableBase(double value); };\n"
      "struct StableOptions : StableBase {\n"
      "  StableOptions(int value);",
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
          "struct StableOptions : StableBase {\n  using StableBase::StableBase;",
      ),
      encoding="utf-8",
  )

  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.1.1"
  )

  assert any(
      "overload added to existing callable" in failure
      and "StableBase :: StableBase" in failure
      for failure in failures
  ), failures


def test_dependent_inherited_constructor_matches_derived_identity(tmp_path):
  header_path, payload = make_repo(tmp_path)
  header = tmp_path / "include/tess/tess.h"
  text = header.read_text(encoding="utf-8").replace(
      "struct StableOptions {",
      "template < class T > struct StableBase { StableBase(double value); };\n"
      "template < class T > struct StableOptions : StableBase<T> {\n"
      "  StableOptions(int value);",
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
      "overload added to existing callable" in failure
      and "StableBase < T > :: StableBase" in failure
      for failure in failures
  ), failures


def test_nested_dependent_inherited_constructor_uses_derived_identity(tmp_path):
  header_path, payload = make_repo(tmp_path)
  header = tmp_path / "include/tess/tess.h"
  text = header.read_text(encoding="utf-8").replace(
      "struct StableOptions {",
      "template < class T > struct StableOuter {\n"
      "  struct Base { Base(double value); };\n"
      "};\n"
      "template < class T > struct StableOptions\n"
      "  : StableOuter<T>::Base {\n"
      "  StableOptions(int value);",
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
          "  StableOptions(int value);",
          "  using StableOuter<T>::Base::Base;\n"
          "  StableOptions(int value);",
      ),
      encoding="utf-8",
  )

  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.1.1"
  )

  assert any(
      "overload added to existing callable" in failure
      and "Base :: Base" in failure
      for failure in failures
  ), failures


def test_relational_dependent_inherited_constructor_uses_derived_identity(
    tmp_path,
):
  header_path, payload = make_repo(tmp_path)
  header = tmp_path / "include/tess/tess.h"
  text = header.read_text(encoding="utf-8").replace(
      "struct StableOptions {",
      "template < bool Enabled > struct StableBase {\n"
      "  StableBase(double value);\n"
      "};\n"
      "struct StableOptions : StableBase<(1 > 0)> {\n"
      "  StableOptions(int value);",
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
          "  StableOptions(int value);",
          "  using StableBase<(1 > 0)>::StableBase;\n"
          "  StableOptions(int value);",
      ),
      encoding="utf-8",
  )

  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.1.1"
  )

  assert any(
      "overload added to existing callable" in failure
      and "StableBase" in failure
      for failure in failures
  ), failures


def test_bare_relational_dependent_inherited_constructor_uses_derived_identity(
    tmp_path,
):
  header_path, payload = make_repo(tmp_path)
  header = tmp_path / "include/tess/tess.h"
  text = header.read_text(encoding="utf-8").replace(
      "struct StableOptions {",
      "struct StableBound { static constexpr int value = 0; };\n"
      "template < bool Enabled > struct StableBase {\n"
      "  StableBase(double value);\n"
      "};\n"
      "struct StableOptions\n"
      "  : StableBase<StableBound::value < 1> {\n"
      "  StableOptions(int value);",
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
          "  StableOptions(int value);",
          "  using StableBase<StableBound::value < 1>::StableBase;\n"
          "  StableOptions(int value);",
      ),
      encoding="utf-8",
  )

  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.1.1"
  )

  assert any(
      "overload added to existing callable" in failure
      and "StableBase" in failure
      for failure in failures
  ), failures


def test_qualified_relational_inherited_constructor_uses_derived_identity(
    tmp_path,
):
  header_path, payload = make_repo(tmp_path)
  header = tmp_path / "include/tess/tess.h"
  text = header.read_text(encoding="utf-8").replace(
      "struct StableOptions {",
      "struct StableBound { static constexpr int value = 0; };\n"
      "namespace qualified {\n"
      "template < bool Enabled > struct StableBase {\n"
      "  StableBase(double value);\n"
      "};\n"
      "}\n"
      "struct StableOptions\n"
      "  : qualified::StableBase<StableBound::value < 1> {\n"
      "  StableOptions(int value);",
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
          "  StableOptions(int value);",
          "  using qualified::StableBase<StableBound::value < 1>"
          "::StableBase;\n"
          "  StableOptions(int value);",
      ),
      encoding="utf-8",
  )

  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.1.1"
  )

  assert any(
      "overload added to existing callable" in failure
      and "StableBase" in failure
      for failure in failures
  ), failures


def test_namespace_name_does_not_fake_inherited_constructor(tmp_path):
  header_path, payload = make_repo(tmp_path)
  header = tmp_path / "include/tess/tess.h"
  text = header.read_text(encoding="utf-8").replace(
      "struct StableOptions {",
      "namespace evaluate {\n"
      "template < bool Enabled > struct StableBase {\n"
      "  void evaluate(double value);\n"
      "};\n"
      "}\n"
      "struct StableOptions : evaluate::StableBase<true> {\n"
      "  void evaluate(int value);\n"
      " private:\n"
      "  int hidden = 0;\n"
      " public:",
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
          " public:\n  int max_steps",
          " public:\n  using evaluate::StableBase<true>::evaluate;\n"
          "  int max_steps",
      ),
      encoding="utf-8",
  )

  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.1.1"
  )

  assert any(
      "overload added to existing callable" in failure
      and "evaluate" in failure
      for failure in failures
  ), failures


def test_template_argument_name_does_not_fake_inherited_constructor():
  declarations = (
      "using Base<::Argument>::Argument;",
      "using Base<Other::Argument<int>>::Argument;",
  )
  for declaration in declarations:
    contract = extract_api_contract(
        f"struct Stable {{ {declaration} int value; }};"
    )

    assert "aggregate Stable" in contract


def test_relational_template_expressions_preserve_callable_identity(tmp_path):
  declarations = (
      "template < int N = ( 1 < 2 ) > void evaluate ( int value )",
      "template < int N > enable_if_t < ( N < 3 ) , bool > "
      "evaluate ( int value )",
  )
  for index, declaration in enumerate(declarations):
    repo = tmp_path / f"variant-{index}"
    header_path, payload = make_repo(repo)
    header = repo / "include/tess/tess.h"
    text = header.read_text(encoding="utf-8").replace(
        "struct StableOptions {",
        f"struct StableOptions {{\n  {declaration};",
    )
    header.write_text(text, encoding="utf-8")
    payload["api_contract"] = snapshots.current_api_contract(
        repo,
        payload["headers"]["stable"]
        + payload["headers"]["optional-stable"],
    )
    snapshot_root = write_snapshot(repo, payload)
    header.write_text(
        text.replace(
            f"  {declaration};",
            f"  {declaration};\n"
            "  template < int N > void evaluate ( double value );",
        ),
        encoding="utf-8",
    )

    failures = snapshots.check_snapshots(
        repo, snapshot_root, header_path, "1.1.1"
    )

    assert any(
        "overload added to existing callable" in failure
        and "evaluate" in failure
        for failure in failures
  ), failures


def test_unparenthesized_relational_template_expressions_keep_identity(
    tmp_path,
):
  declarations = (
      "template < int N = 1 < 2 > void evaluate ( int value )",
      "template < int N > Result < N < 3 > evaluate ( int value )",
  )
  for index, declaration in enumerate(declarations):
    repo = tmp_path / f"variant-{index}"
    header_path, payload = make_repo(repo)
    header = repo / "include/tess/tess.h"
    text = header.read_text(encoding="utf-8").replace(
        "struct StableOptions {",
        f"struct StableOptions {{\n  {declaration};",
    )
    header.write_text(text, encoding="utf-8")
    payload["api_contract"] = snapshots.current_api_contract(
        repo,
        payload["headers"]["stable"]
        + payload["headers"]["optional-stable"],
    )
    snapshot_root = write_snapshot(repo, payload)
    header.write_text(
        text.replace(
            f"  {declaration};",
            f"  {declaration};\n  void evaluate(double value);",
        ),
        encoding="utf-8",
    )

    failures = snapshots.check_snapshots(
        repo, snapshot_root, header_path, "1.1.1"
    )

    assert any(
        "overload added to existing callable" in failure
        and "evaluate" in failure
        for failure in failures
    ), failures


def test_relational_template_fallback_ignores_nested_default_call(tmp_path):
  header_path, payload = make_repo(tmp_path)
  header = tmp_path / "include/tess/tess.h"
  declaration = (
      "template < int N = 1 < 2 > void evaluate "
      "( int value = helper ( ) )"
  )
  text = header.read_text(encoding="utf-8").replace(
      "struct StableOptions {",
      f"struct StableOptions {{\n  {declaration};",
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
          f"  {declaration};",
          f"  {declaration};\n  void evaluate(double value);",
      ),
      encoding="utf-8",
  )

  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.1.1"
  )

  assert any(
      "overload added to existing callable" in failure
      and "evaluate" in failure
      for failure in failures
  ), failures


def test_relational_template_with_requires_keeps_callable_identity(tmp_path):
  declarations = (
      "template < int N = 1 < 2 > void evaluate ( int value ) "
      "requires ( N < 3 )",
      "template < int N = 1 < 2 > auto evaluate ( int value ) "
      "noexcept ( N < 3 ) -> bool",
      "template < int N = 1 < 2 > void evaluate ( int value ) "
      "requires StableConcept < N >",
  )
  for index, declaration in enumerate(declarations):
    repo = tmp_path / f"variant-{index}"
    header_path, payload = make_repo(repo)
    header = repo / "include/tess/tess.h"
    text = header.read_text(encoding="utf-8").replace(
        "struct StableOptions {",
        f"struct StableOptions {{\n  {declaration};",
    )
    header.write_text(text, encoding="utf-8")
    payload["api_contract"] = snapshots.current_api_contract(
        repo,
        payload["headers"]["stable"]
        + payload["headers"]["optional-stable"],
    )
    snapshot_root = write_snapshot(repo, payload)
    header.write_text(
        text.replace(
            f"  {declaration};",
            f"  {declaration};\n  void evaluate(double value);",
        ),
        encoding="utf-8",
    )

    failures = snapshots.check_snapshots(
        repo, snapshot_root, header_path, "1.1.1"
    )

    assert any(
        "overload added to existing callable" in failure
        and "evaluate" in failure
        for failure in failures
    ), failures


def test_trailing_template_calls_do_not_steal_callable_identity(tmp_path):
  declarations = (
      "template < int N = 1 < 2 > auto evaluate ( int value ) "
      "-> Tag < helper ( ) >",
      "template < int N = 1 < 2 > void evaluate ( int value ) "
      "requires Concept < helper ( ) >",
  )
  for index, declaration in enumerate(declarations):
    repo = tmp_path / f"variant-{index}"
    header_path, payload = make_repo(repo)
    header = repo / "include/tess/tess.h"
    text = header.read_text(encoding="utf-8").replace(
        "struct StableOptions {",
        f"struct StableOptions {{\n  {declaration};",
    )
    header.write_text(text, encoding="utf-8")
    payload["api_contract"] = snapshots.current_api_contract(
        repo,
        payload["headers"]["stable"]
        + payload["headers"]["optional-stable"],
    )
    snapshot_root = write_snapshot(repo, payload)
    header.write_text(
        text.replace(
            f"  {declaration};",
            f"  {declaration};\n  void evaluate(double value);",
        ),
        encoding="utf-8",
    )

    failures = snapshots.check_snapshots(
        repo, snapshot_root, header_path, "1.1.1"
    )

    assert any(
        "overload added to existing callable" in failure
        and "evaluate" in failure
        for failure in failures
    ), failures


def test_callable_template_data_member_is_not_mistaken_for_function():
  contract = extract_api_contract(
      "struct Stable { Callable<void(int)> callback; };"
  )

  assert any(
      item.startswith("data-member Stable:") and "callback" in item
      for item in contract
  )
  assert not any(item.startswith("member Stable:") for item in contract)


def test_named_functions_are_not_mistaken_for_parenthesized_objects():
  contract = extract_api_contract(
      "struct Stable { Widget evaluate(int); std::size_t route(int); };"
  )

  assert any(
      item.startswith("member Stable:") and "evaluate" in item
      for item in contract
  )
  assert any(
      item.startswith("member Stable:") and "route" in item
      for item in contract
  )


def test_grouped_function_declarator_retains_callable_identity(tmp_path):
  header_path, payload = make_repo(tmp_path)
  header = tmp_path / "include/tess/tess.h"
  text = header.read_text(encoding="utf-8").replace(
      "struct StableOptions {",
      "struct StableOptions {\n  int (evaluate)(int value);",
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
          "  int (evaluate)(int value);",
          "  int (evaluate)(int value);\n"
          "  int evaluate(double value);",
      ),
      encoding="utf-8",
  )

  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.1.1"
  )

  assert any(
      "overload added to existing callable" in failure
      and "evaluate" in failure
      for failure in failures
  ), failures

  header.write_text(
      text.replace(
          "  int (evaluate)(int value);",
          "  int (evaluate)(int value);\n  int distinct(double value);",
      ),
      encoding="utf-8",
  )

  assert snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.1.1"
  ) == []


def test_requires_expression_body_is_part_of_callable_contract(tmp_path):
  header_path, payload = make_repo(tmp_path)
  header = tmp_path / "include/tess/tess.h"
  text = header.read_text(encoding="utf-8").replace(
      "struct StableOptions {",
      "struct StableOptions {\n"
      "  template < class T > void evaluate(T value)\n"
      "    requires requires(T item) { item.foo(); } {}",
  )
  header.write_text(text, encoding="utf-8")
  payload["api_contract"] = snapshots.current_api_contract(
      tmp_path,
      payload["headers"]["stable"]
      + payload["headers"]["optional-stable"],
  )
  snapshot_root = write_snapshot(tmp_path, payload)
  header.write_text(text.replace("item.foo()", "item.bar()"), encoding="utf-8")

  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.1.1"
  )

  assert any(
      "API declaration changed or removed" in failure
      and "evaluate" in failure
      for failure in failures
  ), failures


def test_parameterless_requires_expression_body_is_part_of_contract(tmp_path):
  header_path, payload = make_repo(tmp_path)
  header = tmp_path / "include/tess/tess.h"
  text = header.read_text(encoding="utf-8").replace(
      "struct StableOptions {",
      "struct StableOptions {\n"
      "  template < class T > void evaluate(T value)\n"
      "    requires requires { value.foo(); } {}",
  )
  header.write_text(text, encoding="utf-8")
  payload["api_contract"] = snapshots.current_api_contract(
      tmp_path,
      payload["headers"]["stable"]
      + payload["headers"]["optional-stable"],
  )
  snapshot_root = write_snapshot(tmp_path, payload)
  header.write_text(
      text.replace("value.foo()", "value.bar()"), encoding="utf-8"
  )

  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.1.1"
  )

  assert any(
      "API declaration changed or removed" in failure
      and "evaluate" in failure
      for failure in failures
  ), failures


def test_elaborated_types_do_not_hide_inline_overloads(tmp_path):
  declarations = (
      "void evaluate(struct StableArgument* value) {}",
      "struct StableArgument* evaluate(int value) { return {}; }",
  )
  for index, declaration in enumerate(declarations):
    repo = tmp_path / f"variant-{index}"
    header_path, payload = make_repo(repo)
    header = repo / "include/tess/tess.h"
    text = header.read_text(encoding="utf-8").replace(
        "struct StableOptions {",
        f"struct StableOptions {{\n  {declaration}",
    )
    header.write_text(text, encoding="utf-8")
    payload["api_contract"] = snapshots.current_api_contract(
        repo,
        payload["headers"]["stable"]
        + payload["headers"]["optional-stable"],
    )
    snapshot_root = write_snapshot(repo, payload)
    header.write_text(
        text.replace(
            f"  {declaration}",
            f"  {declaration}\n  void evaluate(double value) {{}}",
        ),
        encoding="utf-8",
    )

    failures = snapshots.check_snapshots(
        repo, snapshot_root, header_path, "1.1.1"
    )

    assert any(
        "overload added to existing callable" in failure
        and "evaluate" in failure
        for failure in failures
    ), failures


def test_function_like_annotation_before_type_preserves_body_contract(
    tmp_path,
):
  header_path, payload = make_repo(tmp_path)
  header = tmp_path / "include/tess/tess.h"
  text = header.read_text(encoding="utf-8").replace(
      "struct StableOptions {",
      "TESS_EXPORT(api) struct StableOptions {",
  )
  header.write_text(text, encoding="utf-8")
  payload["api_contract"] = snapshots.current_api_contract(
      tmp_path,
      payload["headers"]["stable"]
      + payload["headers"]["optional-stable"],
  )
  snapshot_root = write_snapshot(tmp_path, payload)
  header.write_text(
      text.replace("int max_steps = 1", "double max_steps = 1"),
      encoding="utf-8",
  )

  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.1.1"
  )

  assert any(
      "API declaration changed or removed" in failure
      and "StableOptions" in failure
      for failure in failures
  ), failures


def test_function_like_annotation_before_unscoped_enum_preserves_members(
    tmp_path,
):
  header_path, payload = make_repo(tmp_path)
  header = tmp_path / "include/tess/tess.h"
  text = header.read_text(encoding="utf-8").replace(
      "enum class StableMode { First = 1, Second = 2 };",
      "TESS_EXPORT(api) enum StableMode { First, Second };",
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
      text.replace("{ First, Second }", "{ Inserted, First, Second }"),
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


def test_parenthesized_data_declarators_are_public_data_members(tmp_path):
  additions = (
      "  int (value);\n",
      "  const int (constant);\n",
      "  std::size_t (other);\n",
      "  Callable<void(int)> (*callback);\n",
      "  int (*callback);\n",
      "  int (&reference);\n",
      "  int ((nested));\n",
      "  int (*values)[4];\n",
      "  int (StableSymbol::*member);\n",
      "  StableMode const (mode);\n",
      "  StableMode volatile (*mode_ptr);\n",
      "  StableMode mutable (mutable_mode);\n",
      "  StableMode * const (const_ptr);\n",
  )
  for index, addition in enumerate(additions):
    repo = tmp_path / f"variant-{index}"
    header_path, payload = make_repo(repo)
    snapshot_root = write_snapshot(repo, payload)
    header = repo / "include/tess/tess.h"
    text = header.read_text(encoding="utf-8").replace(
        "struct StableOptions {\n",
        "struct StableOptions {\n" + addition,
    )
    header.write_text(text, encoding="utf-8")

    failures = snapshots.check_snapshots(
        repo, snapshot_root, header_path, "1.1.1"
    )

    assert any(
        "public data member added to existing type" in failure
        for failure in failures
    ), (addition, failures)


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


def test_snapshotted_macro_cannot_gain_a_conditional_redefinition(tmp_path):
  header_path, payload = make_repo(tmp_path)
  snapshot_root = write_snapshot(tmp_path, payload)
  header = tmp_path / "include/tess/tess.h"
  original = header.read_text(encoding="utf-8")
  header.write_text(
      original.replace(
          "#define TESS_COMPAT_LIMIT 8\n",
          "#define TESS_COMPAT_LIMIT 8\n"
          "#if TESS_ALTERNATE_LIMIT\n"
          "#define TESS_COMPAT_LIMIT 16\n"
          "#endif\n",
      ),
      encoding="utf-8",
  )

  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.1.1"
  )

  assert any(
      "stable macro redefined" in failure
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
      '  TESS_DEPRECATED("old") StableOptions() = default;\n',
      '  StableOptions [[deprecated("old")]] () = default;\n',
      '  StableOptions TESS_DEPRECATED("old") () = default;\n',
      "  StableOptions TESS_DEPRECATED () = default;\n",
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


def test_parameter_type_spelling_does_not_hide_aggregate_compatibility(
    tmp_path,
):
  header_path, payload = make_repo(tmp_path)
  header = tmp_path / "include/tess/tess.h"
  text = header.read_text(encoding="utf-8").replace(
      "struct StableOptions {\n",
      "struct StableOptions {\n"
      "  void consume(StableOptions());\n",
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
          "struct StableOptions {\n",
          "struct StableOptions {\n  StableOptions() = default;\n",
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


def test_macro_annotated_callable_cannot_gain_an_overload(tmp_path):
  header_path, payload = make_repo(tmp_path)
  header = tmp_path / "include/tess/tess.h"
  text = header.read_text(encoding="utf-8").replace(
      "  void set_limit(int value = 8);",
      '  TESS_DEPRECATED("old") void set_limit(int value = 8);',
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
          '  TESS_DEPRECATED("old") void set_limit(int value = 8);',
          '  TESS_DEPRECATED("old") void set_limit(int value = 8);\n'
          "  void set_limit(double value);",
      ),
      encoding="utf-8",
  )

  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.1.1"
  )

  assert any(
      "overload added to existing callable" in failure
      and "set_limit" in failure
      for failure in failures
  ), failures


def test_destructor_keeps_aggregate_and_has_distinct_callable_identity(
    tmp_path,
):
  header_path, payload = make_repo(tmp_path)
  header = tmp_path / "include/tess/tess.h"
  text = header.read_text(encoding="utf-8").replace(
      "struct StableOptions {\n",
      "struct StableOptions {\n  ~StableOptions() = default;\n",
  )
  header.write_text(text, encoding="utf-8")
  contract = snapshots.current_api_contract(
      tmp_path,
      payload["headers"]["stable"]
      + payload["headers"]["optional-stable"],
  )["include/tess/tess.h"]

  assert "aggregate tess::StableOptions" in contract
  destructor = next(
      item for item in contract if "~ StableOptions ( )" in item
  )
  assert snapshots._callable_identity(destructor) == (
      "tess::StableOptions::~StableOptions"
  )


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


def test_conditionally_aggregate_type_cannot_lose_remaining_configuration(
    tmp_path,
):
  variants = (
      (
          "#if TESS_NONAGGREGATE_OPTIONS\n"
          " private:\n"
          "  int conditional_state = 0;\n"
          "#endif\n",
          " private:\n  int conditional_state = 0;\n",
      ),
      (
          "#if TESS_NONAGGREGATE_OPTIONS\n"
          "  StableOptions();\n"
          "#endif\n",
          "  StableOptions();\n",
      ),
      (
          "#if TESS_NONAGGREGATE_OPTIONS\n"
          "  virtual void conditional_method();\n"
          "#endif\n",
          "  virtual void conditional_method();\n",
      ),
  )
  for index, (conditional, unconditional) in enumerate(variants):
    repo = tmp_path / f"variant-{index}"
    header_path, payload = make_repo(repo)
    header = repo / "include/tess/tess.h"
    original = header.read_text(encoding="utf-8")
    text = original.replace(
        "struct StableOptions {\n",
        "struct StableOptions {\n" + conditional,
    )
    header.write_text(text, encoding="utf-8")
    payload["api_contract"] = snapshots.current_api_contract(
        repo,
        payload["headers"]["stable"]
        + payload["headers"]["optional-stable"],
    )
    snapshot_root = write_snapshot(repo, payload)
    header.write_text(
        text.replace(conditional, unconditional), encoding="utf-8"
    )

    failures = snapshots.check_snapshots(
        repo, snapshot_root, header_path, "1.1.1"
    )

    assert any(
        "aggregate compatibility changed" in failure
        and "StableOptions" in failure
        for failure in failures
  ), failures


def test_conditionally_nonaggregate_base_cannot_hide_constructor_addition(
    tmp_path,
):
  header_path, payload = make_repo(tmp_path)
  header = tmp_path / "include/tess/tess.h"
  text = header.read_text(encoding="utf-8").replace(
      "struct StableOptions {",
      "struct StableBase {};\nstruct StableOptions\n"
      "#if TESS_NONAGGREGATE_BASE\n"
      ": private StableBase\n"
      "#endif\n{",
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
          "{\n  int max_steps", "{\n  StableOptions();\n  int max_steps"
      ),
      encoding="utf-8",
  )

  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.1.1"
  )

  assert any(
      (
          "API declaration changed or removed" in failure
          or "aggregate compatibility changed" in failure
      )
      and "aggregate tess::StableOptions" in failure
      for failure in failures
  ), failures


def test_conditional_base_aggregate_availability_tracks_all_branches():
  partly_aggregate = extract_api_contract(
      """struct Base {};
struct Sometimes
#if TESS_PRIVATE_BASE
  : private Base
#else
  : public Base
#endif
{ int value; };
"""
  )
  never_aggregate = extract_api_contract(
      """struct Base {};
struct Never
#if TESS_PRIVATE_BASE
  : private Base
#else
  : virtual Base
#endif
{ int value; };
"""
  )

  assert "aggregate Sometimes" in partly_aggregate
  assert any(
      item.startswith("aggregate-break Sometimes:")
      for item in partly_aggregate
  )
  assert "aggregate Never" not in never_aggregate


def test_constructor_in_already_nonaggregate_base_branch_is_compatible(
    tmp_path,
):
  header_path, payload = make_repo(tmp_path)
  header = tmp_path / "include/tess/tess.h"
  text = header.read_text(encoding="utf-8").replace(
      "struct StableOptions {",
      "struct StableBase {};\nstruct StableOptions\n"
      "#if TESS_NONAGGREGATE_BASE\n"
      ": private StableBase\n"
      "#endif\n{",
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
          "{\n  int max_steps",
          "{\n#if TESS_NONAGGREGATE_BASE\n"
          "  StableOptions();\n#endif\n  int max_steps",
      ),
      encoding="utf-8",
  )

  assert snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.1.1"
  ) == []


def test_private_anonymous_type_objects_break_aggregate_compatibility(
    tmp_path,
):
  additions = (
      " private:\n  union { int integer; double real; };\n public:\n",
      " private:\n  struct { int value; } state;\n public:\n",
  )
  for index, addition in enumerate(additions):
    repo = tmp_path / f"variant-{index}"
    header_path, payload = make_repo(repo)
    snapshot_root = write_snapshot(repo, payload)
    header = repo / "include/tess/tess.h"
    text = header.read_text(encoding="utf-8").replace(
        "struct StableOptions {\n",
        "struct StableOptions {\n" + addition,
    )
    header.write_text(text, encoding="utf-8")

    failures = snapshots.check_snapshots(
        repo, snapshot_root, header_path, "1.1.1"
    )

    assert any(
        (
            "API declaration changed or removed" in failure
            or "aggregate compatibility changed" in failure
        )
        and "aggregate tess::StableOptions" in failure
        for failure in failures
    ), failures


def test_private_nested_type_without_an_object_preserves_aggregate():
  contract = extract_api_contract(
      "struct Stable { private: struct Nested { int value; }; "
      "public: int value; };"
  )

  assert "aggregate Stable" in contract


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
