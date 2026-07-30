#!/usr/bin/env python3
"""Benchmark coverage gap-finder (redesign section 4.5, advisory only).

Joins llvm-cov export summaries from the coverage-instrumented bench
binaries against the repository's public header tree and reports which
headers no benchmark executes. Two kinds of evidence are distinguished:

- ``zero-covered-regions``: the header has an emitted coverage mapping
  and none of its regions ran.
- ``absent-from-export``: the header produced no coverage mapping at
  all (never included, or only uninstantiated templates and
  declarations), which llvm-cov cannot report as a file row.

Acknowledged gaps live in an exact-header manifest with reasons; they
render in their own section so new gaps stay loud while known ones stay
visible. The tool never fails on gap content — only on infrastructure
errors (missing or malformed inputs).

``ctest_objects`` supports the sibling test-suite coverage report: it
extracts the deduplicated set of instrumented test executables from
``ctest --show-only=json-v1`` output, because llvm-cov reads coverage
mappings from binaries and must receive every one via ``-object``.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

EXPORT_TYPE = "llvm.coverage.json.export"


class CoverageError(RuntimeError):
  """Raised for infrastructure failures (never for gap content)."""


def _load_json(path: Path) -> dict[str, Any]:
  try:
    with path.open(encoding="utf-8") as handle:
      return json.load(handle)
  except OSError as error:
    raise CoverageError(f"cannot read {path}: {error}") from error
  except json.JSONDecodeError as error:
    raise CoverageError(f"malformed JSON in {path}: {error}") from error


def _export_file_summaries(
  path: Path, marker: str
) -> dict[str, dict[str, int]]:
  """Map header-relative paths to region summaries for one export."""
  payload = _load_json(path)
  if payload.get("type") != EXPORT_TYPE:
    raise CoverageError(
      f"{path} is not an llvm-cov export (type="
      f"{payload.get('type')!r})"
    )
  summaries: dict[str, dict[str, int]] = {}
  for entry in payload.get("data", []):
    for file_row in entry.get("files", []):
      filename = file_row.get("filename", "")
      position = filename.rfind(marker)
      if position < 0:
        continue
      relative = filename[position + len(marker):]
      regions = file_row.get("summary", {}).get("regions", {})
      count = int(regions.get("count", 0))
      covered = int(regions.get("covered", 0))
      existing = summaries.get(relative)
      if existing is None or covered > existing["covered"]:
        summaries[relative] = {"count": count, "covered": covered}
  return summaries


def _repository_headers(include_root: Path) -> list[str]:
  if not include_root.is_dir():
    raise CoverageError(f"include root {include_root} does not exist")
  return sorted(
    str(path.relative_to(include_root))
    for path in include_root.rglob("*.h")
  )


def _subsystem(header: str) -> str:
  if "/" not in header:
    return "(top-level)"
  return header.split("/", 1)[0]


def analyze(
  export_paths: list[Path],
  include_root: Path,
  *,
  known_gaps: list[dict[str, str]],
) -> dict[str, Any]:
  """Join export summaries against the header tree; classify gaps."""
  headers = _repository_headers(include_root)
  marker = "include/tess/"
  merged: dict[str, dict[str, int]] = {}
  for path in export_paths:
    for relative, summary in _export_file_summaries(
      Path(path), marker
    ).items():
      existing = merged.get(relative)
      if existing is None or summary["covered"] > existing["covered"]:
        merged[relative] = summary

  known_reasons = {
    entry["header"]: entry["reason"] for entry in known_gaps
  }
  rows = []
  for header in headers:
    summary = merged.get(header)
    if summary is None:
      executed = False
      reason = "absent-from-export"
      coverage = 0.0
    else:
      executed = summary["covered"] > 0
      reason = None if executed else "zero-covered-regions"
      coverage = (
        summary["covered"] / summary["count"] if summary["count"] else 0.0
      )
    rows.append(
      {
        "header": header,
        "subsystem": _subsystem(header),
        "executed": executed,
        "region_coverage": coverage,
        "reason": reason,
      }
    )

  gaps = []
  known = []
  stale_known = []
  for row in rows:
    if row["executed"]:
      if row["header"] in known_reasons:
        stale_known.append(row["header"])
      continue
    gap = {
      "header": row["header"],
      "subsystem": row["subsystem"],
      "reason": row["reason"],
    }
    acknowledged = known_reasons.get(row["header"])
    if acknowledged is None:
      gaps.append(gap)
    else:
      known.append({**gap, "known_reason": acknowledged})

  subsystem_rows = []
  for name in sorted({row["subsystem"] for row in rows}):
    members = [row for row in rows if row["subsystem"] == name]
    subsystem_rows.append(
      {
        "subsystem": name,
        "headers": len(members),
        "gaps": sum(1 for row in members if not row["executed"]),
      }
    )

  return {
    "headers": rows,
    "subsystems": subsystem_rows,
    "gaps": gaps,
    "known_gaps": known,
    "stale_known_gaps": stale_known,
  }


def ctest_objects(ctest_json: Path, build_dir: Path) -> list[str]:
  """Instrumented test executables from ctest --show-only=json-v1."""
  payload = _load_json(Path(ctest_json))
  build_root = Path(build_dir).resolve()
  objects = set()
  for test in payload.get("tests", []):
    command = test.get("command") or []
    if not command:
      continue
    executable = Path(command[0])
    try:
      executable.resolve().relative_to(build_root)
    except ValueError:
      continue
    if executable.is_file():
      objects.add(str(executable))
  if not objects:
    raise CoverageError(
      f"no instrumented test executables found under {build_dir}"
    )
  return sorted(objects)


def render_report(result: dict[str, Any]) -> str:
  """Markdown for the step summary. Advisory framing throughout."""
  lines = ["## Benchmark coverage gaps (advisory)", ""]
  lines.append("| Subsystem | Headers | Gaps |")
  lines.append("| --- | ---: | ---: |")
  for row in result["subsystems"]:
    lines.append(
      f"| {row['subsystem']} | {row['headers']} | {row['gaps']} |"
    )
  lines.append("")

  gaps = result["gaps"]
  if gaps:
    lines.append("### New benchmark coverage gaps")
    lines.append("")
    lines.append("| Header | Evidence |")
    lines.append("| --- | --- |")
    for gap in gaps:
      lines.append(f"| `{gap['header']}` | {gap['reason']} |")
  else:
    lines.append("### No new benchmark coverage gaps")
  lines.append("")

  known = result["known_gaps"]
  if known:
    lines.append("### Known gaps (acknowledged in the manifest)")
    lines.append("")
    lines.append("| Header | Evidence | Reason |")
    lines.append("| --- | --- | --- |")
    for gap in known:
      lines.append(
        f"| `{gap['header']}` | {gap['reason']} |"
        f" {gap['known_reason']} |"
      )
    lines.append("")

  stale = result["stale_known_gaps"]
  if stale:
    lines.append("### Stale known-gap entries (now executed — remove)")
    lines.append("")
    for header in stale:
      lines.append(f"- `{header}`")
    lines.append("")

  lines.append(
    "_This report is advisory and never gates; it finds subsystems no"
    " benchmark executes (redesign section 4.5)._"
  )
  return "\n".join(lines) + "\n"


def _load_known_gaps(path: Path | None) -> list[dict[str, str]]:
  if path is None:
    return []
  payload = _load_json(path)
  entries = payload.get("known_gaps", [])
  for entry in entries:
    if "header" not in entry or "reason" not in entry:
      raise CoverageError(
        f"{path}: every known_gaps entry needs header and reason"
      )
  return entries


def main(argv: list[str] | None = None) -> int:
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument(
    "--export",
    action="append",
    required=True,
    dest="exports",
    type=Path,
    help="llvm-cov export JSON (repeat per bench binary)",
  )
  parser.add_argument("--include-root", required=True, type=Path)
  parser.add_argument("--known-gaps", type=Path)
  parser.add_argument("--out-markdown", type=Path)
  parser.add_argument("--out-json", type=Path)
  args = parser.parse_args(argv)

  try:
    result = analyze(
      args.exports,
      args.include_root,
      known_gaps=_load_known_gaps(args.known_gaps),
    )
  except CoverageError as error:
    print(f"error: {error}", file=sys.stderr)
    return 1

  report = render_report(result)
  if args.out_markdown is not None:
    args.out_markdown.write_text(report, encoding="utf-8")
  if args.out_json is not None:
    args.out_json.write_text(
      json.dumps(result, indent=2) + "\n", encoding="utf-8"
    )
  print(report)
  return 0


def objects_main(argv: list[str] | None = None) -> int:
  """CLI for ctest_objects: one executable path per line on stdout."""
  parser = argparse.ArgumentParser(
    description="List instrumented ctest executables"
  )
  parser.add_argument("--ctest-json", required=True, type=Path)
  parser.add_argument("--build-dir", required=True, type=Path)
  args = parser.parse_args(argv)

  try:
    objects = ctest_objects(args.ctest_json, args.build_dir)
  except CoverageError as error:
    print(f"error: {error}", file=sys.stderr)
    return 1
  for executable in objects:
    print(executable)
  return 0


if __name__ == "__main__":
  if len(sys.argv) > 1 and sys.argv[1] == "ctest-objects":
    sys.exit(objects_main(sys.argv[2:]))
  sys.exit(main())
