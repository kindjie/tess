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
FIND_PACKAGE_RE = re.compile(r"find_package\(tess\s+(\d+)\.(\d+)")
FETCH_TAG_RE = re.compile(r"GIT_TAG\s+v(\d+)\.(\d+)\.(\d+)")


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
  # docs/packaging.md); any occurrence it does carry must match the source.
  readme_packages = FIND_PACKAGE_RE.findall(readme)
  expected_requirement = source.requirement
  mismatched = sorted(
    {".".join(parts) for parts in readme_packages} - {expected_requirement}
  )
  for actual_requirement in mismatched:
    failures.append(
      "README.md: current-checkout find_package must request "
      f"{expected_requirement}, not {actual_requirement}"
    )

  packaging_packages = {
    ".".join(parts) for parts in FIND_PACKAGE_RE.findall(packaging)
  }
  if expected_requirement not in packaging_packages:
    failures.append(
      "docs/packaging.md: current-checkout find_package must request "
      f"{expected_requirement}"
    )

  expected_tag = str(release)
  for label, text in (("README.md", readme), ("docs/packaging.md", packaging)):
    tags = {
      str(Version.from_match(match)) for match in FETCH_TAG_RE.finditer(text)
    }
    if tags != {expected_tag}:
      rendered = ", ".join(sorted(f"v{tag}" for tag in tags)) or "none"
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
