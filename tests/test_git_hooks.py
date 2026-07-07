"""Unit tests for tools/git_hooks.py check helpers."""

from __future__ import annotations

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import git_hooks  # noqa: E402


def reader_for(files: dict[str, bytes]):
    return lambda path: files.get(path, b"")


# Positive/near-miss fixtures for each PRIVATE_PATTERNS entry, in order.
# Positives are built from concatenated fragments so this test file never
# contains the flagged byte sequences itself.
PRIVATE_CASES = (
    (b"main" + b"tainer was here", b"main" + b"tainers was here"),
    (b"contri" + b"butor", b"contri" + b"butoron"),
    (b"Link" + b"edIn.com/in/example", b"link" + b"edin.org/in/example"),
    (b"/Us" + b"ers/example/notes", b"/Us" + b"er/example/notes"),
    (b"/pri" + b"vate/tmp/thing", b"/pri" + b"vatex/tmp/thing"),
    (b"/ho" + b"me/example/file", b"/ho" + b"me//file"),
    (b"-----BEGIN " + b"PRIV" + b"ATE KEY-----", b"BEGIN PUBLIC KEY"),
    (b"AW" + b"S_REGION = value", b"AW" + b"S-REGION = value"),
    (b"GITHUB_TO" + b"KEN = value", b"GITHUB_TO" + b"KENS value"),
    (b"github_" + b"pat_" + b"abc123", b"github_" + b"pat_ = value"),
    (b"gh" + b"p_" + b"a" * 40, b"gh" + b"p_ prefix only"),
    (b"sk-" + b"a" * 20, b"sk-" + b"a" * 19),
    (b"pass" + b"word: value", b"pass" + b"words: value"),
)


def test_private_case_table_covers_every_pattern():
    assert len(PRIVATE_CASES) == len(git_hooks.PRIVATE_PATTERNS)


@pytest.mark.parametrize(
    ("index", "positive", "near_miss"),
    [(i, pos, neg) for i, (pos, neg) in enumerate(PRIVATE_CASES)],
)
def test_private_pattern_fires_and_near_miss_passes(
    index, positive, near_miss
):
    pattern = git_hooks.PRIVATE_PATTERNS[index]
    assert pattern.search(positive), f"pattern {index} missed its fixture"
    assert not pattern.search(near_miss), (
        f"pattern {index} matched its near-miss"
    )


def test_find_private_matches_flags_offenders_and_skips_clean():
    files = {
        "bad.md": PRIVATE_CASES[0][0],
        "good.md": b"\n".join(neg for _, neg in PRIVATE_CASES),
        "skipped.bin": PRIVATE_CASES[0][0],
    }
    offenders = git_hooks.find_private_matches(
        sorted(files), reader_for(files)
    )
    assert offenders == ["bad.md"]


def test_find_conflict_markers_detects_each_marker_kind():
    marker_lines = (
        b"<<<" + b"<<<< HEAD",
        b">>>" + b">>>> theirs",
        b"|||" + b"|||| base",
        b"===" + b"====",
    )
    for line in marker_lines:
        files = {"a.md": b"text\n" + line + b"\nmore\n"}
        offenders = git_hooks.find_conflict_markers(
            ["a.md"], reader_for(files)
        )
        assert offenders == ["a.md"], line


def test_find_conflict_markers_ignores_near_misses_and_binaries():
    files = {
        "a.md": b"====== =\nx <<<" + b"<<<< y\n====" + b"====\n",
        "b.bin": b"<<<" + b"<<<< HEAD\n",
    }
    offenders = git_hooks.find_conflict_markers(
        sorted(files), reader_for(files)
    )
    assert offenders == []


def test_find_token_overruns_flags_only_oversized_text_files():
    tiktoken = pytest.importorskip("tiktoken")
    encoder = tiktoken.get_encoding("cl100k_base")
    files = {
        "big.md": b"word " * 30_000,
        "small.md": b"short file\n",
        "big.bin": b"word " * 30_000,
    }
    overruns = git_hooks.find_token_overruns(
        sorted(files), reader_for(files), encoder
    )
    assert [path for path, _ in overruns] == ["big.md"]
    assert overruns[0][1] > git_hooks.TOKEN_LIMIT


SHA_A = "a" * 40
SHA_B = "b" * 40
ZEROS = "0" * 40


def test_parse_push_refs_parses_update_and_delete_lines():
    text = (
        f"refs/heads/main {SHA_A} refs/heads/main {SHA_B}\n"
        f"refs/heads/gone {ZEROS} refs/heads/gone {SHA_B}\n"
        f"refs/heads/new {SHA_A} refs/heads/new {ZEROS}\n"
    )
    refs = git_hooks.parse_push_refs(text)
    assert len(refs) == 3
    update, delete, new = refs
    assert update.local_ref == "refs/heads/main"
    assert update.local_sha == SHA_A
    assert update.remote_ref == "refs/heads/main"
    assert update.remote_sha == SHA_B
    assert not update.is_delete() and not update.is_new()
    assert delete.is_delete() and not delete.is_new()
    assert new.is_new() and not new.is_delete()


def test_parse_push_refs_ignores_blank_and_malformed_lines():
    text = f"\nnot a ref line\n{SHA_A}\nrefs/x {SHA_A} refs/x\n"
    assert git_hooks.parse_push_refs(text) == []
    assert git_hooks.parse_push_refs("") == []


def test_should_build_bench_for_new_remote_ref():
    ref = git_hooks.PushRef(
        "refs/heads/new", SHA_A, "refs/heads/new", ZEROS
    )
    assert git_hooks.should_build_bench([ref]) is True


def test_should_build_bench_when_range_is_unresolvable():
    ref = git_hooks.PushRef(
        "refs/heads/x", SHA_A, "refs/heads/x", SHA_B
    )
    assert git_hooks.should_build_bench([ref]) is True


def test_bench_paths_changed_prefixes():
    assert git_hooks.bench_paths_changed(["bench/foo.cc"])
    assert git_hooks.bench_paths_changed(["cmake/Foo.cmake"])
    assert git_hooks.bench_paths_changed(["include/tess/tess.h"])
    assert git_hooks.bench_paths_changed(["CMakeLists.txt"])
    assert not git_hooks.bench_paths_changed(
        ["tests/CMakeLists.txt", "docs/git-hooks.md", "tools/git_hooks.py"]
    )
    assert not git_hooks.bench_paths_changed([])


CMAKE_FIXTURE = """
add_executable(tess_alpha_test alpha.cc)
gtest_discover_tests(tess_alpha_test)

add_executable(
  tess_beta_test
  helper.cc
  beta.cc
)
target_link_libraries(tess_beta_test PRIVATE tess::tess)

add_executable(other_tool tool.cc)
"""

AGENTS_FIXTURE = """
# Tests

- `tess_alpha_test`: covers alpha.
"""


def test_extract_cmake_test_targets_handles_multiline_forms():
    targets = git_hooks.extract_cmake_test_targets(CMAKE_FIXTURE)
    assert targets == ["tess_alpha_test", "tess_beta_test"]


def test_missing_agents_targets_reports_undocumented_targets():
    missing = git_hooks.missing_agents_targets(
        CMAKE_FIXTURE, AGENTS_FIXTURE
    )
    assert missing == ["tess_beta_test"]


def test_missing_agents_targets_empty_when_documented():
    agents = AGENTS_FIXTURE + "- `tess_beta_test`: covers beta.\n"
    assert git_hooks.missing_agents_targets(CMAKE_FIXTURE, agents) == []


def test_repo_agents_md_documents_all_cmake_test_targets():
    root = Path(__file__).resolve().parents[1]
    missing = git_hooks.missing_agents_targets(
        (root / "tests" / "CMakeLists.txt").read_text(),
        (root / "tests" / "AGENTS.md").read_text(),
    )
    assert missing == []
