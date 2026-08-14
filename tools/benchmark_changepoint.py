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
import math
import statistics
from pathlib import Path
from typing import Any

from benchmark_thresholds import ToolError as _ThresholdsError
from benchmark_thresholds import load_threshold_metrics

CANDIDATES = 3
MIN_BASELINE = 8
MAX_BASELINE = 30

UNIT_TO_NS = {"ns": 1.0, "us": 1_000.0, "ms": 1_000_000.0, "s": 1e9}


class ToolError(RuntimeError):
  """Unreadable input that must fail the run."""


def load_artifact(
    directory: Path,
    *,
    allow_legacy: bool = False,
    metrics: dict[str, str] | None = None,
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
  if key == "legacy":
    # Pre-fingerprint artifacts also predate the event/ref fields.
    if metadata.get("event_name") not in (None, "push"):
      return None
    if metadata.get("ref") not in (None, "main", "refs/heads/main"):
      return None
  else:
    # Fingerprinted artifacts must positively attest to a main push;
    # absence is exclusion, not tolerance (series provenance).
    if metadata.get("event_name") != "push":
      return None
    if metadata.get("ref") not in ("main", "refs/heads/main"):
      return None

  medians: dict[str, float] = {}
  observed_names: set[str] = set()
  unusable_names: set[str] = set()
  for result in sorted(directory.glob("*.json")):
    if result.name == "metadata.json":
      continue
    try:
      data = json.loads(result.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
      # A truncated family file must not yield a partial artifact that
      # the detector could call clean without evaluating the family.
      return None
    samples: dict[str, list[float]] = {}
    for row in data.get("benchmarks", []):
      name = row.get("run_name", row.get("name"))
      if name is None:
        continue
      name = str(name)
      observed_names.add(name)
      if row.get("run_type") == "aggregate":
        continue
      unit = UNIT_TO_NS.get(row.get("time_unit", "ns"))
      # Judge each benchmark on the metric its family is gated on, so an
      # alert and the confirmation command it prints mean the same
      # number. Absent from the manifests: CPU time, as before.
      value = row.get((metrics or {}).get(name, "cpu_time"))
      if unit is None or value is None:
        unusable_names.add(name)
        continue
      try:
        normalized = float(value) * unit
      except (TypeError, ValueError, OverflowError):
        unusable_names.add(name)
        continue
      if not math.isfinite(normalized):
        unusable_names.add(name)
        continue
      samples.setdefault(name, []).append(normalized)
    for name, values in samples.items():
      if name not in unusable_names:
        medians[name] = statistics.median(values)
  return {
      "run_id": int(metadata.get("run_id") or 0),
      "run_attempt": int(metadata.get("run_attempt") or 1),
      "commit": metadata.get("commit"),
      "key": key,
      "medians": medians,
      "observed_names": sorted(observed_names),
      "unusable_names": sorted(unusable_names),
  }


def load_history(
    root: Path,
    *,
    allow_legacy: bool = False,
    metrics: dict[str, str] | None = None,
) -> list[dict[str, Any]]:
  """Load every artifact under root, ordered oldest to newest."""
  artifacts = []
  for directory in root.iterdir():
    if not directory.is_dir():
      continue
    artifact = load_artifact(
        directory, allow_legacy=allow_legacy, metrics=metrics
    )
    if artifact is not None and artifact["observed_names"]:
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
    return _result("no-data")
  newest_key = artifacts[-1]["key"]
  stratum = [a for a in artifacts if a["key"] == newest_key]
  if len(stratum) < CANDIDATES + MIN_BASELINE:
    seen_before = any(a["key"] == newest_key for a in artifacts[:-1])
    verdict = "insufficient-history" if seen_before else (
        "series-break" if len(artifacts) > 1 else "insufficient-history"
    )
    candidate_names = _candidate_names(stratum[-CANDIDATES:])
    not_evaluated = [
        {
            "benchmark": name,
            "reason": "insufficient-stratum-history",
            "stratum_artifacts": len(stratum),
            "required_stratum_artifacts": CANDIDATES + MIN_BASELINE,
        }
        for name in candidate_names
    ]
    return _result(
        verdict,
        stratum_size=len(stratum),
        candidate_count=len(candidate_names),
        not_evaluated=not_evaluated,
    )

  candidates = stratum[-CANDIDATES:]
  baseline = stratum[-(CANDIDATES + MAX_BASELINE):-CANDIDATES]
  suspects = []
  not_evaluated = []
  evaluated_count = 0
  candidate_names = _candidate_names(candidates)
  for name in candidate_names:
    missing_run_ids = [
        c["run_id"] for c in candidates
        if name not in set(c["observed_names"])
    ]
    if missing_run_ids:
      not_evaluated.append({
          "benchmark": name,
          "reason": "missing-candidate",
          "missing_run_ids": missing_run_ids,
      })
      continue
    unusable_run_ids = [
        c["run_id"] for c in candidates
        if name in set(c["unusable_names"]) or name not in c["medians"]
    ]
    if unusable_run_ids:
      not_evaluated.append({
          "benchmark": name,
          "reason": "unusable-candidate-reading",
          "unusable_run_ids": unusable_run_ids,
      })
      continue
    history = [
        a["medians"][name] for a in baseline if name in a["medians"]
    ]
    if len(history) < MIN_BASELINE:
      not_evaluated.append({
          "benchmark": name,
          "reason": "insufficient-baseline",
          "baseline_artifacts": len(history),
          "required_baseline_artifacts": MIN_BASELINE,
      })
      continue
    evaluated_count += 1
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
  if suspects:
    verdict = "suspects"
  elif evaluated_count == 0:
    verdict = "insufficient-history"
  elif not_evaluated:
    verdict = "partial"
  else:
    verdict = "clean"
  return _result(
      verdict,
      stratum_size=len(stratum),
      candidate_count=len(candidate_names),
      evaluated_count=evaluated_count,
      not_evaluated=not_evaluated,
      suspects=suspects,
      first_elevated_commit=candidates[0]["commit"],
      last_clean_commit=baseline[-1]["commit"],
      newest_commit=candidates[-1]["commit"],
  )


def _candidate_names(candidates: list[dict[str, Any]]) -> list[str]:
  """Return the deterministic union of raw names in candidate artifacts."""
  return sorted({
      name
      for candidate in candidates
      for name in candidate["observed_names"]
  })


def _result(
    verdict: str,
    *,
    stratum_size: int = 0,
    candidate_count: int = 0,
    evaluated_count: int = 0,
    not_evaluated: list[dict[str, Any]] | None = None,
    suspects: list[dict[str, Any]] | None = None,
    **details: Any,
) -> dict[str, Any]:
  """Build a result with coverage fields present for every verdict."""
  return {
      "verdict": verdict,
      "stratum_size": stratum_size,
      "candidate_count": candidate_count,
      "evaluated_count": evaluated_count,
      "not_evaluated": not_evaluated or [],
      "suspects": suspects or [],
      **details,
  }


def render_report(result: dict[str, Any]) -> str:
  """Render the markdown report for the step summary and issue."""
  verdict = result["verdict"]
  if verdict == "series-break":
    lines = [
        "### Benchmark change-point check: series break",
        "",
        "The runner fingerprint changed; confirm any suspicion manually "
        "with the paired sentinel confirmation workflow while the new "
        "stratum accumulates history.",
    ]
    lines.extend(_coverage_report(result))
    return "\n".join(lines) + "\n"
  if verdict != "suspects":
    lines = [f"### Benchmark change-point check: {verdict}"]
    lines.extend(_coverage_report(result))
    return "\n".join(lines) + "\n"
  lines = [
      "### Benchmark change-point check: sustained shift suspected",
      "",
      *(_coverage_report(result, leading_blank=False)),
      "",
      f"Suspect commit range: `{result['last_clean_commit']}` (newest"
      f" baseline artifact) ... `{result['first_elevated_commit']}` (first"
      " flagged candidate). A shift older than the three candidates may"
      " originate earlier in the baseline window.",
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
  # The diagnostics-binary families cannot be confirmed by the
  # dispatch workflow (it builds tess_bench only); presenting them as
  # paste-ready would produce a run that always fails.
  diagnostics_families = ("diagnostics/", "ecs/", "render_delta/",
                          "fields/")
  suspects = [s["benchmark"] for s in result["suspects"]]
  confirmable = [
      s for s in suspects if not s.startswith(diagnostics_families)
  ]
  diagnostics_only = [
      s for s in suspects if s.startswith(diagnostics_families)
  ]
  # The confirmation tool refuses more than 64 suspects; a broad
  # shift must confirm in batches rather than get a command that
  # always fails.
  shown = confirmable[:64]
  overflow = len(confirmable) - len(shown)
  lines.append("")
  lines.append(
      "Confirmation is the reproduce-paired step of the [profiling"
      " protocol](https://github.com/kindjie/tess/blob/main/"
      "CONTRIBUTING.md); a confirmed shift proceeds to"
      " profile-and-diff under `bench-profile`, and every outcome —"
      " accepted, rejected, deferred, or inconclusive — lands in the"
      " optimization log."
  )
  if shown:
    lines.append(
        f"Paste-ready suspect list: `--suspects={','.join(shown)}`"
        + (
            f" (plus {overflow} more; the confirmation tool caps at 64"
            " suspects per run — confirm in batches)"
            if overflow else ""
        )
    )
  if diagnostics_only:
    lines.append(
        "Diagnostics-binary suspects (not confirmable by the dispatch"
        " workflow, which builds `tess_bench` only — reproduce locally"
        " against `tess_bench_diagnostics` builds per the protocol): "
        + ", ".join(f"`{name}`" for name in diagnostics_only)
    )
  return "\n".join(lines) + "\n"


def _coverage_report(
    result: dict[str, Any], *, leading_blank: bool = True
) -> list[str]:
  """Render coverage counts and any benchmark-level omissions."""
  lines = [""] if leading_blank else []
  lines.append(
      f"Evaluated {result['evaluated_count']} of "
      f"{result['candidate_count']} candidate benchmarks."
  )
  lines.extend(_not_evaluated_report(result["not_evaluated"]))
  return lines


def _not_evaluated_report(entries: list[dict[str, Any]]) -> list[str]:
  """Render deterministic details for benchmarks not evaluated."""
  if not entries:
    return []
  lines = [
      "",
      "| Benchmark not evaluated | Reason |",
      "| --- | --- |",
  ]
  for entry in entries:
    reason = entry["reason"]
    if reason == "missing-candidate":
      detail = "missing from candidate run(s) " + ", ".join(
          str(run_id) for run_id in entry["missing_run_ids"]
      )
    elif reason == "unusable-candidate-reading":
      detail = "unusable reading in candidate run(s) " + ", ".join(
          str(run_id) for run_id in entry["unusable_run_ids"]
      )
    elif reason == "insufficient-baseline":
      detail = (
          f"{entry['baseline_artifacts']} of "
          f"{entry['required_baseline_artifacts']} required baseline "
          "artifacts"
      )
    elif reason == "insufficient-stratum-history":
      detail = (
          f"{entry['stratum_artifacts']} of "
          f"{entry['required_stratum_artifacts']} required stratum artifacts"
      )
    else:
      detail = str(reason)
    lines.append(f"| {entry['benchmark']} | {detail} |")
  return lines


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
  parser.add_argument(
      "--thresholds-dir",
      type=Path,
      default=Path(__file__).resolve().parent.parent / "bench" / "thresholds",
      help=(
          "threshold manifests naming each benchmark's gated metric "
          "(default: the repository's bench/thresholds)"
      ),
  )
  args = parser.parse_args(argv)

  if not args.artifacts.is_dir():
    print(f"error: {args.artifacts} is not a directory")
    return 1
  if not args.thresholds_dir.is_dir():
    # Silently defaulting the whole suite to CPU time would restate the
    # real-time-gated families' history without saying so.
    print(f"error: {args.thresholds_dir} is not a directory")
    return 1
  try:
    metrics = load_threshold_metrics(args.thresholds_dir)
  except _ThresholdsError as error:
    print(f"error: {error}")
    return 1
  artifacts = load_history(
      args.artifacts, allow_legacy=args.allow_legacy, metrics=metrics
  )
  downloaded = sum(1 for d in args.artifacts.iterdir() if d.is_dir())
  newest_dir = max(
      (int(d.name) for d in args.artifacts.iterdir()
       if d.is_dir() and d.name.isdigit()),
      default=0,
  )
  if artifacts and newest_dir > artifacts[-1]["run_id"]:
    print(
        "::warning::change-point: the newest downloaded artifact "
        f"(run {newest_dir}) was unusable; analysis reflects an older "
        "run and the fingerprint producer should be checked"
    )
    result = _result("newest-unusable")
  else:
    result = detect(
        artifacts,
        relative_floor=args.relative_floor,
        absolute_floor_ns=args.absolute_floor_ns,
    )
  result["artifacts_loaded"] = len(artifacts)
  result["directories_downloaded"] = downloaded
  if downloaded > 0 and not artifacts:
    # A dead loader must be loud, never a green "no-data".
    print(
        "::warning::change-point loaded zero artifacts from "
        f"{downloaded} downloaded directories; the fingerprint producer "
        "or provenance filter is broken"
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
