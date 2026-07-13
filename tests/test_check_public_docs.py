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


def test_extract_doxygen_symbols_handles_blocks_lines_and_templates():
    assert cpd.extract_doxygen_symbols(SYNTHETIC_HEADER) == {
        "Documented",
        "make_documented",
    }


def test_check_headers_reports_only_undocumented_public_symbols(tmp_path):
    header = tmp_path / "include" / "tess" / "example.h"
    header.parent.mkdir(parents=True)
    header.write_text(SYNTHETIC_HEADER, encoding="utf-8")

    assert cpd.check_headers(tmp_path, ["include/tess/example.h"]) == [
        "include/tess/example.h: public symbol 'Missing' lacks Doxygen "
        "documentation"
    ]


def test_check_headers_reports_missing_files(tmp_path):
    assert cpd.check_headers(tmp_path, ["include/tess/absent.h"]) == [
        "include/tess/absent.h: documentation-covered header not found"
    ]


def test_real_opt_in_headers_have_doxygen_documentation():
    repo_root = Path(__file__).resolve().parents[1]
    assert cpd.check_headers(repo_root, cpd.DEFAULT_HEADERS) == []
