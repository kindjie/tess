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


SCHEMA_VERSION = 1


def _is_counter_value(value: object) -> bool:
  # Counters are unsigned integers; bool is an int subclass and JSON
  # true would otherwise compare equal to 1, so reject it explicitly.
  return isinstance(value, int) and not isinstance(value, bool) and value >= 0


def load(path: Path) -> dict[str, Any]:
  """Load and structurally validate a counter JSON document.

  Fails closed on anything that is not exactly the probe's shape:
  schema pin, string-keyed workload and family mappings, and
  nonnegative integer counters (bool rejected).
  """
  try:
    data = json.loads(Path(path).read_text(encoding="utf-8"))
  except (OSError, json.JSONDecodeError) as error:
    raise SystemExit(f"error: cannot read {path}: {error}") from error

  def invalid(reason: str) -> SystemExit:
    return SystemExit(f"error: {path} is not a counter document: {reason}")

  if not isinstance(data, dict):
    raise invalid("root is not an object")
  schema = data.get("schema")
  if isinstance(schema, bool) or schema != SCHEMA_VERSION:
    raise invalid(f"schema {schema!r} != {SCHEMA_VERSION}")
  workloads = data.get("workloads")
  if not isinstance(workloads, dict):
    raise invalid("workloads is not an object")
  for workload, families in workloads.items():
    if not isinstance(workload, str) or not isinstance(families, dict):
      raise invalid(f"workload {workload!r} is not an object")
    for family, counters in families.items():
      if not isinstance(family, str) or not isinstance(counters, dict):
        raise invalid(f"family {workload}/{family!r} is not an object")
      for counter, value in counters.items():
        if not isinstance(counter, str) or not _is_counter_value(value):
          raise invalid(
            f"counter {workload}/{family}/{counter!r} is not a "
            "nonnegative integer"
          )
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
  *,
  strict: bool = False,
) -> str:
  """Render the drift table shown in the step summary."""
  mode = "strict mode" if strict else "shadow mode"
  lines = [
    f"### Counter golden drift ({mode})",
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
  def cell(value: object) -> str:
    return str(value).replace("|", "\\|")

  for workload, family, counter, golden_value, observed_value in rows:
    lines.append(
      f"| {cell(workload)} | {cell(family)} | {cell(counter)} "
      f"| {cell(golden_value)} | {cell(observed_value)} |"
    )
  # Structural rows (missing/extra workloads, families, or counters —
  # non-integer golden/observed cells) are instrumentation or schema
  # drift, not evidence the algorithm did different work.
  structural = any(
    not _is_counter_value(golden_value)
    or not _is_counter_value(observed_value)
    for _, _, _, golden_value, observed_value in rows
  )
  numeric = any(
    _is_counter_value(golden_value) and _is_counter_value(observed_value)
    for _, _, _, golden_value, observed_value in rows
  )
  lines.append("")
  if numeric:
    lines.append(
      "Changed counter values are step 1 of the [profiling protocol]"
      "(https://github.com/kindjie/tess/blob/main/CONTRIBUTING.md):"
      " work changed, so the diagnosis is algorithmic — decide whether"
      " the new work is intended before reaching for a profiler."
    )
  if structural:
    lines.append(
      "Rows with missing or extra workloads/families/counters are"
      " instrumentation or schema drift, not changed algorithmic work"
      " — fix the probe/golden pairing rather than investigating"
      " performance."
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
    if args.drift_report:
      # A stale report from an earlier drifted run must not outlive
      # the run that resolved it (multi-config builds share the path).
      args.drift_report.unlink(missing_ok=True)
    print("counter goldens match")
    return 0

  strict = args.strict or (
    os.environ.get("TESS_COUNTER_GOLDENS_STRICT", "") == "1"
  )
  report = render_report(rows, strict=strict)
  print(report)
  if args.drift_report:
    args.drift_report.write_text(report, encoding="utf-8")
  if strict:
    print("error: counter goldens drifted (strict mode)", file=sys.stderr)
    return 1
  print("shadow mode: drift reported, not gated")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
