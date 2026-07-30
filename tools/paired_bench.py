#!/usr/bin/env python3
"""Run sentinel benchmarks paired between a base and a head binary.

The redesign's noise-robust timing comparison (section 4.1): both
binaries run the sentinel set in alternating rounds on the same
machine, and a sentinel flags only when the bootstrap confidence
interval on its median ratio clears a practical-effect threshold and
an absolute materiality floor. Flagged sentinels are re-run once;
only a confirmed flag is a regression. Shadow mode reports without
gating; confirm mode exits nonzero on a confirmed regression.
"""

from __future__ import annotations

import argparse
import json
import random
import statistics
import subprocess
import sys
from collections.abc import Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path
from typing import Any


class ToolError(RuntimeError):
  """An input or execution failure that must fail the run."""


@dataclass(frozen=True)
class Sentinel:
  """One sentinel benchmark and the metric it is judged on."""

  metric: str


@dataclass(frozen=True)
class Config:
  """Sentinel definitions, source map, and statistical parameters."""

  sentinels: dict[str, Sentinel]
  source_map: dict[str, Any]
  repetitions: int
  effect_floor: float
  materiality_ns: float
  resamples: int
  confidence: float


@dataclass(frozen=True)
class SentinelResult:
  """The paired comparison for one sentinel in one pass."""

  name: str
  base_median: float
  head_median: float
  delta_relative: float
  ci_low: float
  ci_high: float
  flagged: bool


def load_config(path: Path) -> Config:
  """Load and validate the sentinel definition file."""
  data = json.loads(Path(path).read_text(encoding="utf-8"))
  sentinels = {
    name: Sentinel(metric=entry["metric"])
    for name, entry in data["sentinels"].items()
  }
  if not sentinels:
    raise ToolError("sentinel file defines no sentinels")
  parameters = data["parameters"]
  return Config(
    sentinels=sentinels,
    source_map=data.get("source_map", {}),
    repetitions=int(parameters["repetitions"]),
    effect_floor=float(parameters["effect_floor_relative"]),
    materiality_ns=float(parameters["materiality_floor_ns"]),
    resamples=int(parameters["bootstrap_resamples"]),
    confidence=float(parameters["confidence"]),
  )


def benchmark_filter(names: Sequence[str]) -> str:
  """Return the anchored Google Benchmark filter for the sentinel set."""
  return "^(" + "|".join(names) + ")$"


def parse_results(
  payload: str,
  metrics: Mapping[str, str],
) -> dict[str, float]:
  """Extract each sentinel's configured metric from benchmark JSON."""
  unit_to_ns = {"ns": 1.0, "us": 1_000.0, "ms": 1_000_000.0, "s": 1e9}
  try:
    data = json.loads(payload)
    values = {}
    for benchmark in data.get("benchmarks", []):
      name = benchmark["name"]
      if name in metrics:
        scale = unit_to_ns[benchmark.get("time_unit", "ns")]
        values[name] = float(benchmark[metrics[name]]) * scale
  except (json.JSONDecodeError, KeyError, TypeError, ValueError) as error:
    raise ToolError(f"unparseable benchmark output: {error}") from error
  missing = sorted(set(metrics) - set(values))
  if missing:
    raise ToolError(f"benchmark output missing sentinels: {missing}")
  return values


def list_benchmarks(binary: Path) -> frozenset[str]:
  """Return the benchmark names a binary registers."""
  try:
    result = subprocess.run(
      (str(binary), "--benchmark_list_tests=true"),
      capture_output=True,
      text=True,
      timeout=120,
    )
  except (OSError, subprocess.TimeoutExpired) as error:
    raise ToolError(f"cannot list benchmarks in {binary}: {error}") from error
  if result.returncode != 0:
    raise ToolError(f"{binary} failed to list benchmarks")
  return frozenset(
    line.strip() for line in result.stdout.splitlines() if line.strip()
  )


def comparable_sentinels(
  config: Config,
  base_names: frozenset[str],
  head_names: frozenset[str],
) -> tuple[dict[str, Sentinel], dict[str, str]]:
  """Split sentinels into comparable and skipped-with-reason.

  A sentinel renamed or removed on one side cannot be compared; it is
  reported rather than silently dropped, and an empty comparable set
  fails the run.
  """
  comparable = {}
  skipped = {}
  for name, sentinel in config.sentinels.items():
    if name not in base_names:
      skipped[name] = "not registered in the base binary"
    elif name not in head_names:
      skipped[name] = "not registered in the head binary"
    else:
      comparable[name] = sentinel
  if not comparable:
    raise ToolError("no sentinel is registered in both binaries")
  return comparable, skipped


def round_sides(round_index: int) -> tuple[str, str]:
  """Alternate which side runs first to cancel machine drift."""
  if round_index % 2 == 0:
    return ("base", "head")
  return ("head", "base")


def run_binary(
  binary: Path,
  filter_re: str,
  metrics: Mapping[str, str],
) -> dict[str, float]:
  """Run one benchmark binary over the sentinel filter."""
  command = (
    str(binary),
    f"--benchmark_filter={filter_re}",
    "--benchmark_format=json",
  )
  try:
    result = subprocess.run(
      command, capture_output=True, text=True, timeout=1200
    )
  except (OSError, subprocess.TimeoutExpired) as error:
    raise ToolError(f"cannot run {binary}: {error}") from error
  if result.returncode != 0:
    raise ToolError(
      f"{binary} exited {result.returncode}: {result.stderr.strip()[:500]}"
    )
  return parse_results(result.stdout, metrics)


def collect(
  base_binary: Path,
  head_binary: Path,
  config: Config,
  runner=run_binary,
) -> dict[str, tuple[list[float], list[float]]]:
  """Collect interleaved samples for every sentinel from both sides."""
  metrics = {name: s.metric for name, s in config.sentinels.items()}
  filter_re = benchmark_filter(sorted(metrics))
  samples = {name: ([], []) for name in metrics}
  binaries = {"base": base_binary, "head": head_binary}
  for round_index in range(config.repetitions):
    for side in round_sides(round_index):
      values = runner(binaries[side], filter_re, metrics)
      for name, value in values.items():
        samples[name][0 if side == "base" else 1].append(value)
  return samples


def adjusted_confidence(confidence: float, comparisons: int) -> float:
  """Bonferroni-adjust a per-comparison confidence for a family size."""
  if comparisons <= 1:
    return confidence
  return 1.0 - (1.0 - confidence) / comparisons


def _paired_bootstrap_ci(
  ratios: Sequence[float],
  *,
  resamples: int,
  confidence: float,
  seed: int,
) -> tuple[float, float]:
  """Percentile bootstrap CI for the median per-round ratio minus one.

  Rounds are paired — base and head samples with the same index ran
  back to back under the same machine conditions — so the resampling
  unit is the per-round ratio, not the marginal samples.
  """
  rng = random.Random(seed)
  deltas = []
  for _ in range(resamples):
    sample = [rng.choice(ratios) for _ in ratios]
    deltas.append(statistics.median(sample) - 1.0)
  deltas.sort()
  tail = (1.0 - confidence) / 2.0
  # Conservative symmetric percentile indices: round the lower bound
  # down and mirror it, widening rather than narrowing the interval.
  low_index = int(tail * (len(deltas) - 1))
  high_index = len(deltas) - 1 - low_index
  return (deltas[low_index], deltas[high_index])


def evaluate_sentinel(
  name: str,
  base: Sequence[float],
  head: Sequence[float],
  *,
  effect_floor: float,
  materiality_ns: float,
  resamples: int,
  confidence: float,
  seed: int,
) -> SentinelResult:
  """Judge one sentinel's paired samples against both floors."""
  if len(base) != len(head):
    raise ToolError(f"{name}: unpaired sample counts {len(base)}/{len(head)}")
  for value in (*base, *head):
    if not (value > 0.0 and value == value and value != float("inf")):
      raise ToolError(f"{name}: non-finite or nonpositive sample {value!r}")
  ratios = [h / b for b, h in zip(base, head)]
  base_median = statistics.median(base)
  head_median = statistics.median(head)
  paired_delta = statistics.median(h - b for b, h in zip(base, head))
  delta_relative = statistics.median(ratios) - 1.0
  ci_low, ci_high = _paired_bootstrap_ci(
    ratios,
    resamples=resamples,
    confidence=confidence,
    seed=seed,
  )
  flagged = ci_low > effect_floor and paired_delta > materiality_ns
  return SentinelResult(
    name=name,
    base_median=base_median,
    head_median=head_median,
    delta_relative=delta_relative,
    ci_low=ci_low,
    ci_high=ci_high,
    flagged=flagged,
  )


def evaluate(
  samples: Mapping[str, tuple[list[float], list[float]]],
  config: Config,
  seed: int,
) -> list[SentinelResult]:
  """Judge every sentinel, with a per-sentinel derived seed."""
  results = []
  for index, name in enumerate(sorted(samples)):
    base, head = samples[name]
    results.append(
      evaluate_sentinel(
        name,
        base,
        head,
        effect_floor=config.effect_floor,
        materiality_ns=config.materiality_ns,
        resamples=config.resamples,
        confidence=config.confidence,
        seed=seed + index,
      )
    )
  return results


def sentinel_verdict(flagged: bool, confirmed: bool | None) -> str:
  """Combine the first pass and the confirmation pass into a verdict."""
  if not flagged:
    return "pass"
  return "regression" if confirmed else "advisory"


def run_verdict(verdicts: Sequence[str]) -> str:
  """The run's verdict is its worst sentinel verdict."""
  for level in ("regression", "advisory"):
    if level in verdicts:
      return level
  return "pass"


def render_markdown(
  judged: Sequence[tuple[SentinelResult, str]],
  overall: str,
  *,
  mode: str,
  confidence: float = 0.95,
  skipped: Mapping[str, str] | None = None,
) -> str:
  """Render the step-summary table."""
  level = f"{confidence * 100.0:.4f}".rstrip("0").rstrip(".") + "%"
  lines = [
    f"### Paired sentinel run — **{overall}** ({mode} mode)",
    "",
    f"| Sentinel | Base median | Head median | Δ | {level} CI | Verdict |",
    "| --- | --- | --- | --- | --- | --- |",
  ]
  for result, verdict in judged:
    lines.append(
      f"| {result.name} "
      f"| {result.base_median:,.0f} ns "
      f"| {result.head_median:,.0f} ns "
      f"| {result.delta_relative:+.1%} "
      f"| [{result.ci_low:+.1%}, {result.ci_high:+.1%}] "
      f"| {verdict} |"
    )
  for name, reason in sorted((skipped or {}).items()):
    lines.append(f"| {name} | — | — | — | — | skipped: {reason} |")
  if mode == "shadow":
    lines.append("")
    lines.append(
      "Shadow mode: results are informational; the calibrated threshold "
      "gates remain authoritative (redesign section 4.3)."
    )
  return "\n".join(lines) + "\n"


MAX_SUSPECTS = 64


def load_threshold_metrics(thresholds_dir: Path) -> dict[str, str]:
  """Map benchmark names to their gated metric from the manifests.

  A benchmark gated on real time (the parallel pool suite and manually
  timed cache benchmarks) is judged on real time here too; everything
  else, including ungated lab registrations, defaults to CPU time.
  """
  metrics: dict[str, str] = {}
  for manifest in sorted(thresholds_dir.glob("*.json")):
    try:
      entries = json.loads(manifest.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
      raise ToolError(f"unreadable thresholds manifest {manifest}: {error}")
    for name, entry in entries.get("benchmarks", {}).items():
      if entry.get("max_real_time_ns") is not None:
        metrics[name] = "real_time"
      else:
        metrics[name] = "cpu_time"
  return metrics


def suspect_sentinels(
  names: Sequence[str],
  thresholds_dir: Path,
) -> dict[str, Sentinel]:
  """Build the confirmation set from predeclared suspect names.

  Formal confirmation is deliberately suspect-scoped: the Bonferroni
  family sizes to this list, and lists beyond MAX_SUSPECTS are refused
  because extreme-tail bootstrap intervals at broad scopes are not
  statistically valid at practical round counts.
  """
  cleaned = [name.strip() for name in names if name.strip()]
  if not cleaned:
    raise ToolError("no suspect benchmark names given")
  if len(cleaned) > MAX_SUSPECTS:
    raise ToolError(
      f"{len(cleaned)} suspects exceeds the {MAX_SUSPECTS} cap; formal "
      "confirmation is targeted — narrow the list to the change-point "
      "report's suspects"
    )
  metrics = load_threshold_metrics(thresholds_dir)
  return {
    name: Sentinel(metric=metrics.get(name, "cpu_time")) for name in cleaned
  }


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("--base-binary", required=True, type=Path)
  parser.add_argument("--head-binary", required=True, type=Path)
  parser.add_argument("--sentinels", required=True, type=Path)
  parser.add_argument(
    "--suspects",
    help="comma/newline-separated benchmark names replacing the sentinel "
         "set for a targeted confirmation (statistics size to this list)",
  )
  parser.add_argument(
    "--thresholds-dir",
    type=Path,
    default=Path(__file__).resolve().parents[1] / "bench" / "thresholds",
    help=argparse.SUPPRESS,
  )
  parser.add_argument("--mode", choices=("shadow", "confirm"), required=True)
  parser.add_argument("--summary", type=Path)
  parser.add_argument("--json", dest="json_out", type=Path)
  parser.add_argument("--seed", type=int, default=20260729)
  parser.add_argument("--repetitions", type=int)
  return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
  args = parse_args(sys.argv[1:] if argv is None else argv)
  try:
    config = load_config(args.sentinels)
    if args.suspects:
      config = Config(
        sentinels=suspect_sentinels(
          args.suspects.replace(",", "\n").split("\n"),
          args.thresholds_dir,
        ),
        source_map={},
        repetitions=config.repetitions,
        effect_floor=config.effect_floor,
        materiality_ns=config.materiality_ns,
        resamples=config.resamples,
        confidence=config.confidence,
      )
    if args.repetitions is not None and args.repetitions < 1:
      raise ToolError("--repetitions must be at least 1")
    if args.repetitions:
      config = Config(
        sentinels=config.sentinels,
        source_map=config.source_map,
        repetitions=args.repetitions,
        effect_floor=config.effect_floor,
        materiality_ns=config.materiality_ns,
        resamples=config.resamples,
        confidence=config.confidence,
      )
    for binary in (args.base_binary, args.head_binary):
      if not Path(binary).is_file():
        raise ToolError(f"benchmark binary not found: {binary}")

    base_names = list_benchmarks(args.base_binary)
    head_names = list_benchmarks(args.head_binary)
    comparable, skipped = comparable_sentinels(
      config, base_names, head_names
    )
    config = Config(
      sentinels=comparable,
      source_map=config.source_map,
      repetitions=config.repetitions,
      effect_floor=config.effect_floor,
      materiality_ns=config.materiality_ns,
      resamples=config.resamples,
      confidence=config.confidence,
    )
    for name, reason in sorted(skipped.items()):
      print(f"skipping {name}: {reason}", flush=True)

    if args.mode == "confirm":
      # One confirmed regression fails the run, so control the
      # family-wise error across the sentinel set — and give the
      # narrower tails enough bootstrap resolution to be stable.
      confirm_confidence = adjusted_confidence(
        config.confidence, len(config.sentinels)
      )
      tail = (1.0 - confirm_confidence) / 2.0
      config = Config(
        sentinels=config.sentinels,
        source_map=config.source_map,
        repetitions=config.repetitions,
        effect_floor=config.effect_floor,
        materiality_ns=config.materiality_ns,
        resamples=max(config.resamples, int(100.0 / max(tail, 1e-6))),
        confidence=confirm_confidence,
      )

    samples = collect(args.base_binary, args.head_binary, config)
    first_pass = evaluate(samples, config, seed=args.seed)
    flagged = [r for r in first_pass if r.flagged]
    confirmations: dict[str, bool] = {}
    rerun = {}
    if flagged:
      print(
        f"{len(flagged)} sentinel(s) flagged; re-running once to confirm",
        flush=True,
      )
      rerun_samples = collect(args.base_binary, args.head_binary, config)
      rerun = {
        r.name: r for r in evaluate(rerun_samples, config, seed=args.seed + 1)
      }
      confirmations = {r.name: rerun[r.name].flagged for r in flagged}

    judged = [
      (result, sentinel_verdict(result.flagged, confirmations.get(result.name)))
      for result in first_pass
    ]
    overall = run_verdict([verdict for _, verdict in judged])

    report = {
      "mode": args.mode,
      "verdict": overall,
      "repetitions": config.repetitions,
      "confidence": config.confidence,
      "resamples": config.resamples,
      "skipped": skipped,
      "sentinels": {
        result.name: {
          "base_median_ns": result.base_median,
          "head_median_ns": result.head_median,
          "delta_relative": result.delta_relative,
          "ci_low": result.ci_low,
          "ci_high": result.ci_high,
          "verdict": verdict,
          **(
            {
              "confirmation": {
                "base_median_ns": rerun[result.name].base_median,
                "head_median_ns": rerun[result.name].head_median,
                "delta_relative": rerun[result.name].delta_relative,
                "ci_low": rerun[result.name].ci_low,
                "ci_high": rerun[result.name].ci_high,
                "flagged": rerun[result.name].flagged,
              }
            }
            if result.name in rerun
            else {}
          ),
        }
        for result, verdict in judged
      },
    }
    if args.json_out:
      args.json_out.write_text(
        json.dumps(report, indent=2) + "\n", encoding="utf-8"
      )
    summary = render_markdown(
      judged,
      overall,
      mode=args.mode,
      confidence=config.confidence,
      skipped=skipped,
    )
    if args.summary:
      args.summary.write_text(summary, encoding="utf-8")
    print(summary)
  except ToolError as error:
    print(f"error: {error}", file=sys.stderr)
    return 1

  if args.mode == "confirm" and overall == "regression":
    return 1
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
