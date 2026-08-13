"""Regression tests for conservatively parsed C++ compatibility evidence."""

from test_compatibility_snapshots import (
    assert_aggregate,
    assert_aggregate_break,
    assert_overload,
    extract_api_contract,
    make_repo,
    refresh_payload,
    snapshots,
    write_snapshot,
)


def test_ambiguous_using_import_checks_ordinary_overload_identity(tmp_path):
  variants = (
      "template<class> struct Holder { static void Base(double); };\n"
      "namespace other { template<class> struct Base {}; }\n"
      "struct StableOptions : Holder<other::Base<int>> {\n"
      "  void Base(int);\n private: int hidden = 0;\n"
      " public: using Holder<other::Base<int>>::Base;",
      "struct Source { static void Base(double); };\nSource make();\n"
      "struct StableOptions : Source {\n"
      "  void Base(int);\n private: int hidden = 0;\n"
      " public: using decltype(make())::Base;",
  )
  for index, replacement in enumerate(variants):
    repo = tmp_path / f"variant-{index}"
    header_path, payload = make_repo(repo)
    header = repo / "include/tess/tess.h"
    original = header.read_text(encoding="utf-8")
    baseline = replacement.rsplit("\n", 1)[0]
    header.write_text(
        original.replace("struct StableOptions {", baseline),
        encoding="utf-8",
    )
    refresh_payload(repo, payload)
    snapshot_root = write_snapshot(repo, payload)
    header.write_text(
        original.replace("struct StableOptions {", replacement),
        encoding="utf-8",
    )
    failures = snapshots.check_snapshots(
        repo, snapshot_root, header_path, "1.1.1"
    )
    assert_overload(failures, "StableOptions")


def test_snapshotted_using_import_checks_later_direct_overload(tmp_path):
  variants = (
      (
          "struct Base { void evaluate(double); };\n"
          "struct StableOptions : Base {\n  using Base::evaluate;",
          "  void evaluate(int);\n",
      ),
      (
          "struct Base { Base(double); };\n"
          "struct StableOptions : Base {\n  using Base::Base;",
          "  StableOptions(int);\n",
      ),
      (
          "template<class> struct Holder { static void Base(double); };\n"
          "namespace other { template<class> struct Base {}; }\n"
          "struct StableOptions : Holder<other::Base<int>> {\n"
          "  using Holder<other::Base<int>>::Base;",
          "  void Base(int);\n",
      ),
  )
  for index, (replacement, addition) in enumerate(variants):
    repo = tmp_path / f"variant-{index}"
    header_path, payload = make_repo(repo)
    header = repo / "include/tess/tess.h"
    text = header.read_text(encoding="utf-8").replace(
        "struct StableOptions {", replacement
    )
    header.write_text(text, encoding="utf-8")
    refresh_payload(repo, payload)
    snapshot_root = write_snapshot(repo, payload)
    header.write_text(
        text.replace("  int max_steps = 1;", addition + "  int max_steps = 1;"),
        encoding="utf-8",
    )
    failures = snapshots.check_snapshots(
        repo, snapshot_root, header_path, "1.1.1"
    )
    assert_overload(failures, "StableOptions")


def test_using_declarator_lists_check_every_callable(tmp_path):
  header_path, payload = make_repo(tmp_path)
  header = tmp_path / "include/tess/tess.h"
  text = header.read_text(encoding="utf-8").replace(
      "struct StableOptions {",
      "struct Base {\n"
      "  void evaluate(double);\n  void other(double);\n"
      "  bool operator<(int) const;\n  void operator,(int);\n};\n"
      "struct StableOptions : Base {\n"
      "  void evaluate(int);\n  bool operator<(double) const;",
  )
  header.write_text(text, encoding="utf-8")
  refresh_payload(tmp_path, payload)
  snapshot_root = write_snapshot(tmp_path, payload)
  header.write_text(
      text.replace(
          "  int max_steps = 1;",
          "  using Base::operator<, Base::evaluate, Base::other, "
          "Base::operator,;\n"
          "  int max_steps = 1;",
      ),
      encoding="utf-8",
  )
  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.1.1"
  )
  assert_overload(failures, "StableOptions")


def test_relational_using_list_checks_every_callable(tmp_path):
  header_path, payload = make_repo(tmp_path)
  header = tmp_path / "include/tess/tess.h"
  text = header.read_text(encoding="utf-8").replace(
      "struct StableOptions {",
      "template<bool> struct Holder {\n"
      "  void evaluate(double);\n  using value_type = int;\n};\n"
      "struct StableOptions : Holder<true> {\n  void evaluate(int);",
  )
  header.write_text(text, encoding="utf-8")
  refresh_payload(tmp_path, payload)
  snapshot_root = write_snapshot(tmp_path, payload)
  header.write_text(
      text.replace(
          "  int max_steps = 1;",
          "  using Holder<1 < 2>::evaluate, Holder<true>::value_type;\n"
          "  int max_steps = 1;",
      ),
      encoding="utf-8",
  )
  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.1.1"
  )
  assert_overload(failures, "StableOptions")


def test_namespace_using_participates_in_overload_checks(tmp_path):
  for index in range(2):
    repo = tmp_path / f"variant-{index}"
    header_path, payload = make_repo(repo)
    header = repo / "include/tess/tess.h"
    text = header.read_text(encoding="utf-8").replace(
        "namespace tess {",
        "namespace external { void evaluate(double); }\nnamespace tess {\n"
        + ("using external::evaluate;\n" if index else "void evaluate(int);\n"),
    )
    header.write_text(text, encoding="utf-8")
    refresh_payload(repo, payload)
    snapshot_root = write_snapshot(repo, payload)
    if index:
      current = text.replace(
          "[[nodiscard]] auto stable_route",
          "void evaluate(int);\n[[nodiscard]] auto stable_route",
      )
    else:
      current = text.replace(
          "namespace tess {", "namespace tess {\nusing external::evaluate;"
      )
    header.write_text(current, encoding="utf-8")
    failures = snapshots.check_snapshots(
        repo, snapshot_root, header_path, "1.1.1"
    )
    assert_overload(failures, "tess")

  reverse = tmp_path / "reverse"
  header_path, payload = make_repo(reverse)
  header = reverse / "include/tess/tess.h"
  text = header.read_text(encoding="utf-8").replace(
      "struct StableOptions {",
      "struct Base { void evaluate(double); void other(double); };\n"
      "struct StableOptions : Base {\n"
      "  using Base::evaluate, Base::other;",
  )
  header.write_text(text, encoding="utf-8")
  refresh_payload(reverse, payload)
  snapshot_root = write_snapshot(reverse, payload)
  header.write_text(
      text.replace(
          "  int max_steps = 1;",
          "  void other(int);\n  int max_steps = 1;",
      ),
      encoding="utf-8",
  )
  failures = snapshots.check_snapshots(
      reverse, snapshot_root, header_path, "1.1.1"
  )
  assert_overload(failures, "StableOptions")


def test_later_member_cannot_hide_inherited_callable(tmp_path):
  variants = (
      "struct Root { void evaluate(int); };\n"
      "template<class> struct Middle : Root {};\n"
      "struct StableOptions : Middle<int> {",
      "struct Root { void evaluate(int); };\nusing Alias = Root;\n"
      "struct StableOptions : Alias {",
      "struct Root { void evaluate(int); };\n"
      "template<class T> struct Middle : T {};\n"
      "struct StableOptions : Middle<Root> {",
  )
  for index, replacement in enumerate(variants):
    repo = tmp_path / f"variant-{index}"
    header_path, payload = make_repo(repo)
    header = repo / "include/tess/tess.h"
    text = header.read_text(encoding="utf-8").replace(
        "struct StableOptions {", replacement
    )
    header.write_text(text, encoding="utf-8")
    refresh_payload(repo, payload)
    snapshot_root = write_snapshot(repo, payload)
    header.write_text(
        text.replace(
            "  int max_steps = 1;",
            "  void evaluate(const char*);\n  int max_steps = 1;",
        ),
        encoding="utf-8",
    )
    failures = snapshots.check_snapshots(
        repo, snapshot_root, header_path, "1.1.1"
    )
    assert_overload(failures, "StableOptions")


def test_relational_base_does_not_absorb_later_public_base():
  contract = extract_api_contract(
      "template<bool> struct Base {}; struct Other {}; "
      "constexpr int N=0, M=1; "
      "class Stable : Base<N < M>, public Other { public: int x; };"
  )
  assert "aggregate Stable" not in contract

  for second in ("Other", "::Other"):
    contract = extract_api_contract(
        "template<bool> struct Base {}; struct Other {}; "
        "constexpr int N=0, M=1; "
        f"class Stable : public Base<N < M>, {second} "
        "{ public: int x; };"
    )
    assert "aggregate Stable" in contract
    assert any("@ambiguous-base-clause@" in item for item in contract)


def test_multi_argument_public_base_retains_aggregate_evidence(tmp_path):
  header_path, payload = make_repo(tmp_path)
  header = tmp_path / "include/tess/tess.h"
  text = header.read_text(encoding="utf-8").replace(
      "struct StableOptions {",
      "template<class, class> struct StableBase {};\n"
      "struct StableArg {};\n"
      "class StableOptions : public StableBase<int, StableArg> {\n public:",
  )
  header.write_text(text, encoding="utf-8")
  refresh_payload(tmp_path, payload)
  assert_aggregate(payload, "StableOptions")
  snapshot_root = write_snapshot(tmp_path, payload)
  header.write_text(
      text.replace(
          "  int max_steps = 1;",
          "  StableOptions() = default;\n  int max_steps = 1;",
      ),
      encoding="utf-8",
  )
  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.1.1"
  )
  assert_aggregate_break(failures, "StableOptions")
