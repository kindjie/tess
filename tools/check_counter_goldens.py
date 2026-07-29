#!/usr/bin/env python3
"""Compare observed deterministic work counters against committed goldens.

The redesign's counter-golden gate (section 3.3) in its shadow phase:
drift is printed loudly and written to a report file, but the exit code
stays zero unless strict mode is requested (the phase 4 promotion flips
CI to strict). ``--update`` rewrites the golden from the observed run —
the documented intentional-change workflow, committed in the same PR.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path
from typing import Any


def load(path: Path) -> dict[str, Any]:
  """Load a counter JSON document, failing closed on malformed input."""
  try:
    data = json.loads(Path(path).read_text(encoding="utf-8"))
  except (OSError, json.JSONDecodeError) as error:
    raise SystemExit(f"error: cannot read {path}: {error}") from error
  if not isinstance(data, dict) or "workloads" not in data:
    raise SystemExit(f"error: {path} is not a counter document")
  return data


def diff_counters(
  golden: dict[str, Any],
  observed: dict[str, Any],
) -> list[tuple[str, str, str, object, object]]:
  """Return (workload, family, counter, golden, observed) drift rows.

  Missing and unexpected workloads, families, and counters are drift:
  a probe that silently stops emitting a counter must not pass.
  """
  rows = []
  golden_workloads = golden["workloads"]
  observed_workloads = observed["workloads"]
  for workload in sorted(set(golden_workloads) | set(observed_workloads)):
    golden_families = golden_workloads.get(workload)
    observed_families = observed_workloads.get(workload)
    if golden_families is None or observed_families is None:
      rows.append((workload, "-", "-", golden_families is not None,
                   observed_families is not None))
      continue
    for family in sorted(set(golden_families) | set(observed_families)):
      golden_counters = golden_families.get(family)
      observed_counters = observed_families.get(family)
      if golden_counters is None or observed_counters is None:
        rows.append((workload, family, "-", golden_counters is not None,
                     observed_counters is not None))
        continue
      for counter in sorted(set(golden_counters) | set(observed_counters)):
        golden_value = golden_counters.get(counter)
        observed_value = observed_counters.get(counter)
        if golden_value != observed_value:
          rows.append(
            (workload, family, counter, golden_value, observed_value)
          )
  return rows


def render_report(
  rows: list[tuple[str, str, str, object, object]],
) -> str:
  """Render the drift table shown in the step summary."""
  lines = [
    "### Counter golden drift (shadow mode)",
    "",
    "Deterministic work counters diverged from"
    " `tests/goldens/counters.json`. If the behavior change is"
    " intentional, regenerate the golden and commit it in this PR:"
    " run the probe, then"
    " `tools/check_counter_goldens.py --observed <file> --golden"
    " tests/goldens/counters.json --update`.",
    "",
    "| Workload | Family | Counter | Golden | Observed |",
    "| --- | --- | --- | --- | --- |",
  ]
  for workload, family, counter, golden_value, observed_value in rows:
    lines.append(
      f"| {workload} | {family} | {counter} "
      f"| {golden_value} | {observed_value} |"
    )
  return "\n".join(lines) + "\n"


def main(argv: list[str] | None = None) -> int:
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("--observed", required=True, type=Path)
  parser.add_argument("--golden", required=True, type=Path)
  parser.add_argument("--drift-report", type=Path)
  parser.add_argument(
    "--update",
    action="store_true",
    help="rewrite the golden from the observed run",
  )
  parser.add_argument(
    "--strict",
    action="store_true",
    help="fail on drift (also enabled by TESS_COUNTER_GOLDENS_STRICT=1)",
  )
  args = parser.parse_args(argv)

  observed = load(args.observed)
  if args.update:
    args.golden.write_text(
      json.dumps(observed, indent=2, sort_keys=True) + "\n",
      encoding="utf-8",
    )
    print(f"golden updated from {args.observed}")
    return 0

  golden = load(args.golden)
  rows = diff_counters(golden, observed)
  if not rows:
    print("counter goldens match")
    return 0

  report = render_report(rows)
  print(report)
  if args.drift_report:
    args.drift_report.write_text(report, encoding="utf-8")
  strict = args.strict or (
    os.environ.get("TESS_COUNTER_GOLDENS_STRICT", "") == "1"
  )
  if strict:
    print("error: counter goldens drifted (strict mode)", file=sys.stderr)
    return 1
  print("shadow mode: drift reported, not gated")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
