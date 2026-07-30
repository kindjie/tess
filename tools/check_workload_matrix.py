#!/usr/bin/env python3
"""Benchmark workload-matrix drift checker (redesign section 4.5).

The catalog (bench/workload-matrix.json) declares operation-level
family rules: an anchored regex grammar, capture-group-to-dimension
mappings, family defaults, and per-name overrides. Measured workload
cells are GENERATED from that classification rather than hand-listed,
so the curated surface stays small while drift stays loud:

- every registration must match exactly one family rule;
- every rule must match at least one registration;
- dimension tokens in a name (extents, executor widths) must be
  consumed by a capture or an explicit override — never silently
  shadowed by a default;
- dimension values must come from the declared vocabularies
  (``unknown`` is an error: annotate the real value or mark the
  dimension ``not_applicable``);
- structured ``unmeasured`` selectors must not match any generated
  measured cell — when the gap fills, the entry must retire —
  except open-ended ``policy: true`` statements;
- ``composite`` registrations (one timing over several conflated
  configurations) never count as measured cells.

Registration universes: threshold manifests plus ``lab/`` string
literals from the bench sources (static, hooks tier), and optionally
``--registrations`` files produced by ``--benchmark_list_tests``
(runtime authority in the bench job; survives threshold retirement).
Checks run by default; the exit code is nonzero on any finding.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any

DIMENSIONS = (
  "world_extent",
  "chunk_extent",
  "layout",
  "storage",
  "executor_kind",
  "worker_count",
  "payload",
)

# Free-vocabulary dimensions carry annotated prose (payload) or exact
# extents; the rest must use the catalog's declared vocabularies.
VOCABULARY_DIMENSIONS = ("layout", "storage", "executor_kind")

# Google Benchmark control suffixes carry measurement configuration,
# not workload identity. Meaningful workload arguments (for example
# maintenance/flush_budget/256) are plain integers WITH the family
# prefix and are kept.
CONTROL_SUFFIX = re.compile(
  r"/(iterations:\d+|repeats:\d+|manual_time|real_time|process_time"
  r"|threads:\d+)$"
)

# Tokens in a registration name that assert a dimension and therefore
# must be consumed by a capture group or an explicit per-name
# override: extents (512x512, 1x512x512, 64x64x16) and executor
# widths (pool_w4, scoped_threads_w2).
EXTENT_TOKEN = re.compile(r"\d+x\d+(?:x\d+)?")
EXECUTOR_TOKEN = re.compile(r"(?:pool|scoped_threads)_w\d+")

LAB_LITERAL = re.compile(r'"(lab/[^"]+)"')


class MatrixError(RuntimeError):
  """Raised for unusable inputs (never for drift findings)."""


def _load_json(path: Path) -> dict[str, Any]:
  try:
    with path.open(encoding="utf-8") as handle:
      return json.load(handle)
  except OSError as error:
    raise MatrixError(f"cannot read {path}: {error}") from error
  except json.JSONDecodeError as error:
    raise MatrixError(f"malformed JSON in {path}: {error}") from error


def canonical(name: str) -> str:
  """Workload identity: the name minus control suffixes."""
  while True:
    stripped = CONTROL_SUFFIX.sub("", name)
    if stripped == name:
      return name
    name = stripped


def threshold_registrations(thresholds_dir: Path) -> set[str]:
  names: set[str] = set()
  for manifest in sorted(Path(thresholds_dir).glob("*.json")):
    payload = _load_json(manifest)
    entries = payload.get("benchmarks", payload)
    if isinstance(entries, dict):
      names.update(entries.keys())
    elif isinstance(entries, list):
      for entry in entries:
        if isinstance(entry, dict) and "name" in entry:
          names.add(str(entry["name"]))
  return names


def lab_registrations(bench_sources: Path) -> set[str]:
  names: set[str] = set()
  for source in sorted(Path(bench_sources).glob("*.cc")):
    names.update(LAB_LITERAL.findall(source.read_text(encoding="utf-8")))
  return names


def load_registrations(listings: list[Path]) -> set[str]:
  names: set[str] = set()
  for listing in listings:
    try:
      text = Path(listing).read_text(encoding="utf-8")
    except OSError as error:
      raise MatrixError(
        f"cannot read registrations {listing}: {error}"
      ) from error
    names.update(
      line.strip() for line in text.splitlines() if line.strip()
    )
  return names


def _match_rules(
  catalog: dict[str, Any], name: str
) -> list[dict[str, Any]]:
  matched = []
  for rule in catalog.get("families", []):
    if re.search(rule["pattern"], name):
      matched.append(rule)
  return matched


def _cell_for(
  rule: dict[str, Any], name: str, errors: list[str]
) -> dict[str, str]:
  cell = dict(rule.get("defaults", {}))
  match = re.search(rule["pattern"], name)
  assert match is not None
  captures = rule.get("captures", {})
  consumed_spans = []
  for dimension, group in captures.items():
    value = match.group(group)
    if value is not None:
      cell[dimension] = value
      consumed_spans.append(match.span(group))
  overrides = rule.get("overrides", {}).get(name)
  overridden = set()
  if overrides:
    cell.update(overrides)
    overridden = set(overrides)

  for token_re, dimension_hint in (
    (EXTENT_TOKEN, ("world_extent", "chunk_extent", "payload")),
    (EXECUTOR_TOKEN, ("executor_kind", "worker_count")),
  ):
    for token in token_re.finditer(name):
      start, end = token.span()
      # Overlap suffices: an executor token like pool_w2 spans two
      # capture groups plus the literal connector between them.
      in_capture = any(
        not (ce <= start or end <= cs) for cs, ce in consumed_spans
      )
      in_override = any(hint in overridden for hint in dimension_hint)
      if not in_capture and not in_override:
        errors.append(
          f"{rule['family']}: unconsumed dimension token "
          f"{token.group(0)!r} in {name!r} — capture it or add a "
          "per-name override for the dimension it asserts"
        )
  return cell


def classify(
  catalog: dict[str, Any], universe: set[str]
) -> dict[str, dict[str, str]]:
  """Canonical name -> generated workload cell (best effort)."""
  cells: dict[str, dict[str, str]] = {}
  for name in sorted({canonical(raw) for raw in universe}):
    matched = _match_rules(catalog, name)
    if len(matched) == 1:
      cells[name] = _cell_for(matched[0], name, [])
  return cells


def _selector_matches(
  selector: dict[str, str], cell: dict[str, str]
) -> bool:
  return all(cell.get(k) == v for k, v in selector.items())


def check(catalog: dict[str, Any], universe: set[str]) -> list[str]:
  """All drift findings for the catalog against the universe."""
  errors: list[str] = []
  vocabularies = catalog.get("vocabularies", {})
  canonical_names = sorted({canonical(raw) for raw in universe})

  rules = catalog.get("families", [])
  matched_by_rule: dict[str, int] = {r["family"]: 0 for r in rules}
  measured_cells: list[tuple[str, dict[str, str]]] = []

  for name in canonical_names:
    matched = _match_rules(catalog, name)
    if not matched:
      errors.append(f"{name!r}: no family rule classifies it")
      continue
    if len(matched) > 1:
      families = ", ".join(r["family"] for r in matched)
      errors.append(
        f"{name!r}: must match exactly one family rule, matched "
        f"{families}"
      )
      continue
    rule = matched[0]
    matched_by_rule[rule["family"]] += 1
    cell = _cell_for(rule, name, errors)

    for dimension in DIMENSIONS:
      value = cell.get(dimension)
      if value is None or value == "unknown":
        errors.append(
          f"{rule['family']}: {name!r} has unknown {dimension} — "
          "annotate the real value or not_applicable"
        )
      elif dimension in VOCABULARY_DIMENSIONS:
        allowed = vocabularies.get(dimension, [])
        if value not in allowed:
          errors.append(
            f"{rule['family']}: {name!r} {dimension}={value!r} is "
            f"outside the declared vocabulary {allowed}"
          )
    if not rule.get("composite", False):
      measured_cells.append((name, cell))

  for family, count in matched_by_rule.items():
    if count == 0:
      errors.append(
        f"family rule {family!r} matches no registration — remove or "
        "fix it"
      )

  for entry in catalog.get("unmeasured", []):
    selector = entry.get("selector")
    if not isinstance(selector, dict) or not selector:
      errors.append("unmeasured entry without a structured selector")
      continue
    if not entry.get("reason"):
      errors.append(
        f"unmeasured selector {selector} has no reason"
      )
    unknown_dims = [k for k in selector if k not in DIMENSIONS]
    if unknown_dims:
      errors.append(
        f"unmeasured selector {selector} uses unknown dimension(s) "
        f"{unknown_dims}"
      )
      continue
    if entry.get("policy", False):
      continue
    for name, cell in measured_cells:
      if _selector_matches(selector, cell):
        errors.append(
          f"unmeasured selector {selector} matches measured "
          f"registration {name!r} — the gap is filled, retire the "
          "entry"
        )
        break

  return errors


def main(argv: list[str] | None = None) -> int:
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("--catalog", required=True, type=Path)
  parser.add_argument("--thresholds-dir", type=Path)
  parser.add_argument("--bench-sources", type=Path)
  parser.add_argument(
    "--registrations",
    action="append",
    default=[],
    type=Path,
    help="file of newline-separated --benchmark_list_tests output",
  )
  args = parser.parse_args(argv)

  try:
    catalog = _load_json(args.catalog)
    universe: set[str] = set()
    if args.thresholds_dir is not None:
      universe |= threshold_registrations(args.thresholds_dir)
    if args.bench_sources is not None:
      universe |= lab_registrations(args.bench_sources)
    if args.registrations:
      universe |= load_registrations(args.registrations)
    if not universe:
      raise MatrixError("no registration universe provided")
    errors = check(catalog, universe)
  except MatrixError as error:
    print(f"error: {error}", file=sys.stderr)
    return 1

  if errors:
    for finding in errors:
      print(f"workload-matrix: {finding}", file=sys.stderr)
    return 1
  print(
    f"workload-matrix: {len(universe)} registrations coherent with "
    f"{len(catalog.get('families', []))} family rules"
  )
  return 0


if __name__ == "__main__":
  sys.exit(main())
