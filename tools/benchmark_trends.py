#!/usr/bin/env python3
"""Generate benchmark trend reports from CI baseline artifacts."""

from __future__ import annotations

import argparse
import html
import json
import re
import statistics
import sys
import tempfile
import zipfile
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any
from zoneinfo import ZoneInfo


DEFAULT_BENCHMARKS = (
    "block/context_iteration_2d",
    "block/chunk_tile_iteration_2d",
    "block/chunk_boundary_scan_2d",
    "storage/world_chunks_iteration",
    "storage/world_dirty_chunks_iteration",
)
PACIFIC = ZoneInfo("America/Vancouver")


@dataclass(frozen=True)
class Run:
  label: str
  commit: str | None
  run_id: str | None
  timestamp: datetime | None
  values: dict[str, float]


def main(argv: list[str] | None = None) -> int:
  parser = argparse.ArgumentParser()
  parser.add_argument("artifacts", nargs="+", type=Path)
  parser.add_argument("--out", type=Path)
  parser.add_argument("--snapshot-svg", type=Path)
  parser.add_argument("--summary-md", type=Path)
  parser.add_argument("--benchmark", action="append", dest="benchmarks")
  parser.add_argument("--source-commit")
  parser.add_argument("--source-run-id")
  args = parser.parse_args(argv)

  benchmarks = tuple(args.benchmarks or DEFAULT_BENCHMARKS)
  runs = sorted(
      (
          load_run(path, benchmarks, args.source_commit, args.source_run_id)
          for path in args.artifacts
      ),
      key=lambda run: run.timestamp
      or datetime.min.replace(tzinfo=timezone.utc),
  )

  if args.benchmarks:
    matched = {name for run in runs for name in run.values}
    unmatched = [name for name in benchmarks if name not in matched]
    if unmatched:
      for name in unmatched:
        print(
            f"benchmark_trends: --benchmark {name} matched no results in "
            "any artifact",
            file=sys.stderr,
        )
      return 1

  if args.out is not None:
    write_text(args.out, render_html(runs, benchmarks))
  if args.snapshot_svg is not None:
    write_text(args.snapshot_svg, render_svg(runs, benchmarks, compact=True))
  if args.summary_md is not None:
    write_text(args.summary_md, render_markdown(runs, benchmarks))
  if args.out is None and args.snapshot_svg is None and args.summary_md is None:
    print(render_markdown(runs, benchmarks))

  return 0


def load_run(
    path: Path,
    benchmarks: tuple[str, ...],
    source_commit: str | None,
    source_run_id: str | None,
) -> Run:
  with artifact_dir(path) as directory:
    metadata = load_metadata(directory)
    values = collect_values(directory, benchmarks)
    timestamp = metadata_timestamp(metadata) or benchmark_timestamp(directory)
    run_id = metadata.get("run_id") or source_run_id or infer_run_id(path)
    commit = metadata.get("commit") or source_commit
    label = run_label(commit, run_id, timestamp, path)
    return Run(label, commit, run_id, timestamp, values)


def artifact_dir(path: Path):
  if path.is_dir():
    return NullContext(path)
  if zipfile.is_zipfile(path):
    temp = tempfile.TemporaryDirectory()
    with zipfile.ZipFile(path) as archive:
      archive.extractall(temp.name)
    return TempContext(temp)
  raise ValueError(f"{path} is not a directory or zip artifact")


class NullContext:
  def __init__(self, path: Path):
    self.path = path

  def __enter__(self) -> Path:
    return self.path

  def __exit__(self, *_: object) -> None:
    return None


class TempContext:
  def __init__(self, temp: tempfile.TemporaryDirectory[str]):
    self.temp = temp

  def __enter__(self) -> Path:
    return Path(self.temp.name)

  def __exit__(self, *_: object) -> None:
    self.temp.cleanup()


def load_metadata(directory: Path) -> dict[str, str]:
  path = directory / "metadata.json"
  if not path.exists():
    return {}
  data = load_json(path)
  return {key: value for key, value in data.items() if isinstance(value, str)}


def benchmark_result_files(directory: Path) -> list[Path]:
  """Every benchmark result JSON in a baseline artifact directory.

  Discovers files instead of hardcoding family names so new benchmark
  families are picked up automatically; only the artifact metadata file
  is excluded.
  """
  return sorted(
      path
      for path in directory.glob("*.json")
      if path.name != "metadata.json"
  )


def collect_values(
    directory: Path,
    benchmarks: tuple[str, ...],
) -> dict[str, float]:
  samples: dict[str, list[float]] = {name: [] for name in benchmarks}
  for path in benchmark_result_files(directory):
    data = load_json(path)
    for benchmark in data.get("benchmarks", []):
      bench_name = benchmark.get("name")
      if bench_name not in samples:
        continue
      if is_aggregate_name(str(bench_name)):
        continue
      value = benchmark.get("cpu_time")
      unit = benchmark.get("time_unit")
      if value is None or unit is None:
        continue
      samples[str(bench_name)].append(to_nanoseconds(float(value), str(unit)))

  return {
      name: statistics.median(values)
      for name, values in samples.items()
      if values
  }


def metadata_timestamp(metadata: dict[str, str]) -> datetime | None:
  value = metadata.get("generated_at_utc")
  if value is None:
    return None
  return parse_datetime(value)


def benchmark_timestamp(directory: Path) -> datetime | None:
  for path in benchmark_result_files(directory):
    context = load_json(path).get("context", {})
    if isinstance(context, dict) and isinstance(context.get("date"), str):
      timestamp = parse_datetime(context["date"])
      if timestamp is not None:
        return timestamp
  return None


def run_label(
    commit: str | None,
    run_id: str | None,
    timestamp: datetime | None,
    path: Path,
) -> str:
  parts: list[str] = []
  if commit:
    parts.append(commit[:7])
  elif run_id:
    parts.append(f"run {run_id}")
  else:
    parts.append(path.stem)
  if timestamp is not None:
    parts.append(pacific_time(timestamp, short=True))
  return "\\n".join(parts)


def render_html(runs: list[Run], benchmarks: tuple[str, ...]) -> str:
  svg = render_svg(runs, benchmarks, compact=False)
  rows = "\n".join(summary_rows(runs, benchmarks))
  return f"""<!doctype html>
<html lang=\"en\">
<meta charset=\"utf-8\">
<title>Tess Benchmark Trends</title>
<style>
body {{ font-family: -apple-system, BlinkMacSystemFont, Segoe UI, sans-serif; }}
main {{ max-width: 1100px; margin: 2rem auto; padding: 0 1rem; }}
table {{ border-collapse: collapse; width: 100%; margin-top: 1rem; }}
td, th {{ border-bottom: 1px solid #ddd; padding: 0.35rem 0.5rem; }}
td, th {{ text-align: right; }}
td:first-child, th:first-child {{ text-align: left; }}
</style>
<main>
<h1>Tess Benchmark Trends</h1>
<p>{html.escape(stale_label(runs))}</p>
{svg}
<table>
<thead><tr><th>Benchmark</th><th>Latest median CPU ns</th></tr></thead>
<tbody>
{rows}
</tbody>
</table>
</main>
</html>
"""


def render_markdown(runs: list[Run], benchmarks: tuple[str, ...]) -> str:
  lines = [
      "# Benchmark Trends",
      "",
      stale_label(runs),
      "",
      "The SVG snapshot in `docs/assets/benchmark-trends.svg` is a labeled",
      "summary, not the source of truth. Use CI benchmark baseline artifacts",
      "and threshold JSON files for calibration decisions.",
      "",
      "Regenerate the detailed report with:",
      "",
      "```sh",
      "tools/benchmark_trends.py path/to/benchmark-baselines-* \\",
      "  --out build/bench/benchmark-trends.html \\",
      "  --snapshot-svg docs/assets/benchmark-trends.svg \\",
      "  --summary-md build/bench/benchmark-trends.md",
      "```",
      "",
      "## Latest Snapshot",
      "",
      "![Benchmark trend snapshot](assets/benchmark-trends.svg)",
      "",
      "| Benchmark | Latest median CPU ns |",
      "| --- | ---: |",
  ]
  latest = runs[-1] if runs else None
  for name in benchmarks:
    value = latest.values.get(name) if latest is not None else None
    lines.append(f"| `{name}` | {format_ns(value)} |")
  lines.append("")
  return "\n".join(lines)


def render_svg(
    runs: list[Run],
    benchmarks: tuple[str, ...],
    compact: bool,
) -> str:
  width = 920
  row_height = 86 if compact else 104
  top = 74
  left = 235
  right = 36
  height = top + row_height * len(benchmarks) + 36
  plot_width = width - left - right
  palette = ["#2563eb", "#16a34a", "#dc2626", "#7c3aed", "#ca8a04"]
  latest = runs[-1] if runs else None
  elements = [
      f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" '
      f'height="{height}" viewBox="0 0 {width} {height}" role="img" '
      'aria-label="Benchmark trend snapshot">',
      '<rect width="100%" height="100%" fill="#ffffff"/>',
      '<text x="20" y="28" font-family="Arial, sans-serif" font-size="18" '
      'font-weight="700" fill="#111827">Tess Benchmark Trends</text>',
      f'<text x="20" y="50" font-family="Arial, sans-serif" font-size="12" '
      f'fill="#4b5563">{escape(stale_label(runs))}</text>',
  ]

  for index, name in enumerate(benchmarks):
    y = top + index * row_height
    color = palette[index % len(palette)]
    values = [run.values.get(name) for run in runs]
    present = [value for value in values if value is not None]
    max_value = max(present) if present else 1.0
    latest_value = latest.values.get(name) if latest is not None else None
    elements.extend(
        [
            f'<text x="20" y="{y + 20}" font-family="Arial, sans-serif" '
            f'font-size="12" fill="#111827">{escape(name)}</text>',
            f'<text x="20" y="{y + 39}" font-family="Arial, sans-serif" '
            f'font-size="11" fill="#6b7280">latest '
            f'{format_ns(latest_value)} ns</text>',
            f'<line x1="{left}" y1="{y + 56}" x2="{width - right}" '
            f'y2="{y + 56}" stroke="#e5e7eb"/>',
        ]
    )
    points = []
    for run_index, value in enumerate(values):
      if value is None:
        continue
      x = (
          left
          if len(runs) == 1
          else left + plot_width * run_index / (len(runs) - 1)
      )
      point_y = y + 56 - (value / max_value) * 38
      points.append((x, point_y))
    if len(points) > 1:
      path = " ".join(f"{x:.1f},{point_y:.1f}" for x, point_y in points)
      elements.append(
          f'<polyline points="{path}" fill="none" stroke="{color}" '
          'stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"/>'
      )
    for x, point_y in points:
      elements.append(
          f'<circle cx="{x:.1f}" cy="{point_y:.1f}" r="3.5" '
          f'fill="{color}"/>'
      )

  elements.append("</svg>")
  return "\n".join(elements)


def summary_rows(runs: list[Run], benchmarks: tuple[str, ...]) -> list[str]:
  latest = runs[-1] if runs else None
  rows = []
  for name in benchmarks:
    value = latest.values.get(name) if latest is not None else None
    rows.append(
        f"<tr><td>{html.escape(name)}</td><td>{format_ns(value)}</td></tr>"
    )
  return rows


def stale_label(runs: list[Run]) -> str:
  if not runs:
    return "No benchmark artifacts were loaded."
  latest = runs[-1]
  commit = latest.commit[:7] if latest.commit else "unknown commit"
  run = latest.run_id or "unknown run"
  timestamp = (
      pacific_time(latest.timestamp)
      if latest.timestamp
      else "unknown Pacific time"
  )
  return (
      f"Stale snapshot: data from CI run {run} at commit {commit}, "
      f"collected {timestamp}."
  )


def pacific_time(timestamp: datetime, short: bool = False) -> str:
  if timestamp.tzinfo is None:
    timestamp = timestamp.replace(tzinfo=timezone.utc)
  local = timestamp.astimezone(PACIFIC)
  return local.strftime("%Y-%m-%d %H:%M %Z" if not short else "%m-%d %H:%M %Z")


def infer_run_id(path: Path) -> str | None:
  match = re.search(r"benchmark-baselines-(\d+)", path.name)
  return match.group(1) if match else None


def load_json(path: Path) -> dict[str, Any]:
  with path.open(encoding="utf-8") as file:
    data = json.load(file)
  if not isinstance(data, dict):
    raise TypeError(f"{path} must contain a JSON object")
  return data


def parse_datetime(value: str) -> datetime | None:
  try:
    return datetime.fromisoformat(value.replace("Z", "+00:00"))
  except ValueError:
    return None


def is_aggregate_name(name: str) -> bool:
  return name.endswith(("_mean", "_median", "_stddev", "_cv"))


def to_nanoseconds(value: float, unit: str) -> float:
  if unit == "ns":
    return value
  if unit == "us":
    return value * 1_000.0
  if unit == "ms":
    return value * 1_000_000.0
  if unit == "s":
    return value * 1_000_000_000.0
  raise ValueError(f"unsupported benchmark time unit: {unit}")


def format_ns(value: float | None) -> str:
  return "n/a" if value is None else f"{value:.3f}"


def write_text(path: Path, content: str) -> None:
  path.parent.mkdir(parents=True, exist_ok=True)
  path.write_text(content, encoding="utf-8")


def escape(value: str) -> str:
  return html.escape(value, quote=True)


if __name__ == "__main__":
  raise SystemExit(main())
