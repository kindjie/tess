#!/usr/bin/env python3
"""Keep development and latest-release documentation unambiguous."""

from __future__ import annotations

import re
import sys
from dataclasses import dataclass
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
SOURCE_VERSION_RE = re.compile(r"set\(TESS_VERSION\s+(\d+)\.(\d+)\.(\d+)\)")
LATEST_RELEASE_RE = re.compile(
  r"latest release is `v(\d+)\.(\d+)\.(\d+)`", re.IGNORECASE
)
CHANGELOG_RELEASE_RE = re.compile(r"^## \[(\d+)\.(\d+)\.(\d+)\]", re.MULTILINE)
FIND_PACKAGE_RE = re.compile(r"find_package\(tess\s+([0-9][0-9.]*)")
FETCH_TAG_TOKEN_RE = re.compile(r"\bGIT_TAG\b")
FETCH_TAG_VALUE_RE = re.compile(r"\bGIT_TAG\b[ \t]+([^\s`]+)")
PRE_1_0_RE = re.compile(r"pre-1\.0", re.IGNORECASE)

# Trees recording historical intent, and the one guide whose subject is
# what applications built before 1.0 must do. Everywhere else, "pre-1.0"
# describes the present and stops being true at the 1.0 release.
PRE_1_0_EXEMPT_PARTS = frozenset({"decisions", "planning", "tdd"})
PRE_1_0_EXEMPT_PATHS = frozenset({"docs/upgrade-1.0.md"})
PRE_1_0_SCANNED = ("docs/**/*.md", "include/**/*.h", "README.md")


@dataclass(frozen=True, order=True)
class Version:
  """A numeric semantic version used by the documentation checks."""

  major: int
  minor: int
  patch: int

  @classmethod
  def from_match(cls, match: re.Match[str]) -> "Version":
    return cls(*(int(part) for part in match.groups()))

  def __str__(self) -> str:
    return f"{self.major}.{self.minor}.{self.patch}"

  @property
  def requirement(self) -> str:
    return f"{self.major}.{self.minor}"


def _read(path: Path) -> str:
  return path.read_text(encoding="utf-8")


def _required_version(
  text: str, pattern: re.Pattern[str], label: str
) -> tuple[Version | None, list[str]]:
  match = pattern.search(text)
  if match is None:
    return None, [f"{label}: version declaration not found"]
  return Version.from_match(match), []


def _stale_pre_1_0_claims(repo_root: Path) -> list[str]:
  """Report maintained text still calling the current state pre-1.0.

  These are not version banners the release process rewrites; they sit
  in shipped headers and architecture prose, where they simply stop
  being true once 1.0 is out and nothing else would catch them.
  """
  failures: list[str] = []
  seen: set[Path] = set()
  for pattern in PRE_1_0_SCANNED:
    for path in sorted(repo_root.glob(pattern)):
      relative = path.relative_to(repo_root)
      if path in seen or PRE_1_0_EXEMPT_PARTS.intersection(relative.parts):
        continue
      if relative.as_posix() in PRE_1_0_EXEMPT_PATHS:
        continue
      seen.add(path)
      for number, line in enumerate(_read(path).splitlines(), start=1):
        if PRE_1_0_RE.search(line):
          failures.append(
            f"{relative.as_posix()}:{number}: 1.x is released; say what "
            "the scope is rather than calling it pre-1.0"
          )
  return failures


def check_repository(repo_root: Path = REPO_ROOT) -> list[str]:
  """Return development/release version documentation failures."""
  source_text = _read(repo_root / "cmake" / "tess-version.cmake")
  readme = _read(repo_root / "README.md")
  changelog = _read(repo_root / "CHANGELOG.md")
  index = _read(repo_root / "docs" / "index.md")
  packaging = _read(repo_root / "docs" / "packaging.md")

  source, failures = _required_version(
    source_text, SOURCE_VERSION_RE, "cmake/tess-version.cmake"
  )
  release, release_failures = _required_version(
    readme, LATEST_RELEASE_RE, "README.md latest release"
  )
  failures.extend(release_failures)
  if source is None or release is None:
    return failures

  if release.major >= 1:
    failures.extend(_stale_pre_1_0_claims(repo_root))

  changelog_versions = [
    Version.from_match(match)
    for match in CHANGELOG_RELEASE_RE.finditer(changelog)
  ]
  if not changelog_versions or max(changelog_versions) != release:
    failures.append(
      "CHANGELOG.md: newest released version must match README.md "
      f"latest release v{release}"
    )
  if source < release:
    failures.append(
      "cmake/tess-version.cmake: source version must not be older "
      f"than latest release v{release}"
    )
  elif source == release:
    release_phrase = f"`v{source}` release"
    unreleased_phrase = f"unreleased `v{source}`"
    for label, text in (("README.md", readme), ("docs/index.md", index)):
      if unreleased_phrase in text.lower():
        failures.append(
          f"{label}: release checkout still describes v{source} as "
          "unreleased"
        )
      elif release_phrase not in text:
        failures.append(
          f"{label}: identify v{source} as the current release"
        )
  else:
    development_phrase = f"`v{source}` development"
    if (
      development_phrase not in readme
      or "unreleased" not in readme.lower()
    ):
      failures.append(
        f"README.md: identify v{source} as the unreleased development API"
      )
    if development_phrase not in index or "unreleased" not in index.lower():
      failures.append(
        f"docs/index.md: identify v{source} as the unreleased development API"
      )

  # The README may omit find_package entirely (installation lives in
  # docs/packaging.md); any occurrence it does carry must match the source
  # major.minor exactly, with no patch component.
  expected_requirement = source.requirement
  for label, packages, required in (
    ("README.md", FIND_PACKAGE_RE.findall(readme), False),
    ("docs/packaging.md", FIND_PACKAGE_RE.findall(packaging), True),
  ):
    if required and not packages:
      failures.append(
        f"{label}: current-checkout find_package must request "
        f"{expected_requirement}"
      )
    for actual_requirement in sorted(set(packages) - {expected_requirement}):
      failures.append(
        f"{label}: current-checkout find_package must request "
        f"{expected_requirement}, not {actual_requirement}"
      )

  # The README may keep installation at the decision level and omit a
  # FetchContent example. The packaging guide is the required command-level
  # authority; any tag shown in either document must identify the release.
  expected_tag = f"v{release}"
  for label, text, required in (
    ("README.md", readme, False),
    ("docs/packaging.md", packaging, True),
  ):
    tag_values = FETCH_TAG_VALUE_RE.findall(text)
    tags = set(tag_values)
    malformed = len(FETCH_TAG_TOKEN_RE.findall(text)) != len(tag_values)
    if (required and not tags) or malformed or tags - {expected_tag}:
      rendered = ", ".join(sorted(tags)) or "none"
      failures.append(
        f"{label}: release FetchContent tag must be v{release}; "
        f"found {rendered}"
      )

  return failures


def main() -> int:
  failures = check_repository()
  if failures:
    for failure in failures:
      print(f"error: {failure}", file=sys.stderr)
    return 1
  print("Development and release documentation versions are consistent.")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
