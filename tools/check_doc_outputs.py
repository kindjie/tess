#!/usr/bin/env python3
"""Keep documented example output synchronized with compiled binaries."""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
OUTPUT_BLOCK_RE = re.compile(
  r"^<!-- tess-output: (?P<name>[a-z][a-z0-9_-]*) "
  r"source=(?P<source>[^ ]+) -->\n"
  r"```text\n"
  r"(?P<body>.*?)\n"
  r"```\n"
  r"<!-- /tess-output -->$",
  re.MULTILINE | re.DOTALL,
)


@dataclass(frozen=True)
class OutputBlock:
  """One documented program-output fence backed by a compiled source."""

  name: str
  source: str
  body: str
  document: str


def extract_output_blocks(text: str, label: str) -> list[OutputBlock]:
  """Extract source-backed output fences from one Markdown document."""
  blocks: list[OutputBlock] = []
  names: set[str] = set()
  for match in OUTPUT_BLOCK_RE.finditer(text):
    name = match.group("name")
    if name in names:
      raise ValueError(f"{label}: duplicate output block '{name}'")
    names.add(name)
    blocks.append(
      OutputBlock(
        name=name,
        source=match.group("source"),
        body=match.group("body"),
        document=label,
      )
    )
  return blocks


def markdown_paths(repo_root: Path) -> list[Path]:
  """Return tracked documentation locations that may contain output blocks."""
  paths = [repo_root / "README.md", repo_root / "CONTRIBUTING.md"]
  paths.extend(sorted((repo_root / "docs").rglob("*.md")))
  return [path for path in paths if path.is_file()]


def collect_output_blocks(repo_root: Path) -> tuple[list[OutputBlock], list[str]]:
  """Return all documented output blocks and extraction failures."""
  blocks: list[OutputBlock] = []
  failures: list[str] = []
  for path in markdown_paths(repo_root):
    relative = path.relative_to(repo_root).as_posix()
    try:
      blocks.extend(
        extract_output_blocks(path.read_text(encoding="utf-8"), relative)
      )
    except ValueError as error:
      failures.append(str(error))
  return blocks, failures


def check_repository(repo_root: Path, binaries: dict[str, Path]) -> list[str]:
  """Return documented-output failures given source-to-binary mappings."""
  blocks, failures = collect_output_blocks(repo_root)

  used_sources = {block.source for block in blocks}
  for source in sorted(set(binaries) - used_sources):
    failures.append(
      f"binary mapping for {source} matches no documented output block"
    )

  outputs: dict[str, str] = {}
  for block in sorted(blocks, key=lambda item: (item.document, item.name)):
    binary = binaries.get(block.source)
    if binary is None:
      failures.append(
        f"{block.document}: output block '{block.name}' has no binary "
        f"mapping for {block.source}"
      )
      continue
    if block.source not in outputs:
      if not binary.is_file():
        failures.append(f"binary does not exist: {binary.as_posix()}")
        continue
      completed = subprocess.run(
        [str(binary)], capture_output=True, text=True, check=False
      )
      if completed.returncode != 0:
        failures.append(
          f"{binary.as_posix()} exited with status {completed.returncode}"
        )
        continue
      outputs[block.source] = completed.stdout
    if outputs[block.source].rstrip("\n") != block.body.rstrip("\n"):
      failures.append(
        f"{block.document}: output block '{block.name}' differs from "
        f"{block.source} output"
      )
  return failures


def parse_mappings(pairs: list[str]) -> dict[str, Path]:
  """Parse ``source=binary`` command-line mappings."""
  binaries: dict[str, Path] = {}
  for pair in pairs:
    source, separator, binary = pair.partition("=")
    if not separator or not source or not binary:
      raise ValueError(f"expected source=binary mapping, got '{pair}'")
    binaries[source] = Path(binary)
  return binaries


def main() -> int:
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument(
    "--repo-root", type=Path, default=REPO_ROOT, help="repository root"
  )
  parser.add_argument(
    "mappings",
    nargs="+",
    help="source=binary mapping, e.g. examples/quickstart.cc=build/dev/"
    "examples/tess_quickstart",
  )
  args = parser.parse_args()

  try:
    binaries = parse_mappings(args.mappings)
  except ValueError as error:
    print(f"error: {error}", file=sys.stderr)
    return 2

  failures = check_repository(args.repo_root.resolve(), binaries)
  if failures:
    for failure in failures:
      print(f"error: {failure}", file=sys.stderr)
    return 1
  print("Documented example output matches compiled binaries.")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
