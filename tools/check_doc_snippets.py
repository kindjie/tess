#!/usr/bin/env python3
"""Keep Markdown code excerpts synchronized with compiled source regions."""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
SOURCE_MARKER_RE = re.compile(
    r"^\s*//\s*\[([a-z][a-z0-9_-]*)\]\s*$"
)
MARKDOWN_SNIPPET_RE = re.compile(
    r"^<!-- tess-snippet: (?P<name>[a-z][a-z0-9_-]*) "
    r"source=(?P<source>[^ ]+) -->\n"
    r"```(?P<language>[a-zA-Z0-9_+-]+)\n"
    r"(?P<body>.*?)\n"
    r"```\n"
    r"<!-- /tess-snippet -->$",
    re.MULTILINE | re.DOTALL,
)


@dataclass(frozen=True)
class MarkdownSnippet:
    """One source-backed fenced block in a Markdown document."""

    name: str
    source: Path
    language: str
    body: str
    start: int
    end: int


def extract_source_regions(text: str, label: str) -> dict[str, str]:
    """Extract paired ``// [name]`` source regions."""
    regions: dict[str, str] = {}
    active_name: str | None = None
    active_lines: list[str] = []

    for line_number, line in enumerate(text.splitlines(), start=1):
        marker = SOURCE_MARKER_RE.match(line)
        if marker is None:
            if active_name is not None:
                active_lines.append(line)
            continue

        name = marker.group(1)
        if active_name is None:
            if name in regions:
                raise ValueError(
                    f"{label}:{line_number}: duplicate snippet '{name}'"
                )
            active_name = name
            active_lines = []
            continue

        if name != active_name:
            raise ValueError(
                f"{label}:{line_number}: snippet '{active_name}' closed by "
                f"'{name}'"
            )
        regions[name] = "\n".join(active_lines).strip("\n")
        active_name = None
        active_lines = []

    if active_name is not None:
        raise ValueError(f"{label}: unclosed snippet '{active_name}'")
    return regions


def extract_markdown_snippets(
    text: str, label: str
) -> list[MarkdownSnippet]:
    """Extract source-backed fenced blocks from one Markdown document."""
    snippets: list[MarkdownSnippet] = []
    names: set[str] = set()
    for match in MARKDOWN_SNIPPET_RE.finditer(text):
        name = match.group("name")
        if name in names:
            raise ValueError(f"{label}: duplicate Markdown snippet '{name}'")
        names.add(name)
        snippets.append(
            MarkdownSnippet(
                name=name,
                source=Path(match.group("source")),
                language=match.group("language"),
                body=match.group("body"),
                start=match.start("body"),
                end=match.end("body"),
            )
        )
    return snippets


def _source_body(repo_root: Path, snippet: MarkdownSnippet) -> str:
    source_path = repo_root / snippet.source
    if not source_path.is_file():
        raise ValueError(f"source does not exist: {snippet.source.as_posix()}")
    regions = extract_source_regions(
        source_path.read_text(encoding="utf-8"), snippet.source.as_posix()
    )
    if snippet.name not in regions:
        raise ValueError(
            f"{snippet.source.as_posix()}: missing snippet "
            f"'{snippet.name}'"
        )
    return regions[snippet.name]


def check_markdown(repo_root: Path, path: Path) -> list[str]:
    """Return synchronization failures for one Markdown document."""
    relative = path.relative_to(repo_root).as_posix()
    try:
        snippets = extract_markdown_snippets(
            path.read_text(encoding="utf-8"), relative
        )
    except ValueError as error:
        return [str(error)]

    failures: list[str] = []
    for snippet in snippets:
        try:
            source_body = _source_body(repo_root, snippet)
        except ValueError as error:
            failures.append(f"{relative}: {error}")
            continue
        if snippet.body != source_body:
            failures.append(
                f"{relative}: snippet '{snippet.name}' differs from "
                f"{snippet.source.as_posix()}"
            )
    return failures


def update_markdown(repo_root: Path, path: Path) -> bool:
    """Replace drifted fenced bodies in one Markdown document."""
    text = path.read_text(encoding="utf-8")
    relative = path.relative_to(repo_root).as_posix()
    snippets = extract_markdown_snippets(text, relative)
    updated = text
    changed = False
    for snippet in reversed(snippets):
        source_body = _source_body(repo_root, snippet)
        if snippet.body == source_body:
            continue
        updated = (
            updated[: snippet.start] + source_body + updated[snippet.end :]
        )
        changed = True
    if changed:
        path.write_text(updated, encoding="utf-8")
    return changed


def markdown_paths(repo_root: Path) -> list[Path]:
    """Return tracked documentation locations that may contain snippets."""
    paths = [repo_root / "README.md", repo_root / "CONTRIBUTING.md"]
    paths.extend(sorted((repo_root / "docs").rglob("*.md")))
    return [path for path in paths if path.is_file()]


def check_repository(repo_root: Path) -> list[str]:
    """Return all snippet failures in repository documentation."""
    failures: list[str] = []
    for path in markdown_paths(repo_root):
        failures.extend(check_markdown(repo_root, path))
    return failures


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--repo-root", type=Path, default=REPO_ROOT, help="repository root"
    )
    parser.add_argument(
        "--update", action="store_true", help="refresh snippets in place"
    )
    args = parser.parse_args()
    repo_root = args.repo_root.resolve()

    if args.update:
        changed = [
            path.relative_to(repo_root).as_posix()
            for path in markdown_paths(repo_root)
            if update_markdown(repo_root, path)
        ]
        for path in changed:
            print(f"updated {path}")
        return 0

    failures = check_repository(repo_root)
    if failures:
        for failure in failures:
            print(f"error: {failure}", file=sys.stderr)
        return 1
    print("Documentation snippets match compiled sources.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
