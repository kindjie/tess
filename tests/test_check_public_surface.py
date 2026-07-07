"""Unit tests for tools/check_public_surface.py."""

from __future__ import annotations

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import check_public_surface as cps  # noqa: E402


CMAKE_TEXT = """
set(
  TESS_PUBLIC_HEADERS
  include/tess/core/shape.h
  include/tess/sim/movement.h
)

target_sources(tess INTERFACE FILE_SET HEADERS)
"""

SYNTHETIC_HEADER = """
#pragma once

#include <cstdint>

// A struct mentioned in a comment: struct CommentedOut {};
/* struct BlockCommentedOut {}; */

#define WIDGET_MACRO(expr) \\
  do {                     \\
    expr;                  \\
  } while (false)

namespace tess {

struct Widget {
  int member_field = 0;
  void member_function(int value);
  friend constexpr bool operator==(Widget lhs, Widget rhs) noexcept = default;
};

class GadgetCache {
 public:
  void clear() noexcept;
};

enum class WidgetStatus : std::uint8_t {
  Ok,
  Broken,
};

template <typename Shape>
struct ForwardDeclared;

namespace detail {

struct HiddenDetail {};

constexpr int hidden_helper(int value) noexcept { return value; }

}  // namespace detail

[[nodiscard]] constexpr bool widget_valid(Widget widget) noexcept {
  struct LocalInsideFunction {};
  return true;
}

inline void reset_widget(Widget& widget) noexcept {}

[[nodiscard]] inline auto make_widget(int seed) -> Widget {
  return Widget{};
}

template <typename World>
[[nodiscard]] auto tick_widget(World& world, Widget& widget,
                               int max_steps = 1) -> WidgetStatus {
  auto local_lambda = [](int value) { return value; };
  return WidgetStatus::Ok;
}

}  // namespace tess

namespace tess::internal {

struct AlsoHidden {};

}  // namespace tess::internal
"""


def test_parse_public_headers_extracts_header_lines():
    headers = cps.parse_public_headers(CMAKE_TEXT)
    assert headers == [
        "include/tess/core/shape.h",
        "include/tess/sim/movement.h",
    ]


def test_parse_public_headers_requires_the_set_block():
    with pytest.raises(ValueError):
        cps.parse_public_headers("set(OTHER_VAR a.h)")


def test_extract_finds_types_and_free_functions():
    symbols = cps.extract_public_symbols(SYNTHETIC_HEADER)
    assert {
        "Widget",
        "GadgetCache",
        "WidgetStatus",
        "ForwardDeclared",
        "widget_valid",
        "reset_widget",
        "make_widget",
        "tick_widget",
    } <= symbols


def test_extract_skips_members_locals_and_detail_namespaces():
    symbols = cps.extract_public_symbols(SYNTHETIC_HEADER)
    assert "member_function" not in symbols
    assert "LocalInsideFunction" not in symbols
    assert "HiddenDetail" not in symbols
    assert "hidden_helper" not in symbols
    assert "AlsoHidden" not in symbols
    assert "operator" not in symbols


def test_extract_skips_comments_and_macro_bodies():
    symbols = cps.extract_public_symbols(SYNTHETIC_HEADER)
    assert "CommentedOut" not in symbols
    assert "BlockCommentedOut" not in symbols
    # The multi-line macro's do/while braces must not desynchronize the
    # scope stack; namespace-scope symbols after it are still found.
    assert "widget_valid" in symbols


def test_extract_handles_enum_struct_and_plain_class():
    text = (
        "namespace tess {\n"
        "enum struct Mode : int { A, B };\n"
        "class Owner;\n"
        "}  // namespace tess\n"
    )
    assert cps.extract_public_symbols(text) == {"Mode", "Owner"}


def test_documented_symbols_unions_all_docs():
    manifest = {
        "docs/a.md": ["Widget", "make_widget"],
        "docs/b.md": ["GadgetCache"],
    }
    assert cps.documented_symbols(manifest) == {
        "Widget",
        "make_widget",
        "GadgetCache",
    }


def test_documented_symbols_rejects_non_list_entries():
    with pytest.raises(ValueError):
        cps.documented_symbols({"docs/a.md": "Widget"})


@pytest.fixture()
def synthetic_repo(tmp_path):
    header = tmp_path / "include" / "tess" / "widget.h"
    header.parent.mkdir(parents=True)
    header.write_text(SYNTHETIC_HEADER, encoding="utf-8")
    return tmp_path


def test_check_headers_passes_with_complete_manifest(synthetic_repo):
    manifest = {
        "docs/widget.md": [
            "Widget",
            "GadgetCache",
            "WidgetStatus",
            "ForwardDeclared",
            "widget_valid",
            "reset_widget",
            "make_widget",
            "tick_widget",
        ],
    }
    failures = cps.check_headers(
        synthetic_repo, ["include/tess/widget.h"], manifest
    )
    assert failures == []


def test_check_headers_reports_each_missing_symbol(synthetic_repo):
    manifest = {
        "docs/widget.md": [
            "Widget",
            "GadgetCache",
            "WidgetStatus",
            "ForwardDeclared",
            "widget_valid",
            "reset_widget",
        ],
    }
    failures = cps.check_headers(
        synthetic_repo, ["include/tess/widget.h"], manifest
    )
    assert failures == [
        "include/tess/widget.h: undocumented public symbol 'make_widget'",
        "include/tess/widget.h: undocumented public symbol 'tick_widget'",
    ]


def test_check_headers_reports_missing_header_file(synthetic_repo):
    failures = cps.check_headers(
        synthetic_repo, ["include/tess/absent.h"], {"docs/widget.md": []}
    )
    assert failures == [
        "include/tess/absent.h: listed public header not found"
    ]


def test_real_manifest_covers_real_headers():
    """The committed manifest must stay complete for the real tree."""
    repo_root = Path(__file__).resolve().parents[1]
    headers = cps.parse_public_headers(
        (repo_root / "CMakeLists.txt").read_text(encoding="utf-8")
    )
    manifest = cps.load_manifest(
        repo_root / "docs" / "architecture" / "surface.json"
    )
    for doc in manifest:
        assert (repo_root / doc).is_file(), f"manifest doc {doc} missing"
    assert cps.check_headers(repo_root, headers, manifest) == []
