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
# prefix and are kept. threads:N is deliberately NOT stripped: it
# changes concurrency, so a benchmark using it must be classified
# with its executor dimensions rather than collapsed.
CONTROL_SUFFIX = re.compile(
  r"/(iterations:\d+|repeats:\d+|manual_time|real_time|process_time)$"
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


LINE_COMMENT = re.compile(r"//[^\n]*")
BLOCK_COMMENT = re.compile(r"/\*.*?\*/", re.DOTALL)


def lab_registrations(bench_sources: Path) -> set[str]:
  """lab/ name literals from active code (comments stripped)."""
  names: set[str] = set()
  for source in sorted(Path(bench_sources).glob("*.cc")):
    text = source.read_text(encoding="utf-8")
    text = BLOCK_COMMENT.sub("", text)
    text = LINE_COMMENT.sub("", text)
    names.update(LAB_LITERAL.findall(text))
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
  for dimension, group in rule.get("captures", {}).items():
    value = match.group(group)
    if value is not None:
      cell[dimension] = value
  overrides = rule.get("overrides", {}).get(name)
  if overrides:
    cell.update(overrides)
  cell["family"] = rule["family"]

  # Value-based token consumption: a dimension token in the name is
  # consumed only when the generated cell actually AGREES with it —
  # an extent must appear in world_extent, chunk_extent, or payload
  # (substring, so 512x512 names a 1x512x512 vertical world), and an
  # executor token must match BOTH executor_kind and worker_count.
  # Partial captures or defaults that contradict the name fail.
  for token in EXTENT_TOKEN.finditer(name):
    value = token.group(0)
    if not any(
      value in str(cell.get(dimension, ""))
      for dimension in ("world_extent", "chunk_extent", "payload")
    ):
      errors.append(
        f"{rule['family']}: extent token {value!r} in {name!r} is "
        "not reflected in world_extent, chunk_extent, or payload"
      )
  for token in EXECUTOR_TOKEN.finditer(name):
    kind, width = token.group(0).rsplit("_w", 1)
    if (
      cell.get("executor_kind") != kind
      or str(cell.get("worker_count")) != width
    ):
      errors.append(
        f"{rule['family']}: executor token {token.group(0)!r} in "
        f"{name!r} contradicts executor_kind="
        f"{cell.get('executor_kind')!r} worker_count="
        f"{cell.get('worker_count')!r}"
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


def _validate_rules(
  rules: list[dict[str, Any]], universe: set[str], errors: list[str]
) -> None:
  """Catalog-shape validation: fail loudly, never with a traceback."""
  seen_families: set[str] = set()
  for rule in rules:
    family = rule.get("family")
    if not family or "pattern" not in rule or "defaults" not in rule:
      errors.append(
        f"family rule {family!r} is missing family/pattern/defaults"
      )
      continue
    if family in seen_families:
      errors.append(
        f"family rule {family!r} is declared more than once"
      )
      continue
    seen_families.add(family)
    pattern = rule["pattern"]
    if not pattern.startswith("^") or not pattern.endswith("$"):
      errors.append(
        f"family rule {family!r}: pattern must be anchored with "
        "^ and $"
      )
    try:
      compiled = re.compile(pattern)
    except re.error as error:
      errors.append(f"family rule {family!r}: bad pattern: {error}")
      continue
    for dimension, group in rule.get("captures", {}).items():
      if dimension not in DIMENSIONS:
        errors.append(
          f"family rule {family!r}: capture key {dimension!r} is not "
          f"a dimension (misspelling?)"
        )
      if not isinstance(group, int) or group > compiled.groups:
        errors.append(
          f"family rule {family!r}: capture for {dimension} names "
          f"group {group} but the pattern has {compiled.groups}"
        )
    canonical_universe = {canonical(raw) for raw in universe}
    for key in rule.get("overrides", {}):
      if not compiled.search(key):
        errors.append(
          f"family rule {family!r}: override key {key!r} does not "
          "match the rule pattern — stale after a rename?"
        )
      elif key not in canonical_universe:
        errors.append(
          f"family rule {family!r}: override key {key!r} matches no "
          "registration — stale after a removal?"
        )


def check(catalog: dict[str, Any], universe: set[str]) -> list[str]:
  """All drift findings for the catalog against the universe."""
  errors: list[str] = []
  vocabularies = catalog.get("vocabularies", {})
  canonical_names = sorted({canonical(raw) for raw in universe})

  rules = catalog.get("families", [])
  _validate_rules(rules, universe, errors)
  if errors:
    return errors
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
    known_families = {rule["family"] for rule in rules}
    unknown_dims = [
      k for k in selector if k not in DIMENSIONS and k != "family"
    ]
    if unknown_dims:
      errors.append(
        f"unmeasured selector {selector} uses unknown dimension(s) "
        f"{unknown_dims}"
      )
      continue
    bad_values = [
      (k, v) for k, v in selector.items()
      if (k in VOCABULARY_DIMENSIONS and v not in vocabularies.get(k, []))
      or (k == "family" and v not in known_families)
    ]
    if bad_values:
      errors.append(
        f"unmeasured selector {selector} uses value(s) outside the "
        f"vocabulary/family list: {bad_values} — a typo here can "
        "never trigger retirement"
      )
      continue
    policy = entry.get("policy", False)
    if not isinstance(policy, bool):
      errors.append(
        f"unmeasured selector {selector}: policy must be a boolean"
      )
      continue
    if policy:
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
  except (KeyError, TypeError, AttributeError) as error:
    print(
      f"error: malformed catalog structure: {error!r}", file=sys.stderr
    )
    return 1
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
