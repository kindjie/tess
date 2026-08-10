# test_sweep_cpu_plan.py

- `tests/test_sweep_cpu_plan.py`: pytest coverage for the thread-scaling
  sweep's CPU pinning plan (`tools/cloud/sweep_cpu_plan.py`), its paired
  validation verdict (`tools/cloud/diagnostic_verdict.py`), and literal
  assertions on `tools/cloud/diagnose_pool_width.sh`. Against a synthetic
  two-socket, 48-core, two-thread topology it pins one thread per
  physical core with SMT siblings used only once every core is occupied,
  widths that land on exactly one NUMA node (24) and one socket (48),
  errors for zero or over-wide requests, deterministic plans, and the N+1
  mask that gives the dispatcher a CPU of its own — a sibling of a worker
  core, so the mask never spills into another NUMA node and destroys the
  width's topological meaning. The shell diagnostic must divide by the
  pool's quantization ceiling rather than the width, with that formula
  cross-checked against `tools/thread_scaling_report.py`, and must fail
  the run on an unplannable width or a failed verdict. The verdict fails
  when both arms look clean (the negative control never fired, which is
  what a `taskset` mask that never took effect looks like), when an arm
  is missing, and when nothing was measured, while tolerating the one
  width whose degraded arm legitimately shows no gap — provided it is not
  the only width.
