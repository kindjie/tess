"""Tests for changelog fragment validation and assembly."""

from __future__ import annotations

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import assemble_changelog as ac  # noqa: E402


REPO_ROOT = Path(__file__).resolve().parents[1]


@pytest.fixture(autouse=True)
def _isolate_optimization_log(tmp_path_factory, monkeypatch):
  """Keep every test off the real optimization log.

  `release` writes each dated document it has fragments for. A test that
  patches only the changelog paths would otherwise assemble into the
  repository's own `optimization-log.md` and delete its fragments -- and
  would do so silently, because the assertions are about the changelog.
  """
  directory = tmp_path_factory.mktemp("optlog.d")
  document = tmp_path_factory.mktemp("optlog") / "optimization-log.md"
  document.write_text("# Optimization Log\n\n## 2026-01-01 - Seed\n\n- Old.\n",
                      encoding="utf-8")
  monkeypatch.setattr(ac, "OPTLOG_FRAGMENTS", directory)
  monkeypatch.setattr(ac, "OPTIMIZATION_LOG", document)
  return directory, document


def _write(directory: Path, name: str, body: str) -> Path:
  path = directory / name
  path.write_text(body, encoding="utf-8")
  return path


# --- Naming and shape ---


def test_release_fragment_accepts_a_well_formed_entry(tmp_path):
  path = _write(tmp_path, "sparse-residency.fixed.md", "- Something broke.\n")

  assert ac.validate_release_fragment(path) == "fixed"


def test_release_fragment_rejects_an_unknown_category(tmp_path):
  path = _write(tmp_path, "thing.improved.md", "- Something.\n")

  with pytest.raises(ac.FragmentError, match="expected <slug>"):
    ac.validate_release_fragment(path)


def test_release_fragment_rejects_a_missing_category(tmp_path):
  path = _write(tmp_path, "thing.md", "- Something.\n")

  with pytest.raises(ac.FragmentError):
    ac.validate_release_fragment(path)


def test_release_fragment_rejects_an_empty_file(tmp_path):
  path = _write(tmp_path, "thing.fixed.md", "\n\n")

  with pytest.raises(ac.FragmentError, match="empty"):
    ac.validate_release_fragment(path)


def test_release_fragment_rejects_prose_outside_a_list_item(tmp_path):
  """Assembly is concatenation, so a fragment must already be list items.

  A bare paragraph would render inside a `###` section as body text
  rather than an entry, which reads correctly in the fragment and wrongly
  in the assembled file.
  """
  path = _write(tmp_path, "thing.fixed.md", "This is a paragraph.\n")

  with pytest.raises(ac.FragmentError, match="list item"):
    ac.validate_release_fragment(path)


def test_release_fragment_allows_wrapped_continuation_lines(tmp_path):
  path = _write(
    tmp_path,
    "thing.fixed.md",
    "- A long entry that wraps\n  onto a second line.\n",
  )

  assert ac.validate_release_fragment(path) == "fixed"


def test_decision_fragment_requires_a_dated_name_and_matching_heading(tmp_path):
  good = _write(
    tmp_path, "2026-08-07-a-decision.md", "## 2026-08-07 - A decision\n\n- Why.\n"
  )

  assert ac.validate_decision_fragment(good) == "2026-08-07"


def test_decision_fragment_rejects_a_heading_date_that_contradicts_the_name(
  tmp_path,
):
  path = _write(
    tmp_path, "2026-08-07-a-decision.md", "## 2026-08-09 - A decision\n\n- Why.\n"
  )

  with pytest.raises(ac.FragmentError, match="must open with"):
    ac.validate_decision_fragment(path)


def test_decision_fragment_rejects_an_undated_name(tmp_path):
  path = _write(tmp_path, "a-decision.md", "## 2026-08-07 - A decision\n")

  with pytest.raises(ac.FragmentError, match="YYYY-MM-DD"):
    ac.validate_decision_fragment(path)


# --- Rendering ---


def test_release_sections_group_by_category_in_canonical_order(
  tmp_path, monkeypatch
):
  monkeypatch.setattr(ac, "RELEASE_FRAGMENTS", tmp_path)
  _write(tmp_path, "b.fixed.md", "- Fixed thing.\n")
  _write(tmp_path, "a.added.md", "- Added thing.\n")
  _write(tmp_path, "c.performance.md", "- Faster thing.\n")

  rendered = ac.render_release_sections()

  assert rendered.index("### Added") < rendered.index("### Fixed")
  assert rendered.index("### Fixed") < rendered.index("### Performance")
  for entry in ("- Added thing.", "- Fixed thing.", "- Faster thing."):
    assert entry in rendered


def test_release_sections_omit_categories_with_no_fragments(
  tmp_path, monkeypatch
):
  monkeypatch.setattr(ac, "RELEASE_FRAGMENTS", tmp_path)
  _write(tmp_path, "a.fixed.md", "- Only a fix.\n")

  rendered = ac.render_release_sections()

  assert "### Fixed" in rendered
  assert "### Added" not in rendered


def test_decision_sections_render_newest_first(tmp_path, monkeypatch):
  monkeypatch.setattr(ac, "DECISION_FRAGMENTS", tmp_path)
  _write(tmp_path, "2026-08-01-older.md", "## 2026-08-01 - Older\n\n- One.\n")
  _write(tmp_path, "2026-08-09-newer.md", "## 2026-08-09 - Newer\n\n- Two.\n")

  rendered = ac.render_decision_sections()

  assert rendered.index("2026-08-09") < rendered.index("2026-08-01")


def test_no_fragment_directory_renders_nothing(tmp_path, monkeypatch):
  monkeypatch.setattr(ac, "RELEASE_FRAGMENTS", tmp_path / "absent")

  assert ac.render_release_sections() == ""


def test_gitkeep_is_not_a_fragment(tmp_path, monkeypatch):
  monkeypatch.setattr(ac, "RELEASE_FRAGMENTS", tmp_path)
  _write(tmp_path, ".gitkeep", "")

  assert ac.check() == []
  assert ac.render_release_sections() == ""


# --- Assembly ---


def test_release_folds_fragments_under_the_version_and_clears_them(
  tmp_path, monkeypatch
):
  release_dir = tmp_path / "changelog.d"
  release_dir.mkdir()
  decision_dir = tmp_path / "decisions.d"
  decision_dir.mkdir()
  changelog = tmp_path / "CHANGELOG.md"
  changelog.write_text(
    "# Changelog\n\n## [Unreleased]\n\n## [0.12.0] - 2026-08-05\n\n- Old.\n",
    encoding="utf-8",
  )
  decisions = tmp_path / "decisions.md"
  decisions.write_text(
    "# Design Changelog\n\nPreamble.\n\n## 2026-08-01 - Earlier\n\n- Old.\n",
    encoding="utf-8",
  )
  monkeypatch.setattr(ac, "RELEASE_FRAGMENTS", release_dir)
  monkeypatch.setattr(ac, "DECISION_FRAGMENTS", decision_dir)
  monkeypatch.setattr(ac, "RELEASE_CHANGELOG", changelog)
  monkeypatch.setattr(ac, "DECISION_CHANGELOG", decisions)
  _write(release_dir, "a.fixed.md", "- A fix.\n")
  _write(decision_dir, "2026-08-07-why.md", "## 2026-08-07 - Why\n\n- Because.\n")

  assert ac.release("0.13.0", "2026-09-01", dry_run=False) == 0

  text = changelog.read_text(encoding="utf-8")
  assert "## [0.13.0] - 2026-09-01" in text
  assert "- A fix." in text
  # The prior release and the Unreleased heading both survive.
  assert "## [0.12.0] - 2026-08-05" in text
  assert "## [Unreleased]" in text
  assert text.index("## [Unreleased]") < text.index("## [0.13.0]")
  assert text.index("## [0.13.0]") < text.index("## [0.12.0]")

  decision_text = decisions.read_text(encoding="utf-8")
  assert "## 2026-08-07 - Why" in decision_text
  assert "## 2026-08-01 - Earlier" in decision_text
  assert decision_text.index("2026-08-07") < decision_text.index("2026-08-01")
  assert decision_text.startswith("# Design Changelog")

  # Fragments are consumed, so a second release cannot duplicate them.
  assert ac._fragment_files(release_dir) == []
  assert ac._fragment_files(decision_dir) == []


def test_release_refuses_to_write_when_a_fragment_is_invalid(
  tmp_path, monkeypatch
):
  release_dir = tmp_path / "changelog.d"
  release_dir.mkdir()
  changelog = tmp_path / "CHANGELOG.md"
  changelog.write_text("# Changelog\n\n## [Unreleased]\n", encoding="utf-8")
  monkeypatch.setattr(ac, "RELEASE_FRAGMENTS", release_dir)
  monkeypatch.setattr(ac, "DECISION_FRAGMENTS", tmp_path / "absent")
  monkeypatch.setattr(ac, "RELEASE_CHANGELOG", changelog)
  _write(release_dir, "bad.nonsense.md", "- Entry.\n")

  assert ac.release("0.13.0", "2026-09-01", dry_run=False) == 1
  # Nothing was written and nothing was deleted.
  assert changelog.read_text(encoding="utf-8") == "# Changelog\n\n## [Unreleased]\n"
  assert len(ac._fragment_files(release_dir)) == 1


def test_dry_run_leaves_both_files_and_fragments_untouched(tmp_path, monkeypatch):
  release_dir = tmp_path / "changelog.d"
  release_dir.mkdir()
  changelog = tmp_path / "CHANGELOG.md"
  original = "# Changelog\n\n## [Unreleased]\n"
  changelog.write_text(original, encoding="utf-8")
  monkeypatch.setattr(ac, "RELEASE_FRAGMENTS", release_dir)
  monkeypatch.setattr(ac, "DECISION_FRAGMENTS", tmp_path / "absent")
  monkeypatch.setattr(ac, "RELEASE_CHANGELOG", changelog)
  _write(release_dir, "a.fixed.md", "- A fix.\n")

  assert ac.release("0.13.0", "2026-09-01", dry_run=True) == 0

  assert changelog.read_text(encoding="utf-8") == original
  assert len(ac._fragment_files(release_dir)) == 1


# --- The repository's own fragments ---


def test_repository_fragments_are_valid():
  """Whatever is pending on this branch must assemble."""
  assert ac.check() == []


def test_fragment_directories_exist():
  """A missing directory would make every branch recreate it."""
  assert (REPO_ROOT / "changelog.d").is_dir()
  assert (REPO_ROOT / "docs" / "decisions" / "changelog.d").is_dir()


def test_release_merges_an_existing_unreleased_body_by_category(
  tmp_path, monkeypatch
):
  """Entries predating fragments must not produce a second heading set.

  Found by a dry run against the real changelogs: stacking fragment
  sections beside the existing `Unreleased` body emitted two `### Fixed`
  headings under one release. Once the transition is done the body is
  empty and this is a no-op.
  """
  release_dir = tmp_path / "changelog.d"
  release_dir.mkdir()
  changelog = tmp_path / "CHANGELOG.md"
  changelog.write_text(
    "# Changelog\n\n## [Unreleased]\n\n"
    "### Fixed\n\n- Written before fragments existed.\n\n"
    "## [0.12.0] - 2026-08-05\n\n### Fixed\n\n- Shipped earlier.\n",
    encoding="utf-8",
  )
  monkeypatch.setattr(ac, "RELEASE_FRAGMENTS", release_dir)
  monkeypatch.setattr(ac, "DECISION_FRAGMENTS", tmp_path / "absent")
  monkeypatch.setattr(ac, "RELEASE_CHANGELOG", changelog)
  _write(release_dir, "later.fixed.md", "- Written as a fragment.\n")

  assert ac.release("0.13.0", "2026-09-01", dry_run=False) == 0

  text = changelog.read_text(encoding="utf-8")
  release_section = text[text.index("## [0.13.0]") : text.index("## [0.12.0]")]
  assert release_section.count("### Fixed") == 1
  assert "- Written before fragments existed." in release_section
  assert "- Written as a fragment." in release_section
  # The prior release keeps its own section, and Unreleased is emptied.
  assert "- Shipped earlier." in text[text.index("## [0.12.0]") :]
  unreleased = text[text.index("## [Unreleased]") : text.index("## [0.13.0]")]
  assert unreleased.strip() == "## [Unreleased]"


def test_parse_sections_keeps_an_unknown_category_rather_than_dropping_it():
  sections = ac.parse_sections("### Curiosities\n\n- An entry.\n")

  assert sections["curiosities"] == ["- An entry."]


def test_release_fragment_rejects_indented_only_content(tmp_path):
  """Every line "continues" a list item that was never started.

  Nonempty and all-indented passes a naive line check, but the assembled
  category would contain no markdown list item at all.
  """
  path = _write(tmp_path, "thing.fixed.md", "  orphan text\n")

  with pytest.raises(ac.FragmentError, match="before any list item"):
    ac.validate_release_fragment(path)


def test_release_rejects_a_date_that_is_not_a_calendar_date(
  tmp_path, monkeypatch
):
  """A mistyped date must not reach the heading or consume fragments."""
  release_dir = tmp_path / "changelog.d"
  release_dir.mkdir()
  changelog = tmp_path / "CHANGELOG.md"
  original = "# Changelog\n\n## [Unreleased]\n"
  changelog.write_text(original, encoding="utf-8")
  monkeypatch.setattr(ac, "RELEASE_FRAGMENTS", release_dir)
  monkeypatch.setattr(ac, "DECISION_FRAGMENTS", tmp_path / "absent")
  monkeypatch.setattr(ac, "RELEASE_CHANGELOG", changelog)
  _write(release_dir, "a.fixed.md", "- A fix.\n")

  for bad in ("not-a-date", "2026-13-01", "2026-02-30", "20260901"):
    assert ac.release("0.13.0", bad, dry_run=False) == 1
    assert changelog.read_text(encoding="utf-8") == original
    assert len(ac._fragment_files(release_dir)) == 1


def test_backdated_decision_fragment_sorts_against_existing_history(
  tmp_path, monkeypatch
):
  """Pending fragments must be ordered against the file, not just each other.

  Prepending would place a backdated fragment above decisions that
  predate it, because sorting the pending set alone says nothing about
  where it belongs in history.
  """
  decision_dir = tmp_path / "decisions.d"
  decision_dir.mkdir()
  decisions = tmp_path / "decisions.md"
  decisions.write_text(
    "# Design Changelog\n\nPreamble.\n\n"
    "## 2026-08-09 - Newest already recorded\n\n- One.\n\n"
    "## 2026-08-01 - Oldest already recorded\n\n- Two.\n",
    encoding="utf-8",
  )
  changelog = tmp_path / "CHANGELOG.md"
  changelog.write_text("# Changelog\n\n## [Unreleased]\n", encoding="utf-8")
  monkeypatch.setattr(ac, "RELEASE_FRAGMENTS", tmp_path / "absent")
  monkeypatch.setattr(ac, "DECISION_FRAGMENTS", decision_dir)
  monkeypatch.setattr(ac, "DECISION_CHANGELOG", decisions)
  monkeypatch.setattr(ac, "RELEASE_CHANGELOG", changelog)
  # Dated BETWEEN the two sections already in the file.
  _write(
    decision_dir,
    "2026-08-05-backdated.md",
    "## 2026-08-05 - Backdated\n\n- Three.\n",
  )

  assert ac.release("0.13.0", "2026-09-01", dry_run=False) == 0

  text = decisions.read_text(encoding="utf-8")
  order = [
    text.index("## 2026-08-09"),
    text.index("## 2026-08-05"),
    text.index("## 2026-08-01"),
  ]
  assert order == sorted(order), "sections must stay newest-first"
  assert text.startswith("# Design Changelog")
  assert "Preamble." in text


def test_decision_fragment_accepts_a_heading_quoted_inside_a_fence(tmp_path):
  """The decisions file ships a Template block that does exactly this.

  Heading detection skips fenced blocks, so a quoted heading stays part of
  the section that owns it instead of splitting the fragment at release.
  """
  path = _write(
    tmp_path,
    "2026-08-07-thing.md",
    "## 2026-08-07 - Thing\n\n- Why.\n\n```\n## 2026-01-15 - Quoted\n```\n",
  )

  assert ac.validate_decision_fragment(path) == "2026-08-07"


def test_decision_fragment_rejects_a_second_unfenced_heading(tmp_path):
  """Two real sections in one file would be split and reordered apart."""
  path = _write(
    tmp_path,
    "2026-08-07-thing.md",
    "## 2026-08-07 - Thing\n\n- Why.\n\n## 2026-01-15 - Another\n\n- Two.\n",
  )

  with pytest.raises(ac.FragmentError, match="section headings"):
    ac.validate_decision_fragment(path)


def test_split_ignores_headings_inside_fences(tmp_path):
  text = (
    "## 2026-08-09 - Real\n\n- One.\n\n```\n## 2026-01-15 - Quoted\n```\n\n"
    "## 2026-08-01 - Also real\n\n- Two.\n"
  )

  sections = ac.split_dated_sections(text)

  assert [date for date, _ in sections] == ["2026-08-09", "2026-08-01"]
  # The quoted heading stays inside the section that owns it.
  assert "## 2026-01-15 - Quoted" in sections[0][1]


def test_release_refuses_unclassified_unreleased_content(tmp_path, monkeypatch):
  """Silently dropping a hand-written entry is the failure to prevent."""
  release_dir = tmp_path / "changelog.d"
  release_dir.mkdir()
  changelog = tmp_path / "CHANGELOG.md"
  original = (
    "# Changelog\n\n## [Unreleased]\n\n- A hand-written entry with no "
    "category.\n\n## [0.12.0] - 2026-08-05\n\n- Old.\n"
  )
  changelog.write_text(original, encoding="utf-8")
  monkeypatch.setattr(ac, "RELEASE_FRAGMENTS", release_dir)
  monkeypatch.setattr(ac, "DECISION_FRAGMENTS", tmp_path / "absent")
  monkeypatch.setattr(ac, "RELEASE_CHANGELOG", changelog)
  _write(release_dir, "a.fixed.md", "- A fix.\n")

  assert ac.release("0.13.0", "2026-09-01", dry_run=False) == 1
  assert changelog.read_text(encoding="utf-8") == original
  assert len(ac._fragment_files(release_dir)) == 1


def test_release_refuses_a_version_already_present(tmp_path, monkeypatch):
  release_dir = tmp_path / "changelog.d"
  release_dir.mkdir()
  changelog = tmp_path / "CHANGELOG.md"
  original = (
    "# Changelog\n\n## [Unreleased]\n\n## [0.13.0] - 2026-09-01\n\n- Old.\n"
  )
  changelog.write_text(original, encoding="utf-8")
  monkeypatch.setattr(ac, "RELEASE_FRAGMENTS", release_dir)
  monkeypatch.setattr(ac, "DECISION_FRAGMENTS", tmp_path / "absent")
  monkeypatch.setattr(ac, "RELEASE_CHANGELOG", changelog)
  _write(release_dir, "a.fixed.md", "- A fix.\n")

  assert ac.release("0.13.0", "2026-09-01", dry_run=False) == 1
  assert changelog.read_text(encoding="utf-8") == original


def test_decisions_only_release_still_folds_the_unreleased_body(
  tmp_path, monkeypatch
):
  """Otherwise release notes stay unreleased while the run reports success."""
  decision_dir = tmp_path / "decisions.d"
  decision_dir.mkdir()
  changelog = tmp_path / "CHANGELOG.md"
  changelog.write_text(
    "# Changelog\n\n## [Unreleased]\n\n### Fixed\n\n- Already written.\n\n"
    "## [0.12.0] - 2026-08-05\n\n- Old.\n",
    encoding="utf-8",
  )
  decisions = tmp_path / "decisions.md"
  decisions.write_text(
    "# Design Changelog\n\nPreamble.\n\n## 2026-08-01 - Earlier\n\n- Old.\n",
    encoding="utf-8",
  )
  monkeypatch.setattr(ac, "RELEASE_FRAGMENTS", tmp_path / "absent")
  monkeypatch.setattr(ac, "DECISION_FRAGMENTS", decision_dir)
  monkeypatch.setattr(ac, "RELEASE_CHANGELOG", changelog)
  monkeypatch.setattr(ac, "DECISION_CHANGELOG", decisions)
  _write(decision_dir, "2026-08-07-why.md", "## 2026-08-07 - Why\n\n- Because.\n")

  assert ac.release("0.13.0", "2026-09-01", dry_run=False) == 0

  text = changelog.read_text(encoding="utf-8")
  assert "## [0.13.0] - 2026-09-01" in text
  assert "- Already written." in text[text.index("## [0.13.0]") :]


def test_release_writes_nothing_when_the_decisions_file_is_unusable(
  tmp_path, monkeypatch
):
  """Rendering both documents must precede writing either.

  The first version wrote CHANGELOG.md before the decisions file was read,
  so a failure there left a half-applied release with the fragments still
  present -- and re-running duplicated the release section.
  """
  release_dir = tmp_path / "changelog.d"
  release_dir.mkdir()
  decision_dir = tmp_path / "decisions.d"
  decision_dir.mkdir()
  changelog = tmp_path / "CHANGELOG.md"
  original = "# Changelog\n\n## [Unreleased]\n\n### Fixed\n\n- Entry.\n"
  changelog.write_text(original, encoding="utf-8")
  decisions = tmp_path / "decisions.md"
  decisions.write_text("# Design Changelog\n\nNo dated sections here.\n",
                       encoding="utf-8")
  monkeypatch.setattr(ac, "RELEASE_FRAGMENTS", release_dir)
  monkeypatch.setattr(ac, "DECISION_FRAGMENTS", decision_dir)
  monkeypatch.setattr(ac, "RELEASE_CHANGELOG", changelog)
  monkeypatch.setattr(ac, "DECISION_CHANGELOG", decisions)
  _write(release_dir, "a.fixed.md", "- A fix.\n")
  _write(decision_dir, "2026-08-07-why.md", "## 2026-08-07 - Why\n\n- Because.\n")

  assert ac.release("0.13.0", "2026-09-01", dry_run=False) == 1

  assert changelog.read_text(encoding="utf-8") == original
  assert len(ac._fragment_files(release_dir)) == 1
  assert len(ac._fragment_files(decision_dir)) == 1


# --- Optimization log ---


def test_optlog_fragment_shares_the_dated_rules(tmp_path):
  path = _write(
    tmp_path, "2026-08-08-planning-index.md",
    "## 2026-08-08 - Planning index\n\n- Area: planning.\n"
  )

  assert ac.validate_optlog_fragment(path) == "2026-08-08"


def test_optlog_fragment_rejects_a_second_heading(tmp_path):
  path = _write(
    tmp_path, "2026-08-08-two.md",
    "## 2026-08-08 - One\n\n- A.\n\n## 2026-08-08 - Two\n\n- B.\n"
  )

  with pytest.raises(ac.FragmentError) as error:
    ac.validate_optlog_fragment(path)
  assert "exactly one" in str(error.value)


def test_check_reports_a_malformed_optlog_fragment(_isolate_optimization_log):
  directory, _ = _isolate_optimization_log
  _write(directory, "no-date-here.md", "## 2026-08-08 - Thing\n\n- A.\n")

  problems = ac.check()

  assert any("no-date-here.md" in problem for problem in problems)


def test_release_assembles_optlog_fragments_newest_first(
  tmp_path, monkeypatch, _isolate_optimization_log
):
  directory, document = _isolate_optimization_log
  release_dir = tmp_path / "changelog.d"
  release_dir.mkdir()
  decision_dir = tmp_path / "decisions.d"
  decision_dir.mkdir()
  changelog = tmp_path / "CHANGELOG.md"
  changelog.write_text("# Changelog\n\n## [Unreleased]\n", encoding="utf-8")
  monkeypatch.setattr(ac, "RELEASE_FRAGMENTS", release_dir)
  monkeypatch.setattr(ac, "DECISION_FRAGMENTS", decision_dir)
  monkeypatch.setattr(ac, "RELEASE_CHANGELOG", changelog)
  # Written out of order on purpose: a branch that sits unmerged across a
  # day produces a fragment older than the newest entry already in the log,
  # and prepending rather than sorting would leave the file misordered.
  _write(directory, "2026-08-05-older.md", "## 2026-08-05 - Older\n\n- A.\n")
  _write(directory, "2026-08-09-newer.md", "## 2026-08-09 - Newer\n\n- B.\n")

  assert ac.release("0.13.0", "2026-09-01", dry_run=False) == 0

  text = document.read_text(encoding="utf-8")
  dates = [date for date, _ in ac.split_dated_sections(text)]
  assert dates == ["2026-08-09", "2026-08-05", "2026-01-01"]
  assert "# Optimization Log" in text
  assert ac._fragment_files(directory) == []


def test_release_leaves_the_optimization_log_alone_without_fragments(
  tmp_path, monkeypatch, _isolate_optimization_log
):
  _, document = _isolate_optimization_log
  original = document.read_text(encoding="utf-8")
  release_dir = tmp_path / "changelog.d"
  release_dir.mkdir()
  changelog = tmp_path / "CHANGELOG.md"
  changelog.write_text("# Changelog\n\n## [Unreleased]\n", encoding="utf-8")
  monkeypatch.setattr(ac, "RELEASE_FRAGMENTS", release_dir)
  monkeypatch.setattr(ac, "DECISION_FRAGMENTS", tmp_path / "absent")
  monkeypatch.setattr(ac, "RELEASE_CHANGELOG", changelog)
  _write(release_dir, "a.fixed.md", "- A fix.\n")

  assert ac.release("0.13.0", "2026-09-01", dry_run=False) == 0

  assert document.read_text(encoding="utf-8") == original


def test_dated_fragment_rejects_an_impossible_calendar_date(tmp_path):
  # The shape matches and the heading agrees with the name, so only a
  # calendar check catches it -- and release sorts on these strings.
  path = _write(
    tmp_path, "2026-02-30-a-decision.md", "## 2026-02-30 - A decision\n"
  )

  with pytest.raises(ac.FragmentError) as error:
    ac.validate_dated_fragment(path)
  assert "not a calendar date" in str(error.value)


def test_preview_shows_the_optimization_log(
  capsys, tmp_path, monkeypatch, _isolate_optimization_log
):
  directory, _ = _isolate_optimization_log
  monkeypatch.setattr(ac, "RELEASE_FRAGMENTS", tmp_path / "none")
  monkeypatch.setattr(ac, "DECISION_FRAGMENTS", tmp_path / "none")
  _write(directory, "2026-08-08-thing.md", "## 2026-08-08 - Thing\n\n- A.\n")

  assert ac.main(["--preview"]) == 0

  out = capsys.readouterr().out
  assert "optimization-log.md" in out
  assert "2026-08-08 - Thing" in out


def test_check_counts_optimization_log_fragments(
  capsys, tmp_path, monkeypatch, _isolate_optimization_log
):
  directory, _ = _isolate_optimization_log
  monkeypatch.setattr(ac, "RELEASE_FRAGMENTS", tmp_path / "none")
  monkeypatch.setattr(ac, "DECISION_FRAGMENTS", tmp_path / "none")
  _write(directory, "2026-08-08-thing.md", "## 2026-08-08 - Thing\n\n- A.\n")

  assert ac.main(["--check"]) == 0

  assert "1 fragments valid" in capsys.readouterr().out
