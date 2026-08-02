#!/usr/bin/env python3
"""Lay out per-main benchmark baselines for the long-retention data branch.

Redesign section 7 requires per-main timing and counter artifacts to
outlive GitHub's 30-day artifact retention so a bisection weeks later
still has data. Section 12 settles where they live: an orphan data
branch, because it survives retention policy without adding an external
dependency and the payload is small JSON.

This module only decides the layout and the index; the workflow does the
git plumbing. Keeping the decision pure is what makes it testable --
the failure this guards against is a publish step that silently writes
nothing and reports success, which is indistinguishable from a healthy
run until someone needs the history.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any

# fullmatch, not `$`: in Python `$` also matches just before a trailing
# newline, so "<sha>\n" -- what an unstripped `git rev-parse` produces --
# would validate and then put a newline inside a destination path.
COMMIT_RE = re.compile(r"[0-9a-f]{40}")
TIMESTAMP_RE = re.compile(r"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z")


class ToolError(RuntimeError):
  """An input failure that must fail the publish step."""


@dataclass(frozen=True)
class Publication:
  """The files to write and the index entry describing them."""

  files: dict[str, str]
  index_entry: dict[str, Any]


def destination(commit: str, timestamp: str, name: str) -> str:
  """Return the data-branch path for one baseline file.

  Sharded by year and month so a directory listing stays usable after
  years of per-main runs, and keyed by commit so a re-run of the same
  commit overwrites rather than accumulating duplicates.
  """
  year, month = timestamp[:4], timestamp[5:7]
  return f"baselines/{year}/{month}/{commit}/{name}"


def plan_publication(
  sources: list[Path],
  commit: str,
  timestamp: str,
  run_id: str,
) -> Publication:
  """Decide what to write for one main-branch benchmark run."""
  if not COMMIT_RE.fullmatch(commit):
    raise ToolError(f"commit must be a full lowercase sha, got {commit!r}")
  if not TIMESTAMP_RE.fullmatch(timestamp):
    raise ToolError(
      f"timestamp must be ISO-8601 UTC (YYYY-MM-DDTHH:MM:SSZ), "
      f"got {timestamp!r}"
    )
  # An empty publish is the silent-success failure this tool exists to
  # prevent: the step would report a healthy run while the history
  # quietly stopped growing, and nobody notices until a bisection needs
  # data that was never stored.
  if not sources:
    raise ToolError("no baseline files to publish")

  files: dict[str, str] = {}
  benchmarks = 0
  for source in sources:
    try:
      payload = source.read_text(encoding="utf-8")
    except OSError as error:
      raise ToolError(f"{source}: cannot read: {error}") from error
    try:
      parsed = json.loads(payload)
    except json.JSONDecodeError as error:
      raise ToolError(f"{source}: malformed JSON: {error}") from error
    if not isinstance(parsed, dict):
      raise ToolError(f"{source}: must contain a JSON object")
    benchmarks += len(parsed.get("benchmarks", []))
    files[destination(commit, timestamp, source.name)] = payload

  index_entry = {
    "commit": commit,
    "timestamp": timestamp,
    "run_id": run_id,
    "files": sorted(files),
    "benchmark_count": benchmarks,
  }
  return Publication(files=files, index_entry=index_entry)


def merge_index(existing: str | None, entry: dict[str, Any]) -> str:
  """Add one entry to the index, newest first, replacing a re-run.

  Keyed by commit so re-running a commit corrects its row instead of
  leaving two rows that disagree.
  """
  entries: list[dict[str, Any]] = []
  if existing:
    try:
      loaded = json.loads(existing)
    except json.JSONDecodeError as error:
      raise ToolError(f"index is malformed JSON: {error}") from error
    if not isinstance(loaded, dict) or not isinstance(loaded.get("runs"), list):
      raise ToolError("index must be an object with a 'runs' array")
    entries = [
      run
      for run in loaded["runs"]
      if isinstance(run, dict) and run.get("commit") != entry["commit"]
    ]
  entries.insert(0, entry)
  entries.sort(key=lambda run: run.get("timestamp", ""), reverse=True)
  return json.dumps({"version": 1, "runs": entries}, indent=2) + "\n"


def write_publication(
  publication: Publication, out_dir: Path, index_name: str = "index.json"
) -> int:
  """Write the planned files and refresh the index under `out_dir`."""
  for relative, payload in publication.files.items():
    target = out_dir / relative
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(payload, encoding="utf-8")

  index_path = out_dir / index_name
  existing = (
    index_path.read_text(encoding="utf-8") if index_path.exists() else None
  )
  index_path.write_text(
    merge_index(existing, publication.index_entry), encoding="utf-8"
  )
  return len(publication.files)


def parse_args(argv: list[str]) -> argparse.Namespace:
  """Parse the publish step's command line."""
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("--out-dir", required=True, type=Path)
  parser.add_argument("--commit", required=True)
  parser.add_argument("--timestamp", required=True)
  parser.add_argument("--run-id", default="")
  parser.add_argument("sources", nargs="*", type=Path)
  return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
  """Write one run's baselines into a checked-out data branch."""
  args = parse_args(sys.argv[1:] if argv is None else argv)
  try:
    publication = plan_publication(
      list(args.sources), args.commit, args.timestamp, args.run_id
    )
    written = write_publication(publication, args.out_dir)
  except ToolError as error:
    print(f"error: {error}", file=sys.stderr)
    return 1
  print(f"published {written} baseline file(s) for {args.commit[:12]}")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
