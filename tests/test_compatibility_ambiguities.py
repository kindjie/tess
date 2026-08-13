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
from check_public_surface import extract_public_symbols


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


def test_namespace_using_directive_participates_in_lookup(tmp_path):
  header_path, payload = make_repo(tmp_path)
  header = tmp_path / "include/tess/tess.h"
  text = header.read_text(encoding="utf-8").replace(
      "namespace tess {",
      "namespace external { void evaluate(int); }\n"
      "namespace tess {\nusing namespace external;",
  )
  header.write_text(text, encoding="utf-8")
  refresh_payload(tmp_path, payload)
  snapshot_root = write_snapshot(tmp_path, payload)
  header.write_text(
      text.replace(
          "[[nodiscard]] auto stable_route",
          "void evaluate(double);\n[[nodiscard]] auto stable_route",
      ),
      encoding="utf-8",
  )
  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.1.1"
  )
  assert_overload(failures, "tess")


def test_namespace_alias_using_directive_participates_in_lookup(tmp_path):
  header_path, payload = make_repo(tmp_path)
  header = tmp_path / "include/tess/tess.h"
  text = header.read_text(encoding="utf-8").replace(
      "namespace tess {",
      "namespace a::external { void evaluate(int); }\n"
      "namespace tess {\nnamespace ext = a::external;\n"
      "using namespace ext;",
  )
  header.write_text(text, encoding="utf-8")
  refresh_payload(tmp_path, payload)
  snapshot_root = write_snapshot(tmp_path, payload)
  header.write_text(
      text.replace(
          "[[nodiscard]] auto stable_route",
          "void evaluate(double);\n[[nodiscard]] auto stable_route",
      ),
      encoding="utf-8",
  )
  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.1.1"
  )
  assert_overload(failures, "tess")


def test_namespace_alias_chain_participates_in_lookup(tmp_path):
  header_path, payload = make_repo(tmp_path)
  header = tmp_path / "include/tess/tess.h"
  text = header.read_text(encoding="utf-8").replace(
      "namespace tess {",
      "namespace a { void evaluate(int); }\nnamespace tess {\n"
      "namespace x = a; namespace y = x; using namespace y;",
  )
  header.write_text(text, encoding="utf-8")
  refresh_payload(tmp_path, payload)
  snapshot_root = write_snapshot(tmp_path, payload)
  header.write_text(
      text.replace(
          "[[nodiscard]] auto stable_route",
          "void evaluate(double);\n[[nodiscard]] auto stable_route",
      ),
      encoding="utf-8",
  )
  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.1.1"
  )
  assert_overload(failures, "tess")


def test_global_namespace_import_and_base_preserve_qualification(tmp_path):
  variants = (
      "namespace a { void evaluate(int); }\n"
      "namespace tess { namespace a { void other(int); } "
      "using namespace ::a;",
      "struct Root { void evaluate(int); };\n"
      "namespace tess { struct Root { void other(int); }; "
      "struct StableOptions : ::Root {",
  )
  for index, replacement in enumerate(variants):
    repo = tmp_path / f"variant-{index}"
    header_path, payload = make_repo(repo)
    header = repo / "include/tess/tess.h"
    marker = "namespace tess {" if index == 0 else "struct StableOptions {"
    text = header.read_text(encoding="utf-8").replace(marker, replacement)
    header.write_text(text, encoding="utf-8")
    refresh_payload(repo, payload)
    snapshot_root = write_snapshot(repo, payload)
    insertion = (
        "void evaluate(double);\n[[nodiscard]] auto stable_route"
        if index == 0
        else "  void evaluate(double);\n  int max_steps = 1;"
    )
    current = text.replace(
        "[[nodiscard]] auto stable_route" if index == 0 else "  int max_steps = 1;",
        insertion,
    )
    header.write_text(current, encoding="utf-8")
    failures = snapshots.check_snapshots(
        repo, snapshot_root, header_path, "1.1.1"
    )
    assert_overload(failures, "tess" if index == 0 else "StableOptions")


def test_namespace_overloads_are_checked_across_headers(tmp_path):
  header_path, payload = make_repo(tmp_path)
  first = tmp_path / "include/tess/pathfinding.h"
  second = tmp_path / "include/tess/optional.h"
  first.write_text(
      first.read_text(encoding="utf-8")
      + "\nnamespace tess { void evaluate(int); }\n",
      encoding="utf-8",
  )
  refresh_payload(tmp_path, payload)
  snapshot_root = write_snapshot(tmp_path, payload)
  second.write_text(
      second.read_text(encoding="utf-8")
      + "\nnamespace tess { void evaluate(double); }\n",
      encoding="utf-8",
  )
  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.1.1"
  )
  assert_overload(failures, "tess")


def test_identical_cross_header_redeclaration_is_compatible(tmp_path):
  header_path, payload = make_repo(tmp_path)
  first = tmp_path / "include/tess/pathfinding.h"
  second = tmp_path / "include/tess/optional.h"
  declaration = "\nnamespace tess { void evaluate(int value); }\n"
  first.write_text(
      first.read_text(encoding="utf-8") + declaration,
      encoding="utf-8",
  )
  refresh_payload(tmp_path, payload)
  snapshot_root = write_snapshot(tmp_path, payload)
  second.write_text(
      second.read_text(encoding="utf-8")
      + "\nnamespace tess { void evaluate(int other); }\n",
      encoding="utf-8",
  )
  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.1.1"
  )
  assert not any(
      "overload added to existing callable" in failure
      and "evaluate" in failure
      for failure in failures
  ), failures


def test_nested_template_redeclaration_ignores_parameter_names(tmp_path):
  header_path, payload = make_repo(tmp_path)
  first = tmp_path / "include/tess/pathfinding.h"
  second = tmp_path / "include/tess/optional.h"
  first.write_text(
      first.read_text(encoding="utf-8")
      + "\nnamespace tess { void evaluate("
      "std::pair<int, std::vector<long>> first, int count); }\n",
      encoding="utf-8",
  )
  refresh_payload(tmp_path, payload)
  snapshot_root = write_snapshot(tmp_path, payload)
  second.write_text(
      second.read_text(encoding="utf-8")
      + "\nnamespace tess { void evaluate("
      "std::pair<int, std::vector<long>> other, int value); }\n",
      encoding="utf-8",
  )
  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.1.1"
  )
  assert not any(
      "overload added to existing callable" in failure
      and "evaluate" in failure
      for failure in failures
  ), failures


def test_template_redeclaration_ignores_template_parameter_names(tmp_path):
  header_path, payload = make_repo(tmp_path)
  first = tmp_path / "include/tess/pathfinding.h"
  second = tmp_path / "include/tess/optional.h"
  first.write_text(
      first.read_text(encoding="utf-8")
      + "\nnamespace tess { template<class T> "
      "void evaluate(T value); }\n",
      encoding="utf-8",
  )
  refresh_payload(tmp_path, payload)
  snapshot_root = write_snapshot(tmp_path, payload)
  second.write_text(
      second.read_text(encoding="utf-8")
      + "\nnamespace tess { template<class U> "
      "void evaluate(U other); }\n",
      encoding="utf-8",
  )
  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.1.1"
  )
  assert not any(
      "overload added to existing callable" in failure
      and "evaluate" in failure
      for failure in failures
  ), failures


def test_distinct_trailing_constraints_remain_overloads(tmp_path):
  header_path, payload = make_repo(tmp_path)
  first = tmp_path / "include/tess/pathfinding.h"
  second = tmp_path / "include/tess/optional.h"
  first.write_text(
      first.read_text(encoding="utf-8")
      + "\nnamespace tess { template<class T> "
      "void evaluate(T) requires C1<T>; }\n",
      encoding="utf-8",
  )
  refresh_payload(tmp_path, payload)
  snapshot_root = write_snapshot(tmp_path, payload)
  second.write_text(
      second.read_text(encoding="utf-8")
      + "\nnamespace tess { template<class T> "
      "void evaluate(T) requires C2<T>; }\n",
      encoding="utf-8",
  )
  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.1.1"
  )
  assert_overload(failures, "tess")


def test_unnamed_nontype_template_parameter_types_remain_distinct(tmp_path):
  header_path, payload = make_repo(tmp_path)
  first = tmp_path / "include/tess/pathfinding.h"
  second = tmp_path / "include/tess/optional.h"
  first.write_text(
      first.read_text(encoding="utf-8")
      + "\nnamespace tess { template<int> void evaluate(); }\n",
      encoding="utf-8",
  )
  refresh_payload(tmp_path, payload)
  snapshot_root = write_snapshot(tmp_path, payload)
  second.write_text(
      second.read_text(encoding="utf-8")
      + "\nnamespace tess { template<long> void evaluate(); }\n",
      encoding="utf-8",
  )
  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.1.1"
  )
  assert_overload(failures, "tess")


def test_complex_redeclarations_ignore_declarator_names_and_top_cv(tmp_path):
  pairs = (
      ("int value[3]", "int other[3]"),
      ("void (*callback)(int)", "void (*other)(int)"),
      ("int value", "const int other"),
  )
  for index, (before, after) in enumerate(pairs):
    repo = tmp_path / f"variant-{index}"
    header_path, payload = make_repo(repo)
    first = repo / "include/tess/pathfinding.h"
    second = repo / "include/tess/optional.h"
    first.write_text(
        first.read_text(encoding="utf-8")
        + f"\nnamespace tess {{ void evaluate({before}); }}\n",
        encoding="utf-8",
    )
    refresh_payload(repo, payload)
    snapshot_root = write_snapshot(repo, payload)
    second.write_text(
        second.read_text(encoding="utf-8")
        + f"\nnamespace tess {{ void evaluate({after}); }}\n",
        encoding="utf-8",
    )
    failures = snapshots.check_snapshots(
        repo, snapshot_root, header_path, "1.1.1"
    )
    assert not any(
        "overload added to existing callable" in failure
        and "evaluate" in failure
        for failure in failures
    ), failures


def test_distinct_unnamed_multitoken_parameters_are_overloads(tmp_path):
  header_path, payload = make_repo(tmp_path)
  first = tmp_path / "include/tess/pathfinding.h"
  second = tmp_path / "include/tess/optional.h"
  first.write_text(
      first.read_text(encoding="utf-8")
      + "\nnamespace tess { void evaluate(const char); }\n",
      encoding="utf-8",
  )
  refresh_payload(tmp_path, payload)
  snapshot_root = write_snapshot(tmp_path, payload)
  second.write_text(
      second.read_text(encoding="utf-8")
      + "\nnamespace tess { void evaluate(const int); }\n",
      encoding="utf-8",
  )
  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.1.1"
  )
  assert_overload(failures, "tess")


def test_member_cv_and_ref_qualifiers_remain_distinct_overloads(tmp_path):
  additions = ("void evaluate() const;", "void evaluate() &;")
  for index, addition in enumerate(additions):
    repo = tmp_path / f"variant-{index}"
    header_path, payload = make_repo(repo)
    header = repo / "include/tess/tess.h"
    text = header.read_text(encoding="utf-8").replace(
        "struct StableOptions {",
        "struct StableOptions {\n  void evaluate();",
    )
    header.write_text(text, encoding="utf-8")
    refresh_payload(repo, payload)
    snapshot_root = write_snapshot(repo, payload)
    header.write_text(
        text.replace(
            "  int max_steps = 1;", f"  {addition}\n  int max_steps = 1;"
        ),
        encoding="utf-8",
    )
    failures = snapshots.check_snapshots(
        repo, snapshot_root, header_path, "1.1.1"
    )
    assert_overload(failures, "StableOptions")


def test_comment_marker_in_macro_string_does_not_hide_contract(tmp_path):
  header_path, payload = make_repo(tmp_path)
  header = tmp_path / "include/tess/tess.h"
  header.write_text(
      header.read_text(encoding="utf-8").replace(
          "#define TESS_COMPAT_LIMIT 8",
          '#define TESS_COMPAT_LIMIT "/*"',
      ),
      encoding="utf-8",
  )
  refresh_payload(tmp_path, payload)
  declarations = payload["api_contract"]["include/tess/tess.h"]
  assert any("type tess::StableOptions" in item for item in declarations)


def test_stable_macros_are_checked_across_headers(tmp_path):
  for index, directive in enumerate(
      ("#undef TESS_SHARED_LIMIT", "#define TESS_SHARED_LIMIT 9")
  ):
    repo = tmp_path / f"variant-{index}"
    header_path, payload = make_repo(repo)
    first = repo / "include/tess/pathfinding.h"
    second = repo / "include/tess/optional.h"
    first.write_text(
        first.read_text(encoding="utf-8")
        + "\n#define TESS_SHARED_LIMIT 8\n",
        encoding="utf-8",
    )
    refresh_payload(repo, payload)
    snapshot_root = write_snapshot(repo, payload)
    second.write_text(
        second.read_text(encoding="utf-8") + f"\n{directive}\n",
        encoding="utf-8",
    )
    failures = snapshots.check_snapshots(
        repo, snapshot_root, header_path, "1.1.1"
    )
    expected = "undefined" if index == 0 else "redefined"
    assert any(f"stable macro {expected}" in failure for failure in failures)

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
      "struct Root { void evaluate(int); };\n"
      "using Alias1 = Root;\nusing Alias2 = Alias1;\n"
      "struct StableOptions : Alias2 {",
      "struct Root { void evaluate(int); };\n"
      "typedef Root Alias;\nstruct StableOptions : Alias {",
      "struct Root { void evaluate(int); };\n"
      "template<class> using Alias = Root;\n"
      "struct StableOptions : Alias<int> {",
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


def test_dependent_base_conservatively_blocks_callable_addition(tmp_path):
  header_path, payload = make_repo(tmp_path)
  header = tmp_path / "include/tess/tess.h"
  text = header.read_text(encoding="utf-8").replace(
      "struct StableOptions {",
      "template<class T> struct StableOptions : T {",
  )
  header.write_text(text, encoding="utf-8")
  refresh_payload(tmp_path, payload)
  snapshot_root = write_snapshot(tmp_path, payload)
  header.write_text(
      text.replace(
          "  int max_steps = 1;",
          "  void evaluate(double);\n  int max_steps = 1;",
      ),
      encoding="utf-8",
  )
  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.1.1"
  )
  assert_overload(failures, "StableOptions")


def test_inaccessible_or_same_named_base_does_not_fake_overload(tmp_path):
  variants = (
      "struct Root { void evaluate(int); };\n"
      "struct StableOptions : private Root {",
      "struct Root { void evaluate(int); };\n"
      "class StableOptions : Root {\n public:",
      "namespace a { struct Root { void evaluate(int); }; }\n"
      "namespace b { struct Root { void other(int); }; }\n"
      "struct StableOptions : b::Root {",
      "template<bool> struct Root { void evaluate(int); };\n"
      "struct Other {}; constexpr int N=0, M=1;\n"
      "class StableOptions : Root<N < M>, public Other {\n public:",
      "namespace inner { struct Root { void evaluate(int); }; }\n"
      "namespace tess { namespace inner { struct Root {}; } }\n"
      "struct StableOptions : tess::inner::Root {",
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
            "  void evaluate(double);\n  int max_steps = 1;",
        ),
        encoding="utf-8",
    )
    failures = snapshots.check_snapshots(
        repo, snapshot_root, header_path, "1.1.1"
    )
    assert not any(
        "overload added to existing callable" in failure
        and "StableOptions" in failure
        for failure in failures
    ), failures


def test_qualified_namespace_import_does_not_merge_same_leaf(tmp_path):
  header_path, payload = make_repo(tmp_path)
  header = tmp_path / "include/tess/tess.h"
  text = header.read_text(encoding="utf-8").replace(
      "namespace tess {",
      "namespace a::external { void evaluate(int); }\n"
      "namespace b::external { void other(int); }\n"
      "namespace tess {\nusing namespace b::external;",
  )
  header.write_text(text, encoding="utf-8")
  refresh_payload(tmp_path, payload)
  snapshot_root = write_snapshot(tmp_path, payload)
  header.write_text(
      text.replace(
          "[[nodiscard]] auto stable_route",
          "void evaluate(double);\n[[nodiscard]] auto stable_route",
      ),
      encoding="utf-8",
  )
  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.1.1"
  )
  assert not any(
      "overload added to existing callable" in failure
      and "evaluate" in failure
      for failure in failures
  ), failures


def test_qualified_alias_does_not_merge_same_leaf(tmp_path):
  header_path, payload = make_repo(tmp_path)
  header = tmp_path / "include/tess/tess.h"
  text = header.read_text(encoding="utf-8").replace(
      "struct StableOptions {",
      "namespace a { struct Root { void evaluate(int); }; "
      "using Alias=Root; }\n"
      "namespace b { struct Other {}; using Alias=Other; }\n"
      "struct StableOptions : b::Alias {",
  )
  header.write_text(text, encoding="utf-8")
  refresh_payload(tmp_path, payload)
  snapshot_root = write_snapshot(tmp_path, payload)
  header.write_text(
      text.replace(
          "  int max_steps = 1;",
          "  void evaluate(double);\n  int max_steps = 1;",
      ),
      encoding="utf-8",
  )
  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.1.1"
  )
  assert not any(
      "overload added to existing callable" in failure
      and "StableOptions" in failure
      for failure in failures
  ), failures


def test_alias_template_argument_does_not_become_base(tmp_path):
  header_path, payload = make_repo(tmp_path)
  header = tmp_path / "include/tess/tess.h"
  text = header.read_text(encoding="utf-8").replace(
      "struct StableOptions {",
      "namespace a { struct Root { void evaluate(int); }; }\n"
      "namespace b { template<class> struct Wrapper {}; }\n"
      "using Alias = b::Wrapper<a::Root>;\n"
      "struct StableOptions : Alias {",
  )
  header.write_text(text, encoding="utf-8")
  refresh_payload(tmp_path, payload)
  snapshot_root = write_snapshot(tmp_path, payload)
  header.write_text(
      text.replace(
          "  int max_steps = 1;",
          "  void evaluate(double);\n  int max_steps = 1;",
      ),
      encoding="utf-8",
  )
  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.1.1"
  )
  assert not any(
      "overload added to existing callable" in failure
      and "StableOptions" in failure
      for failure in failures
  ), failures


def test_qualified_alias_head_preserves_namespace(tmp_path):
  header_path, payload = make_repo(tmp_path)
  header = tmp_path / "include/tess/tess.h"
  text = header.read_text(encoding="utf-8").replace(
      "struct StableOptions {",
      "namespace a { template<class> struct Wrapper { "
      "void evaluate(int); }; }\n"
      "namespace b { template<class> struct Wrapper {}; }\n"
      "using Alias = b::Wrapper<int>;\nstruct StableOptions : Alias {",
  )
  header.write_text(text, encoding="utf-8")
  refresh_payload(tmp_path, payload)
  snapshot_root = write_snapshot(tmp_path, payload)
  header.write_text(
      text.replace(
          "  int max_steps = 1;",
          "  void evaluate(double);\n  int max_steps = 1;",
      ),
      encoding="utf-8",
  )
  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.1.1"
  )
  assert not any(
      "overload added to existing callable" in failure
      and "StableOptions" in failure
      for failure in failures
  ), failures


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


def test_global_scope_overloads_are_checked(tmp_path):
  header_path, payload = make_repo(tmp_path)
  header = tmp_path / "include/tess/tess.h"
  text = header.read_text(encoding="utf-8") + "\nvoid evaluate(int);\n"
  header.write_text(text, encoding="utf-8")
  refresh_payload(tmp_path, payload)
  snapshot_root = write_snapshot(tmp_path, payload)
  header.write_text(text + "void evaluate(double);\n", encoding="utf-8")
  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.1.1"
  )
  assert_overload(failures, "evaluate")


def test_global_namespace_alias_imports_are_checked(tmp_path):
  header_path, payload = make_repo(tmp_path)
  header = tmp_path / "include/tess/tess.h"
  prefix = (
      "namespace external { void evaluate(int); }\n"
      "namespace ext = external;\nusing namespace ext;\n"
  )
  text = prefix + header.read_text(encoding="utf-8")
  header.write_text(text, encoding="utf-8")
  refresh_payload(tmp_path, payload)
  snapshot_root = write_snapshot(tmp_path, payload)
  header.write_text(text + "\nvoid evaluate(double);\n", encoding="utf-8")
  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.1.1"
  )
  assert_overload(failures, "evaluate")


def test_member_type_alias_base_callables_are_checked(tmp_path):
  header_path, payload = make_repo(tmp_path)
  header = tmp_path / "include/tess/tess.h"
  text = header.read_text(encoding="utf-8").replace(
      "struct StableOptions {",
      "struct Base { void evaluate(int); };\n"
      "struct Holder { using Alias = Base; };\n"
      "struct StableOptions : Holder::Alias {",
  )
  header.write_text(text, encoding="utf-8")
  refresh_payload(tmp_path, payload)
  snapshot_root = write_snapshot(tmp_path, payload)
  header.write_text(
      text.replace(
          "  int max_steps = 1;",
          "  void evaluate(double);\n  int max_steps = 1;",
      ),
      encoding="utf-8",
  )
  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.1.1"
  )
  assert_overload(failures, "StableOptions")


def test_unnamed_elaborated_parameter_types_remain_distinct(tmp_path):
  variants = (
      ("struct Foo", "struct Bar"),
      ("class Foo", "class Bar"),
      ("enum Foo", "enum Bar"),
      ("struct Foo *", "struct Bar *"),
  )
  for index, (before, after) in enumerate(variants):
    repo = tmp_path / f"variant-{index}"
    header_path, payload = make_repo(repo)
    header = repo / "include/tess/tess.h"
    text = header.read_text(encoding="utf-8").replace(
        "namespace tess {",
        f"namespace tess {{\nvoid evaluate({before});",
    )
    header.write_text(text, encoding="utf-8")
    refresh_payload(repo, payload)
    snapshot_root = write_snapshot(repo, payload)
    header.write_text(
        text.replace(
            "[[nodiscard]] auto stable_route",
            f"void evaluate({after});\n[[nodiscard]] auto stable_route",
        ),
        encoding="utf-8",
    )
    failures = snapshots.check_snapshots(
        repo, snapshot_root, header_path, "1.1.1"
    )
    assert_overload(failures, "evaluate")


def test_cpp_line_splicing_precedes_comment_parsing():
  visible_after_literal = (
      'namespace tess { constexpr auto text = "abc\\\n'
      '//not comment";\nvoid visible(); }'
  )
  continued_comment = (
      "namespace tess {\n// hidden \\\n"
      "void hidden();\nvoid visible();\n}"
  )
  contract = extract_api_contract(visible_after_literal)
  assert any("visible" in declaration for declaration in contract)
  assert "visible" in extract_public_symbols(visible_after_literal)

  contract = extract_api_contract(continued_comment)
  assert not any("hidden" in declaration for declaration in contract)
  assert any("visible" in declaration for declaration in contract)
  symbols = extract_public_symbols(continued_comment)
  assert "hidden" not in symbols
  assert "visible" in symbols


def test_line_spliced_literal_cannot_hide_later_overload(tmp_path):
  header_path, payload = make_repo(tmp_path)
  header = tmp_path / "include/tess/tess.h"
  text = header.read_text(encoding="utf-8").replace(
      "}  // namespace tess",
      'constexpr auto text = "abc\\\n'
      '//not comment";\nvoid evaluate(int);\n}  // namespace tess',
  )
  header.write_text(text, encoding="utf-8")
  refresh_payload(tmp_path, payload)
  snapshot_root = write_snapshot(tmp_path, payload)
  header.write_text(
      text.replace(
          "[[nodiscard]] auto stable_route",
          "void evaluate(double);\n[[nodiscard]] auto stable_route",
      ),
      encoding="utf-8",
  )
  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.1.1"
  )
  assert_overload(failures, "evaluate")
