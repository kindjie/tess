"""Cross-pass comparator for budgeted-progress artifacts.

Enforces the design's section 11.2 timing-versus-counter comparison
(docs/planning/budgeted-progress-benchmarks.md): artifacts pair on
(scenario_id, budget_ns, arrival rate, pacing) with hard-equal trace
hashes; the counter artifact must declare pass "counter" and the
timing artifact pass "timing"; and the tolerance set depends on the
realized regime, never on labels alone:

- Saturated cells: both conservation identities must hold in both
  passes, and the work-units-per-completion gate (1% relative)
  applies only when both passes completed at least one full pool wrap
  (below a wrap the ratio measures pool-prefix composition, not
  instrumentation divergence; the exact per-repetition prefix-sum
  identity is enforced inside each binary instead).
- Demand-limited cells (both passes flow_stable): useful completions
  and consumed work units within 5% relative, interactive-class
  deadline success within 2 percentage points.
- Regime divergence (one pass stable, the other not) is reported as a
  finding — evidence that instrumentation moved capacity below the
  arrival rate — not a tolerance failure.

Frame-level data is never compared: the two passes realize different
service schedules by design.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

SATURATED_WORK_PER_COMPLETION_TOLERANCE = 0.01
DEMAND_RELATIVE_TOLERANCE = 0.05
DEADLINE_SUCCESS_TOLERANCE = 0.02


def pair_key(document: dict) -> tuple:
  """The full cell identity a timing/counter pair must share.

  Every identity axis participates: two artifacts that differ in any of
  these fields are different cells, and pairing them would compare
  incomparable work. scenario_id alone happens to disambiguate today's
  cells, but relying on that convention silently breaks the first time
  one scenario spans an axis.
  """
  experiment = document["experiment"]
  return (experiment.get("scenario_id"), experiment.get("kind"),
          experiment.get("movement_tier", "baseline"),
          experiment.get("budget_ns"),
          experiment.get("sim_tps"), experiment.get("population", 0),
          experiment.get("arrival_rate_num", 0),
          experiment.get("arrival_rate_den", 1), experiment.get("pacing"))


def load_documents(directory: Path, expected_pass: str) -> dict:
  """Load cell artifacts of one pass, keyed by full cell identity.

  Duplicate identities are fatal in either direction: silently keeping
  the last file would compare an arbitrary one of the duplicates and
  report success against unverified data.
  """
  documents = {}
  for path in sorted(directory.glob("*.json")):
    document = json.loads(path.read_text(encoding="utf-8"))
    if document.get("schema") != "tess.budgeted_progress.v1":
      continue  # Search summaries and foreign files are not paired.
    declared = document["experiment"].get("pass", "timing")
    if declared != expected_pass:
      continue
    key = pair_key(document)
    if key in documents:
      raise SystemExit(
          f"error: duplicate cell identity in {directory}: "
          f"{documents[key][0].name} and {path.name} share {key}")
    documents[key] = (path, document)
  return documents


def identities_hold(document: dict) -> bool:
  """Both conservation identities, recomputed by the validator's rules."""
  flow = document["flow"]
  admission = flow["offered"] == (flow["admitted"] + flow["rejected"] +
                                  flow["coalesced_into_pending"])
  terminal = sum(flow[key] for key in
                 ("completed", "cancelled", "superseded", "stale", "failed",
                  "dropped_after_admission"))
  retention = flow["admitted"] == terminal + flow["outstanding_current"]
  return admission and retention


def relative_close(lhs: float, rhs: float, tolerance: float) -> bool:
  """Symmetric relative comparison with an absolute floor of one unit."""
  return abs(lhs - rhs) <= max(1.0, tolerance * max(abs(lhs), abs(rhs)))


def compare_pair(timing: dict, counter: dict, findings: list[str],
                 label: str) -> None:
  """Apply the regime-classified section 11.2 tolerance set."""
  if timing["trace"]["sha256"] != counter["trace"]["sha256"]:
    findings.append(f"{label}: trace hashes differ; the passes did not "
                    "consume the identical demand trace")
    return
  for name, document in (("timing", timing), ("counter", counter)):
    if not identities_hold(document):
      findings.append(f"{label}: conservation identities fail in the "
                      f"{name} pass")
      return

  timing_stable = timing["summary"].get("flow_stable")
  counter_stable = counter["summary"].get("flow_stable")
  saturated = timing["experiment"].get("kind") == "isolated_saturated"

  if saturated:
    pool = timing["experiment"].get("pool_size", 0)
    if pool > 0:
      # Every repetition of both passes must individually wrap the
      # pool: an aggregate threshold would let one long repetition
      # mask several that never wrapped.
      timing_wrapped = timing["summary"].get(
          "min_repetition_completions", 0) >= pool
      counter_wrapped = counter["summary"].get(
          "min_repetition_completions", 0) >= pool
      if not (timing_wrapped and counter_wrapped):
        return  # Below a full wrap the ratio is composition noise.
    timing_wpc = (timing["summary"]["consumed_work_units"] /
                  max(1, timing["summary"]["useful_completions"]))
    counter_wpc = (counter["summary"]["consumed_work_units"] /
                   max(1, counter["summary"]["useful_completions"]))
    if abs(timing_wpc - counter_wpc) > (
        SATURATED_WORK_PER_COMPLETION_TOLERANCE *
        max(timing_wpc, counter_wpc)):
      findings.append(
          f"{label}: work units per completion diverge beyond 1% "
          f"({timing_wpc:.3f} timing vs {counter_wpc:.3f} counter)")
    return

  # Demand-limited kinds classify by realized regime.
  if timing_stable != counter_stable:
    findings.append(
        f"{label}: REGIME DIVERGENCE — flow_stable {timing_stable} in the "
        f"timing pass vs {counter_stable} in the counter pass "
        "(instrumentation likely moved capacity across the arrival rate); "
        "reported as a finding, not a tolerance failure")
    return
  if not timing_stable:
    return  # Both overloaded: saturated identities already checked above.
  if not relative_close(timing["summary"]["useful_completions"],
                        counter["summary"]["useful_completions"],
                        DEMAND_RELATIVE_TOLERANCE):
    findings.append(f"{label}: useful completions diverge beyond 5%")
  if not relative_close(timing["summary"]["consumed_work_units"],
                        counter["summary"]["consumed_work_units"],
                        DEMAND_RELATIVE_TOLERANCE):
    findings.append(f"{label}: consumed work units diverge beyond 5%")
  timing_success = timing["summary"].get("deadline_success_rate")
  counter_success = counter["summary"].get("deadline_success_rate")
  if (isinstance(timing_success, (int, float))
      and isinstance(counter_success, (int, float))
      and abs(timing_success - counter_success) >
      DEADLINE_SUCCESS_TOLERANCE):
    findings.append(
        f"{label}: deadline success diverges beyond 2 points "
        f"({timing_success:.3f} vs {counter_success:.3f})")
  # The two-point tolerance applies per demand class, not only to the
  # aggregate: multi-class cells retain classes[].deadline_success_rate.
  timing_classes = {entry.get("class_id"): entry
                    for entry in timing.get("classes", [])}
  for entry in counter.get("classes", []):
    partner = timing_classes.get(entry.get("class_id"))
    if partner is None:
      findings.append(f"{label}: class {entry.get('class_id')!r} has no "
                      "timing-pass counterpart")
      continue
    timing_rate = partner.get("deadline_success_rate")
    counter_rate = entry.get("deadline_success_rate")
    if (isinstance(timing_rate, (int, float))
        and isinstance(counter_rate, (int, float))
        and abs(timing_rate - counter_rate) > DEADLINE_SUCCESS_TOLERANCE):
      findings.append(
          f"{label}: class {entry.get('class_id')!r} deadline success "
          f"diverges beyond 2 points ({timing_rate:.3f} vs "
          f"{counter_rate:.3f})")


def run_comparison(timing_dir: Path, counter_dir: Path) -> tuple[int, list]:
  """Pair and compare the two directories; returns (pairs, findings)."""
  timing_documents = load_documents(timing_dir, "timing")
  counter_documents = load_documents(counter_dir, "counter")
  findings: list[str] = []
  pairs = 0
  for key, (counter_path, counter_document) in counter_documents.items():
    if key not in timing_documents:
      findings.append(f"{counter_path.name}: no timing-pass artifact pairs "
                      "with this counter artifact")
      continue
    timing_path, timing_document = timing_documents[key]
    pairs += 1
    compare_pair(timing_document, counter_document, findings,
                 f"{timing_path.name} <> {counter_path.name}")
  if pairs == 0:
    findings.append("no timing/counter pairs found")
  return pairs, findings


def main(argv: list[str] | None = None) -> int:
  """Compare the passes; nonzero when findings exist (unless report-only)."""
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("--timing-dir", type=Path, required=True)
  parser.add_argument("--counter-dir", type=Path, required=True)
  parser.add_argument("--smoke-report-only", action="store_true",
                      help="report statistical findings without failing "
                      "(smoke configurations are composition-noise "
                      "dominated); pairing and identity failures still "
                      "fail")
  arguments = parser.parse_args(argv)
  pairs, findings = run_comparison(arguments.timing_dir,
                                   arguments.counter_dir)
  print(f"compared {pairs} timing/counter pairs")
  hard_markers = ("no timing-pass artifact", "no timing/counter pairs",
                  "trace hashes differ", "identities fail")
  hard_failures = [finding for finding in findings
                   if any(marker in finding for marker in hard_markers)]
  for finding in findings:
    print(f"finding: {finding}")
  if arguments.smoke_report_only:
    return 1 if hard_failures else 0
  return 1 if findings else 0


if __name__ == "__main__":
  sys.exit(main())
