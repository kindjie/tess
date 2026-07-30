#!/usr/bin/env python3
"""Detect sustained benchmark shifts across trailing main artifacts.

The redesign section 4.2 alerting leg, deliberately conservative:
artifacts are stratified by runner fingerprint (a fleet or image
migration is a series break, not a regression, and a previously seen
fingerprint resumes its own series), and a benchmark flags only when
the newest three same-stratum artifacts each exceed the stratum's
baseline median by both a relative and an absolute floor:

    candidate_median > baseline_median * (1 + relative_floor)
    AND candidate_median - baseline_median > absolute_floor_ns

The detector never gates: it writes a report for a rolling issue and
exits zero unless its inputs are unreadable.
"""

from __future__ import annotations

import argparse
import json
import statistics
from pathlib import Path
from typing import Any


CANDIDATES = 3
MIN_BASELINE = 8
MAX_BASELINE = 30

UNIT_TO_NS = {"ns": 1.0, "us": 1_000.0, "ms": 1_000_000.0, "s": 1e9}


class ToolError(RuntimeError):
  """Unreadable input that must fail the run."""


def load_artifact(
    directory: Path, *, allow_legacy: bool = False
) -> dict[str, Any] | None:
  """Load one artifact directory into medians plus metadata.

  Returns None for artifacts that cannot participate: missing or
  unusable metadata, or a non-push/non-main run.
  """
  metadata_path = directory / "metadata.json"
  try:
    metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
  except (OSError, json.JSONDecodeError):
    return None
  fingerprint = metadata.get("fingerprint") or {}
  key = fingerprint.get("key") if fingerprint.get("usable") else None
  if key is None:
    if not allow_legacy:
      return None
    # Backtesting only: artifacts that predate fingerprinting share one
    # legacy stratum. CI never passes --allow-legacy.
    key = "legacy"
  if metadata.get("event_name") not in (None, "push"):
    # Older artifacts predate the event field; new ones must be pushes.
    return None
  ref = metadata.get("ref")
  if ref not in (None, "main", "refs/heads/main"):
    return None

  medians: dict[str, float] = {}
  for result in sorted(directory.glob("*.json")):
    if result.name == "metadata.json":
      continue
    try:
      data = json.loads(result.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
      continue
    samples: dict[str, list[float]] = {}
    for row in data.get("benchmarks", []):
      if row.get("run_type") == "aggregate":
        continue
      unit = UNIT_TO_NS.get(row.get("time_unit", "ns"))
      value = row.get("cpu_time")
      name = row.get("name")
      if unit is None or value is None or name is None:
        continue
      samples.setdefault(str(name), []).append(float(value) * unit)
    for name, values in samples.items():
      medians[name] = statistics.median(values)
  return {
      "run_id": int(metadata.get("run_id") or 0),
      "run_attempt": int(metadata.get("run_attempt") or 1),
      "commit": metadata.get("commit"),
      "key": key,
      "medians": medians,
  }


def load_history(
    root: Path, *, allow_legacy: bool = False
) -> list[dict[str, Any]]:
  """Load every artifact under root, ordered oldest to newest."""
  artifacts = []
  for directory in root.iterdir():
    if not directory.is_dir():
      continue
    artifact = load_artifact(directory, allow_legacy=allow_legacy)
    if artifact is not None and artifact["medians"]:
      artifacts.append(artifact)
  artifacts.sort(key=lambda a: (a["run_id"], a["run_attempt"]))
  return artifacts


def detect(
    artifacts: list[dict[str, Any]],
    *,
    relative_floor: float,
    absolute_floor_ns: float,
) -> dict[str, Any]:
  """Run the control-chart rule over the newest artifact's stratum."""
  if not artifacts:
    return {"verdict": "no-data", "suspects": []}
  newest_key = artifacts[-1]["key"]
  stratum = [a for a in artifacts if a["key"] == newest_key]
  if len(stratum) < CANDIDATES + MIN_BASELINE:
    previous_key = artifacts[-2]["key"] if len(artifacts) > 1 else None
    verdict = (
        "series-break"
        if previous_key is not None and previous_key != newest_key
        else "insufficient-history"
    )
    return {
        "verdict": verdict,
        "stratum_size": len(stratum),
        "suspects": [],
    }

  candidates = stratum[-CANDIDATES:]
  baseline = stratum[-(CANDIDATES + MAX_BASELINE):-CANDIDATES]
  suspects = []
  for name in sorted(candidates[-1]["medians"]):
    if any(name not in c["medians"] for c in candidates):
      continue
    history = [
        a["medians"][name] for a in baseline if name in a["medians"]
    ]
    if len(history) < MIN_BASELINE:
      continue
    base_median = statistics.median(history)
    if all(
        c["medians"][name] > base_median * (1.0 + relative_floor)
        and c["medians"][name] - base_median > absolute_floor_ns
        for c in candidates
    ):
      newest = candidates[-1]["medians"][name]
      suspects.append({
          "benchmark": name,
          "baseline_median_ns": base_median,
          "newest_median_ns": newest,
          "delta_relative": newest / base_median - 1.0,
      })
  return {
      "verdict": "suspects" if suspects else "clean",
      "stratum_size": len(stratum),
      "first_elevated_commit": candidates[0]["commit"],
      "last_clean_commit": baseline[-1]["commit"],
      "newest_commit": candidates[-1]["commit"],
      "suspects": suspects,
  }


def render_report(result: dict[str, Any]) -> str:
  """Render the markdown report for the step summary and issue."""
  verdict = result["verdict"]
  if verdict != "suspects":
    return f"### Benchmark change-point check: {verdict}\n"
  lines = [
      "### Benchmark change-point check: sustained shift suspected",
      "",
      f"Suspect commit range: `{result['last_clean_commit']}` (last clean)"
      f" ... `{result['first_elevated_commit']}` (first elevated).",
      "Confirm or refute with the paired sentinel confirmation workflow"
      " before acting; hosted-runner alerting is advisory"
      " (redesign section 4.2).",
      "",
      "| Benchmark | Baseline median | Newest median | Δ |",
      "| --- | --- | --- | --- |",
  ]
  for suspect in result["suspects"]:
    lines.append(
        f"| {suspect['benchmark']} "
        f"| {suspect['baseline_median_ns']:,.0f} ns "
        f"| {suspect['newest_median_ns']:,.0f} ns "
        f"| {suspect['delta_relative']:+.1%} |"
    )
  return "\n".join(lines) + "\n"


def main(argv: list[str] | None = None) -> int:
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("--artifacts", required=True, type=Path)
  parser.add_argument("--report", type=Path)
  parser.add_argument("--json", dest="json_out", type=Path)
  parser.add_argument("--relative-floor", type=float, default=0.10)
  parser.add_argument("--absolute-floor-ns", type=float, default=2000.0)
  parser.add_argument(
      "--allow-legacy",
      action="store_true",
      help="backtesting only: pre-fingerprint artifacts share one stratum",
  )
  args = parser.parse_args(argv)

  if not args.artifacts.is_dir():
    print(f"error: {args.artifacts} is not a directory")
    return 1
  artifacts = load_history(args.artifacts, allow_legacy=args.allow_legacy)
  result = detect(
      artifacts,
      relative_floor=args.relative_floor,
      absolute_floor_ns=args.absolute_floor_ns,
  )
  report = render_report(result)
  print(report)
  if args.report:
    args.report.write_text(report, encoding="utf-8")
  if args.json_out:
    args.json_out.write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
