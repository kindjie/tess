"""Tests for the thread-scaling CPU pinning plan."""

from __future__ import annotations

import sys
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "tools" / "cloud"))

import sweep_cpu_plan  # noqa: E402


# The real c3-standard-192-metal layout, from the campaign's machine.txt:
# 2 sockets x 48 cores x 2 threads; CPUs 0-95 are one thread per physical
# core and 96-191 are their siblings. NUMA node0 = 0-23,96-119 and so on.
def _metal_topology():
  lines = []
  for cpu in range(192):
    core = cpu % 96
    node = core // 24
    socket = core // 48
    lines.append(f"{cpu},{core},{socket},{node}")
  return "\n".join(lines)


def _plan(width, topology=None):
  cpus = sweep_cpu_plan.parse_topology(topology or _metal_topology())
  return sweep_cpu_plan.cpus_for_width(cpus, width)


def test_parses_the_metal_topology():
  cpus = sweep_cpu_plan.parse_topology(_metal_topology())
  assert len(cpus) == 192
  assert sweep_cpu_plan.physical_core_count(cpus) == 96


def test_small_widths_use_distinct_physical_cores():
  # The whole point: two workers must not land on one core's two threads.
  for width in (1, 2, 4, 8):
    chosen = _plan(width)
    assert len(chosen) == width
    cpus = sweep_cpu_plan.parse_topology(_metal_topology())
    cores = {cpus[c].core for c in chosen}
    assert len(cores) == width, f"width {width} doubled up on a core"


def test_twenty_four_workers_fit_one_numa_node():
  chosen = _plan(24)
  cpus = sweep_cpu_plan.parse_topology(_metal_topology())
  assert {cpus[c].node for c in chosen} == {0}


def test_forty_eight_workers_fit_one_socket():
  chosen = _plan(48)
  cpus = sweep_cpu_plan.parse_topology(_metal_topology())
  assert {cpus[c].socket for c in chosen} == {0}
  assert {cpus[c].node for c in chosen} == {0, 1}


def test_ninety_six_workers_are_all_physical_cores_no_siblings():
  chosen = _plan(96)
  cpus = sweep_cpu_plan.parse_topology(_metal_topology())
  assert len(chosen) == 96
  assert len({cpus[c].core for c in chosen}) == 96


def test_beyond_physical_cores_starts_using_siblings():
  chosen = _plan(190)
  cpus = sweep_cpu_plan.parse_topology(_metal_topology())
  assert len(chosen) == 190
  # 96 cores fully used, 94 of them doubled up.
  assert len({cpus[c].core for c in chosen}) == 96


def test_siblings_are_added_only_after_every_core_is_used():
  chosen = set(_plan(97))
  cpus = sweep_cpu_plan.parse_topology(_metal_topology())
  singles = [
    c
    for c in chosen
    if cpus[c].core not in {cpus[o].core for o in chosen if o != c}
  ]
  assert len(singles) == 95  # exactly one core doubled


def test_width_beyond_the_machine_is_an_error():
  with pytest.raises(sweep_cpu_plan.PlanError):
    _plan(193)


def test_zero_width_is_an_error():
  with pytest.raises(sweep_cpu_plan.PlanError):
    _plan(0)


def test_plan_is_deterministic():
  assert _plan(48) == _plan(48)


def test_taskset_list_is_compact_and_sorted():
  spec = sweep_cpu_plan.taskset_list(_plan(4))
  assert spec == "0,1,2,3"
  assert sweep_cpu_plan.taskset_list(_plan(1)) == "0"


def test_widths_match_the_benchmark_manifest():
  """The planner must cover every width the sweep registers."""
  sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
  import thread_scaling_report

  cpus = sweep_cpu_plan.parse_topology(_metal_topology())
  for width in thread_scaling_report.EXPECTED_WORKERS:
    assert len(sweep_cpu_plan.cpus_for_width(cpus, width)) == width


def test_a_smaller_machine_still_plans_within_its_cores():
  # 4 CPUs, 2 physical cores, no NUMA -- a validation VM.
  topology = "\n".join(f"{c},{c % 2},0,0" for c in range(4))
  assert len(_plan(2, topology)) == 2
  cpus = sweep_cpu_plan.parse_topology(topology)
  assert len({cpus[c].core for c in _plan(2, topology)}) == 2


# --- The dispatcher needs a CPU of its own --------------------------
#
# Measured on c3-standard-192-metal 2026-08-04: pinning N workers to
# exactly N CPUs puts the pool's N workers AND the dispatching thread on
# N CPUs, and the result drops into a distinct slow mode -- 65%
# efficiency at width 2 against 100% with one more CPU, and CV 5.91%
# against 0.11%. It was always slow for adjacent cores {0,1} and
# intermittently slow for masks spanning a node or socket (3/10 and 2/10
# repetitions), which is a mode mixture rather than a level shift.


def _mask(width, topology=None):
  cpus = sweep_cpu_plan.parse_topology(topology or _metal_topology())
  return sweep_cpu_plan.mask_for_width(cpus, width)


def test_mask_gives_the_dispatcher_its_own_cpu():
  for width in (1, 2, 4, 24, 48, 96):
    assert len(_mask(width)) == width + 1


def test_mask_contains_every_worker_cpu():
  for width in (1, 2, 4, 24):
    workers = set(_plan(width))
    assert workers <= set(_mask(width))


def test_dispatcher_cpu_is_not_a_worker_cpu():
  for width in (1, 2, 4, 24):
    workers = _plan(width)
    extra = [c for c in _mask(width) if c not in workers]
    assert len(extra) == 1


def test_dispatcher_does_not_drag_the_mask_into_another_numa_node():
  """Width 24 is meant to be exactly one NUMA node.

  Taking the next physical core as the dispatcher's would move the mask
  into node 1 and quietly destroy the topological meaning of the width.
  The SMT sibling of a worker core is in the same node, so it is used
  instead.
  """
  cpus = sweep_cpu_plan.parse_topology(_metal_topology())
  for width in (24, 48):
    nodes = {cpus[c].node for c in _mask(width)}
    worker_nodes = {cpus[c].node for c in _plan(width)}
    assert nodes == worker_nodes


def test_dispatcher_prefers_a_sibling_of_a_worker_core():
  # Width 2 workers are cores 0 and 1 (CPUs 0,1); sibling(0) is 96.
  assert sorted(_mask(2)) == [0, 1, 96]


def test_widest_point_still_fits_the_machine():
  mask = _mask(190)
  assert len(mask) == 191
  assert len(set(mask)) == 191


def test_mask_beyond_the_machine_is_an_error():
  with pytest.raises(sweep_cpu_plan.PlanError):
    _mask(192)


def test_mask_is_deterministic():
  assert _mask(48) == _mask(48)


def test_small_machine_mask_still_works():
  # 4 CPUs, 2 physical cores: width 1 leaves room for a dispatcher.
  topology = "\n".join(f"{c},{c % 2},0,0" for c in range(4))
  assert len(_mask(1, topology)) == 2


def test_diagnostic_reports_against_the_ceiling_not_the_width():
  """The diagnostic script computed efficiency as speedup/width.

  The pool's quantization ceiling at 24 workers is 19.5, so a genuine
  96% result was reported as 78% -- the same arithmetic error the
  optimization log records as corrected, still living in the script.
  """
  script = (REPO / "tools" / "cloud" / "diagnose_pool_width.sh").read_text()
  assert "serial / pool / w))" not in script, (
    "diagnostic divides by width instead of the quantization ceiling"
  )
  assert "ceiling = 4096 / (-(-runs // w) * stride)" in script


def test_diagnostic_ceiling_matches_the_report_tool():
  """One formula, two implementations -- they must agree."""
  import sys

  sys.path.insert(0, str(REPO / "tools"))
  import thread_scaling_report

  def shell_formula(w, chunks=4096):
    stride = max(1, chunks // (w * 4))
    runs = -(-chunks // stride)
    return chunks / (-(-runs // w) * stride)

  for w in (1, 2, 4, 8, 16, 24, 32, 48, 64, 96, 190):
    assert shell_formula(w) == pytest.approx(
      thread_scaling_report.quantization_ceiling(4096, w)
    ), w


# --- the paired-validation verdict ------------------------------------
#
# `--diagnostic=validate` is the negative control for the N+1 mask fix,
# and it used to decide success purely from command exit status. Every
# benchmark invocation can succeed while the run proves nothing: if the
# mask never reached `taskset`, both arms measure the same thing, both
# look clean, and the run reports a pass. These cover the verdict that
# now stands between that and a green run.

import diagnostic_verdict  # noqa: E402


# The arms actually measured on c3-standard-192-metal, as fractions of
# the quantization ceiling. Note width 24: the exactly-N mask does NOT
# degrade there, so a rule demanding a gap at every width would fail a
# correct run on real hardware.
OBSERVED = {
  "fixed_w2": 0.99, "degraded_w2": 0.65,
  "fixed_w4": 0.99, "degraded_w4": 0.69,
  "fixed_w8": 0.98, "degraded_w8": 0.80,
  "fixed_w16": 0.96, "degraded_w16": 0.79,
  "fixed_w24": 0.96, "degraded_w24": 0.97,
}


def test_verdict_passes_the_run_that_validated_the_fix():
  passed, lines = diagnostic_verdict.verdict(OBSERVED)
  assert passed, lines


def test_verdict_tolerates_a_width_where_the_control_does_not_fire():
  # Width 24 alone has no gap, and that is a real property of the
  # hardware rather than a broken run -- but it must not be the ONLY
  # width, or nothing was controlled.
  passed, _ = diagnostic_verdict.verdict(
    {"fixed_w24": 0.96, "degraded_w24": 0.97}
  )
  assert not passed


def test_verdict_fails_when_the_mask_never_reached_taskset():
  """The defect the negative control exists to catch.

  If `taskset` silently ignored the CPU list, both arms measure the
  same machine and both come back clean. Exit status cannot see it.
  """
  both_clean = {
    label: 0.98 if label.startswith("fixed") else 0.97 for label in OBSERVED
  }
  passed, lines = diagnostic_verdict.verdict(both_clean)
  assert not passed
  assert any("negative control" in line for line in lines)


def test_verdict_fails_when_the_fixed_arm_does_not_recover():
  degraded_fix = dict(OBSERVED)
  degraded_fix["fixed_w16"] = 0.72
  passed, lines = diagnostic_verdict.verdict(degraded_fix)
  assert not passed
  assert any("w16" in line and "FAIL" in line for line in lines)


def test_verdict_fails_on_a_missing_arm():
  # A width whose benchmark died leaves one arm behind; the remaining
  # arm is not a comparison.
  half = {k: v for k, v in OBSERVED.items() if k != "degraded_w8"}
  passed, lines = diagnostic_verdict.verdict(half)
  assert not passed
  assert any("degraded" in line and "missing" in line for line in lines)


def test_verdict_fails_when_nothing_was_measured():
  passed, lines = diagnostic_verdict.verdict({})
  assert not passed
  assert lines


def test_verdict_ignores_labels_without_a_width():
  # Mask-survey labels (`two_cores`, `smt_pair`) carry no `_wN`, and
  # must not be mistaken for a paired arm.
  passed, _ = diagnostic_verdict.verdict({"two_cores": 0.99, "smt_pair": 0.65})
  assert not passed


def test_diagnostic_script_requires_the_paired_serial_row():
  """A filter matching nothing exits zero, for serial as for pool."""
  script = (REPO / "tools" / "cloud" / "diagnose_pool_width.sh").read_text()
  assert 'for row in "serial" "${width}"' in script


def test_diagnostic_script_fails_on_an_unplannable_width():
  """`mapfile` from a process substitution reports success regardless.

  A width the planner cannot fit used to drop out of the array without
  a word, and the run went on to validate whichever widths remained.
  """
  script = (REPO / "tools" / "cloud" / "diagnose_pool_width.sh").read_text()
  assert "mapfile -t MASKS < <(\n" not in script
  assert "FATAL: cannot plan the fixed mask for width" in script
  assert "FATAL: planner returned an empty mask for width" in script


def test_diagnostic_script_fails_the_run_on_a_failed_verdict():
  script = (REPO / "tools" / "cloud" / "diagnose_pool_width.sh").read_text()
  assert "REPORT_STATUS=$?" in script
  assert "if (( REPORT_STATUS != 0 )); then" in script
