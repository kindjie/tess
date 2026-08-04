"""Pass/fail verdict for the paired mask-validation diagnostic.

`diagnose_pool_width.sh --diagnostic=validate` runs two arms at each
width: `fixed`, the N+1 mask the production planner now emits, and
`degraded`, the exactly-N mask it emitted before. The table it prints is
not the deliverable -- this verdict is.

Why a verdict at all: every benchmark invocation can succeed while the
run proves nothing. If the mask never reached `taskset` -- a typo in the
CPU list, a `taskset` that silently ignored it, a planner emitting the
same set twice -- both arms measure the same thing, both look clean, and
a script that only checks command exit status calls that a pass. A fix
that cannot be shown to be off has not been shown to be on.

The two criteria are deliberately different in shape:

  * The fixed arm must clear the floor at EVERY width. That is the
    claim being made, and one width failing it falsifies the claim.
  * The negative control need only fire at SOME width. At width 24 the
    exactly-N mask happens not to degrade (97% against the fixed arm's
    96%), because 24 workers on 24 CPUs leaves the dispatcher competing
    with a run that is already quantization-bound. Requiring a gap
    everywhere would fail a correct run on real hardware.

Both thresholds are on efficiency against the pool's quantization
ceiling, never against the worker count -- at 24 workers the ceiling is
19.5, so dividing by the width reports a true 96% as 78%.
"""

from __future__ import annotations

# The fixed arm is claimed to recover; 90% of ceiling is well clear of
# the 96-99% the fix actually measured and well above the 64-80% the
# degraded mask produced.
FIXED_FLOOR = 0.90

# A gap this size cannot be run-to-run noise: the observed gaps are 34,
# 30, 18 and 17 points, while repeated runs of the same arm sit inside
# one point.
MIN_GAP = 0.10


def parse_width(label: str) -> int | None:
  """The width encoded in a `fixed_w8` / `degraded_w8` label."""
  _, _, tail = label.partition("_w")
  return int(tail) if tail.isdigit() else None


def verdict(efficiency: dict[str, float]) -> tuple[bool, list[str]]:
  """Judge the paired arms. Returns (passed, lines to print).

  `efficiency` maps each arm's label to its measured fraction of the
  quantization ceiling.
  """
  widths = sorted(
    {w for label in efficiency if (w := parse_width(label)) is not None}
  )
  if not widths:
    return False, ["FAIL: no paired fixed/degraded rows were measured"]

  passed = True
  control_fired = False
  lines: list[str] = []
  for width in widths:
    fixed = efficiency.get(f"fixed_w{width}")
    degraded = efficiency.get(f"degraded_w{width}")
    if fixed is None or degraded is None:
      missing = "fixed" if fixed is None else "degraded"
      passed = False
      lines.append(f"  w{width}: FAIL -- the {missing} arm is missing")
      continue
    gap = fixed - degraded
    if gap >= MIN_GAP:
      control_fired = True
    if fixed < FIXED_FLOOR:
      passed = False
      lines.append(
        f"  w{width}: FAIL -- fixed arm at {fixed * 100:.0f}% of"
        + f" ceiling, under the {FIXED_FLOOR * 100:.0f}% floor"
      )
    else:
      lines.append(
        f"  w{width}: fixed {fixed * 100:.0f}%, degraded"
        + f" {degraded * 100:.0f}% ({gap * 100:+.0f} points)"
      )

  if not control_fired:
    passed = False
    lines.append(
      "  FAIL -- no negative control: the degraded arm never fell"
      + f" {MIN_GAP * 100:.0f} points below the fixed arm at any width,"
      + " so this run does not show the mask reached taskset"
    )
  return passed, lines
