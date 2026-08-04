#!/usr/bin/env bash
# Width-2 pool diagnostic. Runs ON the instance, invoked by
# setup_metal_vm.sh when the tess-diagnostic metadata attribute is set.
#
# It does NOT create, delete, or bill anything itself: the caller owns the
# instance lifetime, the self-delete trap and the duration cap. This
# script only measures and writes into results/.
#
# The question
# ------------
# The 2026-08-03 campaigns produced a clean factorial (efficiency =
# speedup / width, chunk_compute):
#
#            metal unpinned   metal pinned   local arm64
#   width 1        99%            100%          100%
#   width 2        98%             64%           99%
#   width 4        75%             77%           99%
#   width 8        76%             82%           99%
#
# Width 2 collapses ONLY when pinned, and only on this machine. Every
# competing explanation has already been refuted from the artifacts:
# the paired serial baseline is flat to 0.09% across widths; dispatcher
# CPU is 22 us against a 20,557 us wall; the quantization ceiling is
# exactly 2.0 at width 2; ambient load is HIGHER at width 1, which is
# unaffected; and there is no two-worker branch anywhere in the dispatch
# path -- stride, run count and the notify path are structurally
# identical at 1/2/4/8.
#
# What is left is the CPU set itself. Pinning to exactly N CPUs puts N
# workers and the dispatcher on N CPUs. This varies the mask and holds
# everything else fixed.
#
# The masks, and what each one decides
# ------------------------------------
#   two_cores     two distinct physical cores, same NUMA node  (the campaign's mask)
#   two_plus_one  the same two cores plus a third              -> dispatcher crowding
#   smt_pair      one physical core's two SMT threads          -> positive control
#   cross_node    two cores in different NUMA nodes            -> locality
#   cross_socket  two cores in different sockets               -> interconnect
#
# If two_plus_one recovers ~2x, the cause is that the dispatcher has
# nowhere to run. If smt_pair reproduces the campaign's 1.29x, then the
# campaign's mask was landing on siblings and the topology assumption in
# sweep_cpu_plan.py is wrong. If every mask with two real cores gives
# ~2x, the effect is not the mask and the hypothesis is dead.
#
# Width 1 and width 4 run under every mask too, as controls: width 1
# must stay at ~1.0 everywhere, and width 4 must stay near its campaign
# value. A mask that moves those is telling us the harness is at fault
# rather than the placement.
set -euo pipefail

BINARY="build/bench/tess_bench_thread_scaling"
WORKLOAD="${DIAG_WORKLOAD:-chunk_compute}"
REPS="${DIAG_REPETITIONS:-10}"
# Fixed iteration count, not a time floor. Google Benchmark auto-tunes
# iterations to hit a min time, so a slower point gets fewer iterations
# and any absolute counter total becomes incomparable across points --
# the fixed-N trap in the profiling notes. `8x` pins it.
ITERS="${DIAG_ITERATIONS:-8x}"

mkdir -p results/diagnostic

# ---- Topology, read rather than assumed -----------------------------
# The campaign inferred that CPUs 0-95 are distinct physical cores from
# the NUMA node listing. That inference has never been checked against
# the CORE column, and every mask below depends on it.
lscpu -p=CPU,CORE,SOCKET,NODE > results/diagnostic/topology.csv 2>/dev/null || {
  echo "FATAL: lscpu unavailable; cannot choose masks" >&2
  exit 1
}
for cpu in 0 1 2 24 48 96; do
  sib="/sys/devices/system/cpu/cpu${cpu}/topology/thread_siblings_list"
  core="/sys/devices/system/cpu/cpu${cpu}/topology/core_id"
  [[ -r "$sib" ]] && echo "cpu${cpu} siblings: $(cat "$sib")" \
    >> results/diagnostic/topology.txt
  [[ -r "$core" ]] && echo "cpu${cpu} core_id:  $(cat "$core")" \
    >> results/diagnostic/topology.txt
done

# In validation mode the masks come from the production planner itself,
# paired: `fixed` is what sweep_cpu_plan.py now emits (N+1 CPUs, the
# dispatcher included) and `degraded` is what it emitted before
# (--workers-only, exactly N). Both arms run in the same process order
# on the same machine, so the comparison cannot be confounded by boot
# state, thermal drift or a different instance.
#
# The negative arm is the point: if BOTH arms look clean the run proves
# nothing, because it would mean the mask never reached taskset. A fix
# that cannot be shown to be off is not a fix that has been shown to be
# on.
if [[ "${DIAG_MODE:-masks}" == "validate" ]]; then
  mapfile -t MASKS < <(
    for w in ${DIAG_WIDTHS:-2 4 8 16 24}; do
      fixed="$(python3 tools/cloud/sweep_cpu_plan.py "$w" | cut -f2)"
      degraded="$(python3 tools/cloud/sweep_cpu_plan.py "$w" --workers-only \
        | cut -f2)"
      printf 'fixed_w%s\t%s\t%s\n' "$w" "$fixed" "$w"
      printf 'degraded_w%s\t%s\t%s\n' "$w" "$degraded" "$w"
    done
  )
  echo "validation masks, straight from the production planner:"
  printf '  %s\n' "${MASKS[@]}"
  printf '%s\n' "${MASKS[@]}" > results/diagnostic/masks.tsv
else

# Pick masks FROM the topology instead of hard-coding the assumption.
# `python3` is already required by the campaign.
mapfile -t MASKS < <(python3 - <<'PLAN'
import collections, pathlib, sys
rows = []
for line in pathlib.Path("results/diagnostic/topology.csv").read_text().splitlines():
    line = line.strip()
    if not line or line.startswith("#"):
        continue
    cpu, core, socket, node = (int(x) for x in line.split(",")[:4])
    rows.append((cpu, core, socket, node))
by_core = collections.defaultdict(list)
for cpu, core, socket, node in rows:
    by_core[core].append(cpu)
info = {cpu: (core, socket, node) for cpu, core, socket, node in rows}
first = sorted(by_core)                      # one entry per physical core
def cpu_of(core):
    return sorted(by_core[core])[0]

if len(first) < 2:
    print("machine has fewer than two physical cores; nothing to compare",
          file=sys.stderr)
    raise SystemExit(1)

base = cpu_of(first[0])
second = cpu_of(first[1])
print(f"two_cores\t{base},{second}")
# Needs a third physical core; a 2-core validation VM cannot host it, and
# substituting an SMT sibling would silently change what the mask means.
if len(first) >= 3:
    print(f"two_plus_one\t{base},{second},{cpu_of(first[2])}")
# Four physical cores, so the width-4 control has a mask that can host
# it; without this every mask is too narrow and width 4 is silently
# skipped, losing the control that says whether the harness is at fault.
if len(first) >= 4:
    print("four_cores\t" + ",".join(str(cpu_of(c)) for c in first[:4]))
# A real sibling pair, if SMT is present.
sibs = sorted(by_core[first[0]])
if len(sibs) > 1:
    print(f"smt_pair\t{sibs[0]},{sibs[1]}")
# First core on a different NUMA node, then a different socket.
node0 = info[base][2]
sock0 = info[base][1]
other_node = next((cpu_of(c) for c in first if info[cpu_of(c)][2] != node0), None)
if other_node is not None:
    print(f"cross_node\t{base},{other_node}")
other_sock = next((cpu_of(c) for c in first if info[cpu_of(c)][1] != sock0), None)
if other_sock is not None:
    print(f"cross_socket\t{base},{other_sock}")
PLAN
)

echo "diagnostic masks chosen from the live topology:"
printf '  %s\n' "${MASKS[@]}"
printf '%s\n' "${MASKS[@]}" > results/diagnostic/masks.tsv
fi

# ---- Measure --------------------------------------------------------
# Every (mask, width) pair runs in its own process, and each process also
# re-measures the serial baseline under the SAME mask -- the pairing the
# campaign added so a ratio never straddles a process boundary.
DIAG_FAILURES=0
: > results/diagnostic/summary.tsv
for entry in "${MASKS[@]}"; do
  # label \t cpus \t widths   -- the third field is optional; without it
  # the mask is exercised at 1, 2 and 4 workers.
  IFS=$'\t' read -r label cpus widths <<<"$entry"
  widths="${widths:-1 2 4}"
  width_count="$(awk -F, '{print NF}' <<<"$cpus")"
  for width in $widths; do
    # A mask must hold at least as many CPUs as workers, or the point is
    # measuring oversubscription rather than placement.
    (( width > width_count )) && continue
    # The validate-mode labels already carry the width, so only append
    # it when it is not there already.
    case "$label" in
      *_w"${width}") out="results/diagnostic/${label}.json" ;;
      *)             out="results/diagnostic/${label}_w${width}.json" ;;
    esac
    echo "[$label cpus=$cpus width=$width] $(date -u +%FT%TZ)"
    if ! taskset -c "$cpus" "$BINARY" \
        --benchmark_filter="^lab/thread_scaling/${WORKLOAD}/(serial|${width})/real_time\$" \
        --benchmark_format=json \
        --benchmark_repetitions="$REPS" \
        --benchmark_min_time="$ITERS" \
        --benchmark_out="$out" \
        --benchmark_out_format=json > /dev/null; then
      echo "ERROR: $label width $width failed" >&2
      DIAG_FAILURES=$(( DIAG_FAILURES + 1 ))
      continue
    fi
    # Exit status is not evidence of measurement: a filter matching
    # nothing exits zero.
    if ! grep -q "\"name\": \"lab/thread_scaling/${WORKLOAD}/${width}/real_time\"" \
        "$out" 2>/dev/null; then
      echo "ERROR: $label width $width produced no rows" >&2
      DIAG_FAILURES=$(( DIAG_FAILURES + 1 ))
      continue
    fi
    printf '%s\t%s\t%s\t%s\n' "$label" "$cpus" "$width" "$out" \
      >> results/diagnostic/summary.tsv
  done
done

# ---- Report ---------------------------------------------------------
python3 - <<'REPORT' | tee results/diagnostic/report.txt
import json, pathlib, statistics as st

rows = []
summary = pathlib.Path("results/diagnostic/summary.tsv")
for line in summary.read_text().splitlines() if summary.exists() else []:
    label, cpus, width, path = line.split("\t")
    blob = json.loads(pathlib.Path(path).read_text())
    def med(suffix):
        vals = [e["real_time"] for e in blob["benchmarks"]
                if e.get("run_type") == "iteration"
                and e["name"].endswith(suffix)]
        return st.median(vals) if vals else None
    serial = med("/serial/real_time")
    pool = med(f"/{width}/real_time")
    if serial and pool:
        w = int(width)
        rows.append((label, cpus, w, serial, pool, serial / pool,
                     serial / pool / w))

print()
print("width-2 pool diagnostic -- speedup against the serial run measured")
print("in the SAME process under the SAME mask")
print()
print(f"{'mask':<14}{'cpus':<18}{'w':>3}{'serial us':>12}{'pool us':>12}"
      f"{'speedup':>9}{'efficiency':>12}")
for label, cpus, w, serial, pool, sp, eff in rows:
    print(f"{label:<14}{cpus:<18}{w:>3}{serial/1000:>12.1f}{pool/1000:>12.1f}"
          f"{sp:>9.2f}{eff*100:>11.0f}%")

print()
two = {label: eff for label, _, w, _, _, _, eff in rows if w == 2}
if "two_cores" in two:
    print(f"campaign mask (two_cores) at width 2: {two['two_cores']*100:.0f}%"
          "  -- the campaign measured 64%")
    for other in ("two_plus_one", "smt_pair", "cross_node", "cross_socket"):
        if other in two:
            delta = (two[other] - two["two_cores"]) * 100
            print(f"  vs {other:<14} {two[other]*100:>4.0f}%  ({delta:+.0f} points)")
    print()
    print("Reading it: two_plus_one recovering means the dispatcher had")
    print("nowhere to run. smt_pair matching two_cores means the campaign")
    print("mask was landing on siblings. All masks near 100% means the")
    print("effect is not the mask and this hypothesis is dead.")
REPORT

if (( DIAG_FAILURES > 0 )); then
  echo "ERROR: $DIAG_FAILURES diagnostic point(s) failed" >&2
  exit 1
fi
