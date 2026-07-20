"""Tests for executable documentation snippet synchronization."""

from __future__ import annotations

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import check_doc_snippets as cds  # noqa: E402


SOURCE = """\
#include <cstdint>

// [quickstart]
int answer() {
  return 42;
}
// [quickstart]
"""

MARKDOWN = """\
# Guide

<!-- tess-snippet: quickstart source=examples/quickstart.cc -->
```cpp
int answer() {
  return 42;
}
```
<!-- /tess-snippet -->
"""


def test_extract_source_regions_reads_named_region():
  assert cds.extract_source_regions(SOURCE, "example.cc") == {
    "quickstart": "int answer() {\n  return 42;\n}"
  }


def test_extract_source_regions_dedents_function_body_fragments():
  source = "// [body]\n  int answer = 42;\n// [body]\n"

  assert cds.extract_source_regions(source, "example.cc") == {
    "body": "int answer = 42;"
  }


def test_extract_source_regions_rejects_unclosed_region():
  with pytest.raises(ValueError, match="unclosed snippet 'quickstart'"):
    cds.extract_source_regions("// [quickstart]\nint value;\n", "example.cc")


def test_extract_markdown_snippets_reads_source_and_body():
  snippets = cds.extract_markdown_snippets(MARKDOWN, "README.md")
  assert snippets == [
    cds.MarkdownSnippet(
      name="quickstart",
      source=Path("examples/quickstart.cc"),
      language="cpp",
      body="int answer() {\n  return 42;\n}",
      start=80,
      end=109,
    )
  ]


def test_check_markdown_reports_drift(tmp_path):
  source = tmp_path / "examples" / "quickstart.cc"
  source.parent.mkdir(parents=True)
  source.write_text(SOURCE, encoding="utf-8")
  readme = tmp_path / "README.md"
  readme.write_text(MARKDOWN.replace("42", "41", 1), encoding="utf-8")

  failures = cds.check_markdown(tmp_path, readme)

  assert failures == [
    "README.md: snippet 'quickstart' differs from examples/quickstart.cc"
  ]


def test_check_markdown_rejects_unbacked_cpp_fence(tmp_path):
  guide = tmp_path / "docs" / "guide.md"
  guide.parent.mkdir(parents=True)
  guide.write_text(
    "# Guide\n\n```cpp\nint answer = 42;\n```\n", encoding="utf-8"
  )

  failures = cds.check_markdown(tmp_path, guide)

  assert failures == [
    "docs/guide.md:3: C++ fence is not backed by a compiled source region"
  ]


def test_check_repository_allows_unbacked_historical_cpp(tmp_path):
  history = tmp_path / "docs" / "tdd" / "old-design.md"
  history.parent.mkdir(parents=True)
  history.write_text(
    "# Historical design\n\n```cpp\nint proposal;\n```\n",
    encoding="utf-8",
  )

  assert cds.check_repository(tmp_path) == []


def test_check_repository_rejects_source_not_registered_with_cmake(tmp_path):
  source = tmp_path / "examples" / "guide.cc"
  source.parent.mkdir(parents=True)
  source.write_text(
    "// [guide]\nint answer = 42;\n// [guide]\n", encoding="utf-8"
  )
  readme = tmp_path / "README.md"
  readme.write_text(
    "<!-- tess-snippet: guide source=examples/guide.cc -->\n"
    "```cpp\nint answer = 42;\n```\n"
    "<!-- /tess-snippet -->\n",
    encoding="utf-8",
  )

  assert cds.check_repository(tmp_path) == [
    "README.md: snippet 'guide' source examples/guide.cc is not registered "
    "in examples/CMakeLists.txt"
  ]


def test_update_markdown_replaces_only_the_fenced_body(tmp_path):
  source = tmp_path / "examples" / "quickstart.cc"
  source.parent.mkdir(parents=True)
  source.write_text(SOURCE, encoding="utf-8")
  readme = tmp_path / "README.md"
  readme.write_text(MARKDOWN.replace("42", "41", 1), encoding="utf-8")

  assert cds.update_markdown(tmp_path, readme)
  assert readme.read_text(encoding="utf-8") == MARKDOWN


def test_all_real_documentation_snippets_match_sources():
  repo_root = Path(__file__).resolve().parents[1]
  assert cds.check_repository(repo_root) == []
