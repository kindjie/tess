#!/usr/bin/env python3
"""Draw the serial-versus-pool crossover as an SVG.

One chart, deliberately. The 2026-08-03 campaigns produced exactly one
result robust enough to put in front of an adopter: at FOUR workers the
pool beats the serial executor above roughly 100 ns of work per chunk
and loses below roughly 45 ns, and both campaigns agree on that bracket
despite differing in thread pinning and clock control. The win side
holds at four workers and above; the lose side does not -- ~44 ns loses
at four workers and wins from eight upward -- which is why the chart
plots one width rather than implying a range. The
scaling curve those campaigns were run to produce is not publishable --
cross-socket points sit at 8-22% CV even pinned -- so it is not drawn
here. See docs/planning/optimization-log.md.

SVG is emitted by hand rather than through a plotting library because the
repository has no plotting dependency and its one existing generated
figure (tools/benchmark_trends.py) is hand-rolled the same way. Adding
matplotlib to draw seven points would be the larger change.

The output is theme-aware: docs/ ships a dark mode, and a figure with a
baked-in white background becomes an unreadable slab in it.
"""

from __future__ import annotations

import argparse
import json
import math
import statistics
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any

WIDTH = 720
HEIGHT = 420
LEFT = 64
RIGHT = 700
TOP = 30
BOTTOM = 340

# Series colours chosen to stay distinguishable in both themes and when
# printed greyscale (different lightness, not just different hue).
PALETTE = ("#2a6f97", "#bc4b51")


class ChartError(Exception):
  """Input error that should be reported without a traceback."""


@dataclass
class Series:
  label: str
  points: list[tuple[float, float]]


def log_x(
  value: float, lo: float, hi: float, left: float, right: float
) -> float:
  """Map `value` onto [left, right] logarithmically.

  Log because the work axis spans 11 ns to 6.5 us; on a linear axis every
  point below 300 ns collapses onto the left edge, which is precisely
  the region the crossover lives in.
  """
  if value <= 0 or lo <= 0 or hi <= 0:
    raise ChartError(f"log axis needs positive values, got {value}")
  if hi <= lo:
    raise ChartError("log axis needs hi > lo")
  span = math.log10(hi) - math.log10(lo)
  return left + (math.log10(value) - math.log10(lo)) / span * (right - left)


def lin_y(
  value: float, lo: float, hi: float, top: float, bottom: float
) -> float:
  if hi <= lo:
    raise ChartError("linear axis needs hi > lo")
  return bottom - (value - lo) / (hi - lo) * (bottom - top)


def _escape(text: str) -> str:
  return (
    str(text).replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
  )


def points_from_sweep(
  path: Path, workers: int, chunks: int = 4096
) -> list[tuple[float, float]]:
  """(ns per chunk, speedup) for each workload at `workers`."""
  try:
    blob: Any = json.loads(Path(path).read_text(encoding="utf-8"))
  except (OSError, json.JSONDecodeError) as error:
    raise ChartError(f"cannot read {path}: {error}") from error
  # Grouped by the process each record came from, exactly as
  # thread_scaling_report does. A pinned campaign runs one process per
  # point and each re-measures its own serial baseline; pooling those
  # baselines would divide a width-4 pool median by serial runs from all
  # eleven processes, reintroducing the process confounding the paired
  # runs exist to remove. Untagged artifacts fall back to one group,
  # which is correct for a single-process run.
  groups: dict[str, dict[str, list[float]]] = {}
  for entry in blob.get("benchmarks", []):
    if not isinstance(entry, dict) or entry.get("run_type") != "iteration":
      continue
    name = str(entry.get("name", ""))
    if not name.startswith("lab/thread_scaling/"):
      continue
    stem = name[len("lab/thread_scaling/") :].removesuffix("/real_time")
    group = str(entry.get("tess_run_group", "") or "<single>")
    groups.setdefault(group, {}).setdefault(stem, []).append(
      float(entry.get("real_time", 0.0))
    )

  points: list[tuple[float, float]] = []
  for group in sorted(groups):
    samples = groups[group]
    for key in sorted(samples):
      workload, _, tail = key.rpartition("/")
      if tail != "serial":
        continue
      pool = samples.get(f"{workload}/{workers}")
      if not pool:
        continue
      serial = statistics.median(samples[key])
      if not serial:
        continue
      points.append((serial / chunks, serial / statistics.median(pool)))
  return sorted(points)


def render(series: list[Series], band: tuple[float, float], title: str) -> str:
  if not series or not any(s.points for s in series):
    raise ChartError("nothing to plot")

  xs = [x for s in series for x, _ in s.points]
  ys = [y for s in series for _, y in s.points]
  x_lo = 10 ** math.floor(math.log10(min(xs)))
  x_hi = 10 ** math.ceil(math.log10(max(xs)))
  # A single point, or points inside one decade, would otherwise give a
  # zero-width axis and a division by zero.
  if x_hi <= x_lo:
    x_hi = x_lo * 10
  y_lo, y_hi = 0.0, max(3.5, math.ceil(max(ys) * 2) / 2)

  parts: list[str] = [
    f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {WIDTH} {HEIGHT}" '
    f'width="{WIDTH}" height="{HEIGHT}" role="img" '
    f'aria-label="{_escape(title)}">',
    "<style>",
    ".fg{fill:#1b1b1b}.axis{stroke:#8a8a8a;stroke-width:1}",
    ".grid{stroke:#d8d8d8;stroke-width:1}",
    ".unity{stroke:#1b1b1b;stroke-width:1.5;stroke-dasharray:5 4}",
    ".band{fill:#f0a202;opacity:0.16}",
    "text{font-family:system-ui,-apple-system,Segoe UI,sans-serif;"
    "font-size:12px}",
    "@media (prefers-color-scheme: dark){"
    ".fg{fill:#e8e8e8}.axis{stroke:#7d7d7d}.grid{stroke:#333}"
    ".unity{stroke:#e8e8e8}}",
    "</style>",
  ]

  # The crossover band, drawn first so the data sits on top of it.
  bx1 = log_x(band[0], x_lo, x_hi, LEFT, RIGHT)
  bx2 = log_x(band[1], x_lo, x_hi, LEFT, RIGHT)
  parts.append(
    f'<rect class="band" x="{bx1:.1f}" y="{TOP}" width="{bx2 - bx1:.1f}" '
    f'height="{BOTTOM - TOP}"/>'
  )

  # Horizontal gridlines and the y axis.
  step = 0.5
  value = y_lo
  while value <= y_hi + 1e-9:
    y = lin_y(value, y_lo, y_hi, TOP, BOTTOM)
    parts.append(
      f'<line class="grid" x1="{LEFT}" y1="{y:.1f}" x2="{RIGHT}" y2="{y:.1f}"/>'
    )
    parts.append(
      f'<text class="fg" x="{LEFT - 10}" y="{y + 4:.1f}" '
      f'text-anchor="end">{value:.1f}x</text>'
    )
    value += step

  # Decade ticks on the log x axis.
  decade = x_lo
  while decade <= x_hi + 1e-9:
    x = log_x(decade, x_lo, x_hi, LEFT, RIGHT)
    parts.append(
      f'<line class="grid" x1="{x:.1f}" y1="{TOP}" x2="{x:.1f}" y2="{BOTTOM}"/>'
    )
    label = f"{decade:g}" if decade < 1000 else f"{decade / 1000:g}k"
    parts.append(
      f'<text class="fg" x="{x:.1f}" y="{BOTTOM + 18:.1f}" '
      f'text-anchor="middle">{label}</text>'
    )
    decade *= 10

  # Unity: below it the pool is losing. The whole chart exists to show
  # where the data crosses this line.
  uy = lin_y(1.0, y_lo, y_hi, TOP, BOTTOM)
  parts.append(
    f'<line class="unity" x1="{LEFT}" y1="{uy:.1f}" x2="{RIGHT}" '
    f'y2="{uy:.1f}"/>'
  )
  parts.append(
    f'<text class="fg" x="{RIGHT - 4}" y="{uy - 6:.1f}" text-anchor="end">'
    "serial and pool equal</text>"
  )

  for index, entry in enumerate(series):
    colour = PALETTE[index % len(PALETTE)]
    coords = [
      (
        log_x(x, x_lo, x_hi, LEFT, RIGHT),
        lin_y(min(y, y_hi), y_lo, y_hi, TOP, BOTTOM),
      )
      for x, y in sorted(entry.points)
    ]
    path = " ".join(
      f"{'M' if i == 0 else 'L'}{x:.1f},{y:.1f}"
      for i, (x, y) in enumerate(coords)
    )
    parts.append(
      f'<path d="{path}" fill="none" stroke="{colour}" stroke-width="2"/>'
    )
    for x, y in coords:
      parts.append(
        f'<circle class="pt" cx="{x:.1f}" cy="{y:.1f}" r="4" fill="{colour}"/>'
      )
    ly = TOP + 16 + index * 18
    parts.append(
      f'<circle cx="{LEFT + 12}" cy="{ly - 4:.1f}" r="4" fill="{colour}"/>'
    )
    parts.append(
      f'<text class="fg" x="{LEFT + 24}" y="{ly:.1f}">'
      f"{_escape(entry.label)}</text>"
    )

  parts.append(
    f'<line class="axis" x1="{LEFT}" y1="{BOTTOM}" x2="{RIGHT}" y2="{BOTTOM}"/>'
  )
  parts.append(
    f'<line class="axis" x1="{LEFT}" y1="{TOP}" x2="{LEFT}" y2="{BOTTOM}"/>'
  )
  parts.append(
    f'<text class="fg" x="{(LEFT + RIGHT) / 2:.0f}" y="{BOTTOM + 40}" '
    'text-anchor="middle">work per chunk (nanoseconds, log scale)</text>'
  )
  parts.append(
    f'<text class="fg" x="16" y="{(TOP + BOTTOM) / 2:.0f}" '
    f'transform="rotate(-90 16 {(TOP + BOTTOM) / 2:.0f})" '
    'text-anchor="middle">speedup over serial</text>'
  )
  parts.append(
    f'<text class="fg" x="{(LEFT + RIGHT) / 2:.0f}" y="{HEIGHT - 12}" '
    'text-anchor="middle" opacity="0.75">'
    f"{_escape(title)}</text>"
  )
  parts.append("</svg>")
  return "\n".join(parts)


def main(argv: list[str] | None = None) -> int:
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument(
    "--sweep", action="append", required=True, metavar="LABEL=PATH"
  )
  parser.add_argument("--workers", type=int, default=4)
  parser.add_argument("--chunks", type=int, default=4096)
  parser.add_argument("--band", default="45,95")
  parser.add_argument("--title", required=True)
  parser.add_argument("--out", type=Path, required=True)
  args = parser.parse_args(argv)

  try:
    series = []
    for spec in args.sweep:
      label, _, path = spec.partition("=")
      if not path:
        raise ChartError(f"expected LABEL=PATH, got {spec!r}")
      series.append(
        Series(
          label=label,
          points=points_from_sweep(Path(path), args.workers, args.chunks),
        )
      )
    lo, _, hi = args.band.partition(",")
    svg = render(series, (float(lo), float(hi)), args.title)
  except ChartError as error:
    print(f"thread_scaling_chart: {error}", file=sys.stderr)
    return 1
  args.out.write_text(svg, encoding="utf-8")
  print(f"wrote {args.out}")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
