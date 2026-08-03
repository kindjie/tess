"""Tests for the thread-scaling CPU pinning plan."""

from __future__ import annotations

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools" / "cloud"))

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
