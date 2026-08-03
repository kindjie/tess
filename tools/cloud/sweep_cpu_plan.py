#!/usr/bin/env python3
"""Pick the CPUs to pin each thread-scaling sweep point to.

The 2026-08-03 campaign measured the sweep with no pinning, and thread
placement turned out to dominate everything above about 32 workers: CVs
of 16-33%, and points whose repetitions split into discrete modes rather
than scattering. `chunk_compute/4` sat at either 3.94x (four separate
physical cores) or 3.01x (two of the four workers sharing one core's two
SMT threads) depending on where the kernel happened to put the pool that
repetition. Averaging over that lottery does not produce a scaling curve;
it produces the mean of two different experiments.

The fix is to run each point pinned to a chosen CPU set, which also makes
the worker counts mean what the sweep always claimed they meant. On
c3-standard-192-metal (2 sockets x 48 cores x 2 threads, 4 NUMA nodes,
CPUs 0-95 one per physical core and 96-191 their siblings):

    24 workers -> exactly NUMA node 0
    48 workers -> exactly socket 0
    96 workers -> every physical core, no SMT
   190 workers -> every physical core plus 94 siblings

Unpinned, those are just numbers. Pinned, a knee at 48 is a socket
boundary and a knee at 96 is the onset of SMT.

Policy, in order: one thread per physical core, filling NUMA nodes in
order so that a width which fits a node uses exactly that node; SMT
siblings only once every physical core is occupied. The topology is read
from `lscpu -p` rather than assumed, because the CPU numbering that makes
0-95 the distinct cores is a property of this machine, not a guarantee.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from dataclasses import dataclass


class PlanError(Exception):
  """Input error that should be reported without a traceback."""


@dataclass(frozen=True)
class Cpu:
  cpu: int
  core: int
  socket: int
  node: int


def parse_topology(text: str) -> dict[int, Cpu]:
  """Parse `lscpu -p=CPU,CORE,SOCKET,NODE` output."""
  cpus: dict[int, Cpu] = {}
  for raw in text.splitlines():
    line = raw.strip()
    if not line or line.startswith("#"):
      continue
    parts = line.split(",")
    if len(parts) < 4:
      raise PlanError(f"cannot parse topology line: {raw!r}")
    try:
      cpu, core, socket, node = (int(p) for p in parts[:4])
    except ValueError as error:
      raise PlanError(f"non-numeric topology line: {raw!r}") from error
    cpus[cpu] = Cpu(cpu=cpu, core=core, socket=socket, node=node)
  if not cpus:
    raise PlanError("topology is empty")
  return cpus


def physical_core_count(cpus: dict[int, Cpu]) -> int:
  return len({c.core for c in cpus.values()})


def cpus_for_width(cpus: dict[int, Cpu], width: int) -> list[int]:
  """CPUs to pin a `width`-worker pool to, best placement first."""
  if width < 1:
    raise PlanError(f"width {width} is not positive")
  if width > len(cpus):
    raise PlanError(
      f"width {width} exceeds the machine's {len(cpus)} logical CPUs"
    )

  # Threads per core, in the order the kernel numbered them: index 0 is
  # the "first" thread of each core, index 1 its sibling.
  by_core: dict[int, list[Cpu]] = {}
  for cpu in sorted(cpus.values(), key=lambda c: c.cpu):
    by_core.setdefault(cpu.core, []).append(cpu)

  # Cores ordered by node then socket then id, so a width that fits one
  # NUMA node lands entirely inside it instead of straddling two.
  cores = sorted(
    by_core,
    key=lambda core: (
      by_core[core][0].node,
      by_core[core][0].socket,
      core,
    ),
  )

  chosen: list[int] = []
  depth = 0
  while len(chosen) < width:
    progressed = False
    for core in cores:
      if len(chosen) == width:
        break
      threads = by_core[core]
      if depth < len(threads):
        chosen.append(threads[depth].cpu)
        progressed = True
    if not progressed:
      raise PlanError(f"cannot place {width} workers on this machine")
    depth += 1
  return chosen


def taskset_list(chosen: list[int]) -> str:
  return ",".join(str(c) for c in sorted(chosen))


def read_topology() -> str:
  try:
    return subprocess.run(
      ["lscpu", "-p=CPU,CORE,SOCKET,NODE"],
      check=True,
      capture_output=True,
      text=True,
    ).stdout
  except (OSError, subprocess.CalledProcessError) as error:
    raise PlanError(f"cannot read topology from lscpu: {error}") from error


def main(argv: list[str] | None = None) -> int:
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument(
    "widths", nargs="+", type=int, help="worker counts to plan for"
  )
  parser.add_argument(
    "--topology",
    help="file with lscpu -p output; defaults to running lscpu",
  )
  args = parser.parse_args(argv)

  try:
    text = (
      open(args.topology, encoding="utf-8").read()
      if args.topology
      else read_topology()
    )
    cpus = parse_topology(text)
    for width in args.widths:
      print(f"{width}\t{taskset_list(cpus_for_width(cpus, width))}")
  except PlanError as error:
    print(f"sweep_cpu_plan: {error}", file=sys.stderr)
    return 1
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
