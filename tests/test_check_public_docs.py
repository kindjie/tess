"""Unit tests for tools/check_public_docs.py."""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import check_public_docs as cpd  # noqa: E402


SYNTHETIC_HEADER = """
namespace tess {

/** A documented public type. */
struct Documented {};

/// A documented function.
template <typename Value>
[[nodiscard]] auto make_documented(Value value) -> Documented;

struct Missing {};

namespace detail {
struct Hidden {};
}  // namespace detail

}  // namespace tess
"""

OVERLOADED_HEADER = """
namespace tess {

/// Returns the integer widget.
template <typename Value>
[[nodiscard]] auto load_widget(Value value, int index) -> int;

template <typename Value>
[[nodiscard]] auto load_widget(Value value, double index) -> int;

}  // namespace tess
"""

CONDITIONAL_MACRO_HEADER = """
#if defined(TESS_WIDGETS_ENABLED)
/** Reports whether widget support was compiled in. */
#define TESS_WIDGETS_ACTIVE 1
#else
#define TESS_WIDGETS_ACTIVE 0
#endif
"""

REDECLARED_HEADER = """
namespace tess {

/// Loads a widget with an optional index.
auto load_widget(int index = 0) -> int;

auto load_widget(int index) -> int {
  return index;
}

}  // namespace tess
"""


def test_extract_doxygen_symbols_handles_blocks_lines_and_templates():
    assert cpd.extract_doxygen_symbols(SYNTHETIC_HEADER) == {
        "Documented",
        "make_documented",
    }


def test_check_headers_reports_only_undocumented_public_symbols(tmp_path):
    header = tmp_path / "include" / "tess" / "example.h"
    header.parent.mkdir(parents=True)
    header.write_text(SYNTHETIC_HEADER, encoding="utf-8")

    failures = cpd.check_headers(tmp_path, ["include/tess/example.h"])

    assert len(failures) == 1
    assert failures[0].endswith(
        ": public declaration 'Missing' lacks Doxygen documentation"
    )


def test_check_headers_reports_undocumented_overload(tmp_path):
    header = tmp_path / "include" / "tess" / "overloaded.h"
    header.parent.mkdir(parents=True)
    header.write_text(OVERLOADED_HEADER, encoding="utf-8")

    failures = cpd.check_headers(tmp_path, ["include/tess/overloaded.h"])

    assert len(failures) == 1
    assert failures[0].endswith(
        ": public declaration 'load_widget' lacks Doxygen documentation"
    )


def test_check_headers_deduplicates_conditional_macro_definitions(tmp_path):
    header = tmp_path / "include" / "tess" / "conditional.h"
    header.parent.mkdir(parents=True)
    header.write_text(CONDITIONAL_MACRO_HEADER, encoding="utf-8")

    assert cpd.check_headers(tmp_path, ["include/tess/conditional.h"]) == []


def test_check_headers_deduplicates_defaulted_redeclarations(tmp_path):
    header = tmp_path / "include" / "tess" / "redeclared.h"
    header.parent.mkdir(parents=True)
    header.write_text(REDECLARED_HEADER, encoding="utf-8")

    assert cpd.check_headers(tmp_path, ["include/tess/redeclared.h"]) == []


def test_check_headers_reports_missing_files(tmp_path):
    assert cpd.check_headers(tmp_path, ["include/tess/absent.h"]) == [
        "include/tess/absent.h: documentation-covered header not found"
    ]


def test_all_real_public_headers_have_doxygen_documentation():
    repo_root = Path(__file__).resolve().parents[1]
    assert cpd.check_headers(repo_root, cpd.DEFAULT_HEADERS) == []
