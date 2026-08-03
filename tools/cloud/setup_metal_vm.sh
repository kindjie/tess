#!/usr/bin/env bash
# Startup script that runs ON the bare-metal instance.
#
# Builds tess, runs the campaign benchmarks with hardware counters,
# uploads results, and deletes its own instance.
#
# Self-deletion is armed as early as possible -- the moment the instance
# name and zone are known, before anything that can fail. An early fatal
# error must still delete the instance, because a machine that failed to
# build is billing exactly as much as one that is working.
#
# GCE's --max-run-duration is the backstop if even this trap does not
# run (kernel panic, gcloud missing from the image). Both exist because
# either alone has a failure mode the other covers.
set -euo pipefail

# Identity FIRST, then the trap, then anything that can fail. The
# logging pipe and the metadata reads are themselves fallible; arming
# the self-delete after them leaves a window where a failure strands a
# $10/hour machine.
# gcloud and gsutil are snaps on Ubuntu GCE images, and the guest
# agent's script environment has historically not included /snap/bin.
# Without this the self-delete silently no-ops and the duration cap
# becomes the only cleanup -- $15 for zero results. One line turns an
# accepted loss into a non-event.
export PATH="$PATH:/snap/bin:/usr/local/bin"

# Bounded and retried. This runs BEFORE the self-delete trap exists, so
# an unbounded request here is the one place the script can hang with
# nothing watching it. --max-time caps each attempt; the retry covers a
# metadata server that is briefly not ready during boot.
#
# The exposure if identity still comes back empty is bounded: the
# startup script only runs once the instance has reached RUNNING, so
# --max-run-duration is already counting down and the worst case is the
# cap, not an indefinite instance.
metadata_early() {
  local path="$1" value=""
  for _ in 1 2 3; do
    value="$(curl -fsS --max-time 5 -H "Metadata-Flavor: Google" \
      "http://metadata.google.internal/computeMetadata/v1/$path" \
      2>/dev/null || true)"
    [[ -n "$value" ]] && break
    sleep 1
  done
  printf '%s' "$value"
}
INSTANCE_NAME="$(metadata_early instance/name)"
ZONE_PATH="$(metadata_early instance/zone)"
ZONE="${ZONE_PATH##*/}"
PROJECT="$(metadata_early project/project-id)"


metadata() {
  curl -fsS --max-time 10 -H "Metadata-Flavor: Google" \
    "http://metadata.google.internal/computeMetadata/v1/$1" 2>/dev/null || true
}

# ---- Self-delete trap, armed before any fallible work ---------------
cleanup_and_die() {
  local code=$?
  # Everything in here is best-effort EXCEPT the delete. Under errexit a
  # failed log copy or status write would abort the handler before it
  # ever reached the delete, stranding the instance.
  set +e
  # Stop the heartbeat first so it cannot race the final upload.
  if [[ -n "${HEARTBEAT_PID:-}" ]]; then
    # kill then WAIT: killing the loop does not stop an in-flight
    # `gsutil cp`, which could otherwise overwrite status.txt with a
    # stale "benchmarking" after the final write -- leaving the watcher
    # showing a permanently stale heartbeat for a clean run, which the
    # runbook calls the case worth acting on.
    # Negative PID targets the whole group, so an in-flight gsutil goes
    # with it. Falls back to the bare PID if the group signal fails.
    if (( ${HEARTBEAT_GROUP:-0} )); then
      kill -TERM -- "-$HEARTBEAT_PID" 2>/dev/null || true
    fi
    kill -TERM "$HEARTBEAT_PID" 2>/dev/null || true
    wait "$HEARTBEAT_PID" 2>/dev/null || true
    sleep 1
  fi
  echo "campaign finished with status $code at $(date -u +%FT%TZ)"
  if [[ -n "${BUCKET:-}" ]]; then
    {
      echo "stage: finished"
      echo "exit_code: $code"
      echo "updated: $(date -u +%FT%TZ)"
    } > /tmp/status.txt
    gsutil -q cp /tmp/status.txt "$BUCKET/status.txt" 2>/dev/null || true
  fi
  # Best effort: get the log off the box before it disappears.
  if [[ -n "${BUCKET:-}" ]]; then
    gsutil -q cp /var/log/tess-campaign.log "$BUCKET/campaign.log" || true
  fi
  if [[ -n "$INSTANCE_NAME" && -n "$ZONE" && -n "$PROJECT" ]]; then
    echo "self-deleting $INSTANCE_NAME"
    gcloud compute instances delete "$INSTANCE_NAME" \
      --zone="$ZONE" --project="$PROJECT" --quiet || true
  else
    # Cannot self-delete without an identity. Say so loudly; GCE's
    # duration cap is now the only thing standing between this and a
    # long, expensive idle.
    echo "FATAL: no instance identity; relying on --max-run-duration" >&2
  fi
}
trap cleanup_and_die EXIT

# Logging installed AFTER the trap. Process substitution can fail,
# and a failure here previously terminated the script before the
# self-delete existed -- stranding a $10/hour machine at the one
# moment nothing else was watching.
exec > >(tee -a /var/log/tess-campaign.log) 2>&1
echo "campaign startup at $(date -u +%FT%TZ)"

BUCKET="$(metadata instance/attributes/tess-bucket)"
SOURCE_URL="$(metadata instance/attributes/tess-source-url)"
RUN_ID="$(metadata instance/attributes/tess-run-id)"
GIT_COMMIT="$(metadata instance/attributes/tess-git-commit)"
REQUIRE_COUNTERS="$(metadata instance/attributes/tess-require-counters)"
REQUIRE_COUNTERS="${REQUIRE_COUNTERS:-1}"
SOURCE_SHA256="$(metadata instance/attributes/tess-source-sha256)"
GIT_DESCRIBE="$(metadata instance/attributes/tess-git-describe)"

[[ -n "$BUCKET" && -n "$SOURCE_URL" ]] || {
  echo "FATAL: missing bucket metadata" >&2; exit 1; }

# ---- Progress heartbeat ---------------------------------------------
# Without this the only signal is the instance still existing, and a run
# close to the duration cap is exactly when you want to know whether it
# is working or wedged. Pushes the log and a one-line status every 30s,
# so `gsutil cat .../campaign.log` from anywhere shows current progress.
# The stage lives in a FILE, not a variable. `heartbeat &` runs in a
# subshell with its own copy of the parent's variables, so a variable
# would stay at its initial value forever and every heartbeat would
# report "starting" for the whole run -- verified directly. The file is
# the only thing both processes can see.
STAGE_FILE=/tmp/tess-campaign-stage
echo "starting" > "$STAGE_FILE"
set_stage() {
  echo "$1" > "$STAGE_FILE"
  echo "[stage] $1 at $(date -u +%FT%TZ)"
}
heartbeat() {
  while true; do
    sleep 30
    {
      echo "stage: $(cat "$STAGE_FILE" 2>/dev/null || echo unknown)"
      echo "updated: $(date -u +%FT%TZ)"
      echo "uptime_seconds: $(cut -d. -f1 /proc/uptime)"
    } > /tmp/status.txt
    gsutil -q cp /tmp/status.txt "$BUCKET/status.txt" 2>/dev/null || true
    gsutil -q cp /var/log/tess-campaign.log "$BUCKET/campaign.log" \
      2>/dev/null || true
  done
}
# setsid so the heartbeat and any gsutil it spawns share a process
# group that can be killed as a unit. Signalling only the shell leaves
# an in-flight upload running, which then races the final status write.
# A missing setsid would fail silently here: command-not-found in a
# background job does not trip errexit, HEARTBEAT_PID would hold a dead
# child, and the paid run would proceed with no heartbeat and no
# incremental log upload -- monitoring gone, with nothing saying so.
if command -v setsid >/dev/null 2>&1; then
  setsid bash -c "$(declare -f heartbeat); STAGE_FILE='$STAGE_FILE'; \
    BUCKET='$BUCKET'; heartbeat" &
  HEARTBEAT_PID=$!
  HEARTBEAT_GROUP=1
else
  echo "warning: setsid missing; heartbeat cannot be killed as a group" >&2
  heartbeat &
  HEARTBEAT_PID=$!
  HEARTBEAT_GROUP=0
fi
sleep 1
if ! kill -0 "$HEARTBEAT_PID" 2>/dev/null; then
  echo "ERROR: the heartbeat did not start; this run would have no" \
    "progress signal at all" >&2
  exit 5
fi
echo "heartbeat running (pid $HEARTBEAT_PID)"

set_stage "installing packages"
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
# numactl is for the thread-scaling stage: the world is allocated and
# first-touched on one thread, so on a 4-node box a 190-worker sweep would
# otherwise measure remote-memory latency rather than the executor.
apt-get install -y -qq \
  build-essential cmake ninja-build clang git python3 linux-tools-common \
  numactl
# perf lives in a kernel-matched package. linux-tools-generic does NOT
# match the GCE kernel, and linux-tools-common alone provides only a
# wrapper that errors. Try the exact kernel first, then the GCE flavour.
apt-get install -y -qq "linux-tools-$(uname -r)" \
  || apt-get install -y -qq linux-tools-gcp \
  || apt-get install -y -qq linux-tools-generic \
  || echo "warning: no linux-tools package installed"

set_stage "fetching source"
WORK=/opt/tess-campaign
mkdir -p "$WORK"
cd "$WORK"
gsutil -q cp "$SOURCE_URL" source.tar.gz
# The tarball ships without .git, so the recorded commit is otherwise an
# assertion the instance cannot check. Verifying the hash makes the
# provenance of every published number defensible: these results came
# from exactly this archive.
if [[ -n "$SOURCE_SHA256" ]]; then
  actual_sha="$(sha256sum source.tar.gz | awk '{print $1}')"
  if [[ "$actual_sha" != "$SOURCE_SHA256" ]]; then
    echo "FATAL: source archive hash mismatch" >&2
    echo "  expected $SOURCE_SHA256" >&2
    echo "  actual   $actual_sha" >&2
    exit 6
  fi
  echo "source verified: sha256 $actual_sha"
else
  echo "warning: no source hash supplied; provenance is unverified" >&2
fi
tar -xzf source.tar.gz

# Counters need this; a bare-metal instance is allowed to set it, which
# is the whole reason for using metal over a shared VM.
sysctl -w kernel.perf_event_paranoid=0 \
  || echo "warning: perf_event_paranoid not settable"

# Establish NOW whether the PMU actually works. Hardware counters are the
# entire reason this tier is worth its price; discovering they are
# unavailable after a 45-minute run means paying for numbers a cheap
# shared VM could have produced. Recorded either way so the result is
# never ambiguous after the fact.
# Exit status is not enough: perf returns 0 when the events open but
# report "<not counted>" or "<not supported>". Only a hard
# perf_event_open permission error is nonzero. Since this gate is the
# sole protection for the run's entire value proposition, it validates
# the VALUES the same way the publish path does.
PERF_OK=0
if command -v perf >/dev/null 2>&1; then
  probe_out="$(perf stat -x';' -e cycles,instructions -- true 2>&1 || true)"
  probe_cycles="$(awk -F';' '$3 == "cycles" { print $1; exit }' \
    <<<"$probe_out")"
  if [[ "$probe_cycles" =~ ^[0-9]+$ ]] && (( probe_cycles > 0 )); then
    PERF_OK=1
    echo "PMU: available (probe counted $probe_cycles cycles)"
  else
    echo "PMU: events opened but returned no usable value" \
      "(cycles='$probe_cycles')" >&2
  fi
fi
if (( PERF_OK == 0 )); then
  if [[ "$REQUIRE_COUNTERS" == "1" ]]; then
    # Counters are the only reason this tier costs what it does. A
    # timing-only run here buys numbers a cheap shared VM produces, so
    # stop now rather than spend 45 more minutes of metal time. The
    # trap self-deletes on the way out.
    echo "FATAL: counters required but unavailable; aborting before" \
      "spending the run. Re-run with --allow-no-counters to accept" \
      "timings only." >&2
    exit 3
  fi
  echo "continuing without counters (--allow-no-counters)" >&2
fi

export CC=clang CXX=clang++
set_stage "building"
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DTESS_BUILD_TESTING=OFF \
  -DTESS_BUILD_BENCHMARKS=ON \
  -DTESS_BUILD_EXAMPLES=OFF
cmake --build build --parallel

# A build that produced neither binary would otherwise run zero
# benchmarks, publish only machine.txt, and exit 0 -- the full boot,
# apt and build price for nothing, reported as clean. The `-x` guard in
# the loop below silently tolerates that, so assert here instead.
for required in build/bench/tess_bench build/bench/tess_bench_diagnostics; do
  if [[ ! -x "$required" ]]; then
    echo "FATAL: $required was not built; aborting rather than" \
      "reporting a zero-benchmark run as successful" >&2
    exit 4
  fi
done

# ---- Frequency governor ---------------------------------------------
# The 2026-08-03 campaign recorded `CPU(s) scaling MHz: 21%` against a
# 800-3800 MHz range, and its counter pass measured single-thread
# effective clocks from 2.35 to 3.79 GHz across benchmarks minutes apart
# on an idle machine. A benchmark that is sometimes clocked at 62% of
# another benchmark's rate cannot be compared against it, and no number
# of repetitions fixes it.
#
# Best effort by design: not every image exposes cpufreq, and a campaign
# is still worth running without it. What is NOT acceptable is not
# knowing, so the achieved state is recorded in machine.txt either way.
GOVERNOR_SET="unavailable"
if [[ -w /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor ]]; then
  for gov in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
    echo performance > "$gov" 2>/dev/null || true
  done
  # Verified across EVERY cpu, not just cpu0. A per-CPU write can fail
  # silently, and a sweep pinned to cores 24-47 does not care what core 0
  # is set to -- reading one core and reporting it as the machine's state
  # is how a partially-applied governor becomes an unnoticed variable.
  GOVERNOR_SET="$(cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor \
    2>/dev/null | sort -u | paste -sd, -)"
  if [[ "$GOVERNOR_SET" == "performance" ]]; then
    echo "cpufreq governor: performance on all CPUs"
  else
    echo "WARNING: cpufreq governor is not uniform across CPUs:" \
      "$GOVERNOR_SET" >&2
  fi
else
  echo "WARNING: cpufreq governor not writable; frequency is uncontrolled" >&2
fi
# intel_pstate exposes turbo separately. Leaving turbo ON is deliberate:
# adopters run with turbo on, and the goal is a stable clock rather than
# an artificially low one. Recorded so the state is never assumed.
TURBO_STATE="unknown"
[[ -r /sys/devices/system/cpu/intel_pstate/no_turbo ]] && \
  TURBO_STATE="no_turbo=$(cat /sys/devices/system/cpu/intel_pstate/no_turbo)"

mkdir -p results
# Machine identity is recorded for the campaign record; this file goes
# to a private bucket, not the public data branch, so full CPU details
# are appropriate here.
{
  echo "run_id: $RUN_ID"
  echo "commit: $GIT_COMMIT"
  echo "git_describe: ${GIT_DESCRIBE:-unknown}"
  echo "source_sha256: ${SOURCE_SHA256:-unverified}"
  echo "image: $(grep PRETTY_NAME /etc/os-release | cut -d'"' -f2)"
  echo "benchmark_flags: --benchmark_repetitions=10 --benchmark_min_time=0.2s"
  echo "sweep_flags: --benchmark_repetitions=20 --benchmark_min_time=0.2s"
  # Reported from what is actually installed. Claiming interleave here
  # while the sweep silently fell back to first-touch would turn a
  # memory-placement artifact into a published scaling curve.
  if command -v numactl >/dev/null 2>&1; then
    echo "sweep_memory_policy: numactl --interleave=all"
  else
    echo "sweep_memory_policy: NONE (numactl missing; first-touch)"
  fi
  echo "machine: $(metadata instance/machine-type | awk -F/ '{print $NF}')"
  echo "compiler: $($CXX --version | head -1)"
  echo "cmake: $(cmake --version | head -1)"
  echo "kernel: $(uname -r)"
  echo "perf_event_paranoid: $(cat /proc/sys/kernel/perf_event_paranoid)"
  echo "cpu_pinning: main pass none; thread-scaling sweep taskset per point"
  echo "cpufreq_governor: $GOVERNOR_SET"
  echo "turbo: $TURBO_STATE"
  echo "online_cpus: $(nproc)"
  echo "cpu_scaling_enabled: see the benchmark JSON context"
  echo "counter_pass_flags: --benchmark_min_time=1s (single repetition)"
  lscpu
} > results/machine.txt

# Uploaded as each stage finishes, not in one batch at the end. The
# whole run is close to the duration cap, and GCE deletes at the cap
# regardless of state -- a single upload at the end means an overrun
# loses everything, including the stages that had already completed.
UPLOAD_FAILURES=0
publish() {
  local path="$1"
  [[ -f "$path" ]] || return 0
  # One retry: a transient GCS error should not silently cost a result.
  gsutil -q cp "$path" "$BUCKET/$(basename "$path")" 2>/dev/null \
    || gsutil -q cp "$path" "$BUCKET/$(basename "$path")" 2>/dev/null \
    || {
      echo "ERROR: could not upload $path" >&2
      UPLOAD_FAILURES=$(( UPLOAD_FAILURES + 1 ))
    }
}

set_stage "benchmarking"
publish results/machine.txt

# The build's load was still decaying when the previous run started
# measuring (load average 4.39 on a 4-vCPU box). Waiting costs nothing
# against a 20-minute sweep and removes a contaminant from the first
# family measured.
echo "settling before measurement..."
for _ in $(seq 1 30); do
  load="$(cut -d' ' -f1 /proc/loadavg)"
  # Integer compare: bash has no float arithmetic.
  if [[ "${load%%.*}" -lt 1 ]]; then
    break
  fi
  sleep 5
done
echo "load average at start of benchmarking: $(cut -d' ' -f1-3 /proc/loadavg)"

# Failures handled per iteration with `if !`, NOT `done || { ... }`.
# Putting a compound command on the left of || disables errexit for
# everything inside it, so a failed benchmark would continue silently
# and the handler might never run -- the campaign could then report
# success after a benchmark died.
BENCH_FAILURES=0
for binary in build/bench/tess_bench build/bench/tess_bench_diagnostics; do
  [[ -x "$binary" ]] || continue
  name="$(basename "$binary")"
  echo "running $name at $(date -u +%FT%TZ)"
  if ! "$binary" \
      --benchmark_format=json \
      --benchmark_repetitions=10 \
      --benchmark_min_time=0.2s \
      --benchmark_out="results/$name.json" \
      --benchmark_out_format=json; then
    echo "ERROR: $name failed; publishing whatever it wrote" >&2
    BENCH_FAILURES=$(( BENCH_FAILURES + 1 ))
  fi
  # Published either way: a partial JSON from a died binary is still
  # worth having.
  publish "results/$name.json"
done

# ---- Thread-scaling sweep -------------------------------------------
# Its own stage, after the timing pass, so a failure here cannot cost the
# campaign's primary results. Its own binary too, which is why it is not
# in the loop above: registering a 190-worker sweep in tess_bench would
# have run it there as well.
#
# Run under `numactl --interleave=all`. AlwaysResidentWorld allocates and
# zero-fills every page on its constructing thread, so with default
# first-touch placement the whole 32 MiB world lands on one NUMA node and
# every worker beyond that node measures remote access.
#
# Memory placement was necessary but not sufficient. The 2026-08-03
# campaign had interleaving and still could not publish a curve, because
# THREAD placement was uncontrolled; see the pinned per-point loop below.
#
# 20 repetitions, not the timing pass's 10: this produces a curve, and a
# curve needs per-point dispersion tight enough to tell a real knee from
# noise. The analysis pass below turns the JSON into markdown and decides
# whether the dispersion allows publishing a curve at all.
#
# Bounded by `timeout`. Google Benchmark has no internal cap, so a pool
# that deadlocks at an untested width would otherwise run until GCE
# deletes the instance at the duration cap -- and because the JSON is
# written at process exit, that loses that point entirely while still
# being billed for it.
SWEEP_TIMEOUT_SECONDS="${SWEEP_TIMEOUT_SECONDS:-2700}"
SWEEP_POINT_TIMEOUT_SECONDS="${SWEEP_POINT_TIMEOUT_SECONDS:-300}"
SWEEP_FAILURES=0
sweep_binary=build/bench/tess_bench_thread_scaling
if [[ -x "$sweep_binary" ]]; then
  set_stage "thread-scaling sweep"
  sweep_name="$(basename "$sweep_binary")"
  sweep_runner=()
  if command -v numactl >/dev/null 2>&1; then
    sweep_runner=(numactl --interleave=all)
    echo "thread-scaling memory policy: interleave=all"
  else
    # Recorded, not silently accepted: without it the high-worker end of
    # the curve is a memory-placement measurement and must not be
    # published as a scaling result.
    echo "WARNING: numactl missing; sweep runs under default first-touch" \
      "placement and its high-worker points are NOT publishable" >&2
    SWEEP_FAILURES=$(( SWEEP_FAILURES + 1 ))
  fi
  # -k sends KILL if the process ignores TERM, so a wedged worker pool
  # cannot outlive its own timeout.
  # Per POINT, not per sweep: with one process per point, a single
  # timeout as large as the whole budget would let one wedged width
  # consume every remaining point's time. The overall budget is enforced
  # separately by the deadline check inside the loop.
  sweep_guard=()
  if command -v timeout >/dev/null 2>&1; then
    sweep_guard=(timeout -k 30 "$SWEEP_POINT_TIMEOUT_SECONDS")
  else
    # Counted, not just warned: without it a wedged width runs until GCE
    # deletes the instance, and the deadline check between points cannot
    # interrupt a point that never returns.
    echo "ERROR: no timeout binary; sweep points are unbounded" >&2
    SWEEP_FAILURES=$(( SWEEP_FAILURES + 1 ))
  fi
  # One process per point, each pinned to its own CPU set.
  #
  # The 2026-08-03 campaign ran the whole sweep in one unpinned process
  # and thread placement dominated everything above ~32 workers: CVs of
  # 16-33%, and repetitions splitting into discrete modes rather than
  # scattering. chunk_compute/4 sat at either 3.94x or 3.01x depending on
  # whether the kernel happened to give the four workers four separate
  # physical cores or let two of them share one core's SMT threads.
  # Averaging that lottery does not produce a curve.
  #
  # Pinning also makes the worker counts mean what the sweep claims: on
  # this machine 24 is exactly one NUMA node, 48 one socket, 96 every
  # physical core, 190 every core plus 94 SMT siblings. Unpinned they are
  # just numbers.
  # Probed once. Counting it per point would report one missing tool as
  # 77 failures and bury whatever else went wrong.
  HAVE_TASKSET=0
  if command -v taskset >/dev/null 2>&1; then
    HAVE_TASKSET=1
  else
    echo "ERROR: taskset missing; every point runs unpinned and thread" \
      "placement is uncontrolled -- the curve is not publishable" >&2
    SWEEP_FAILURES=$(( SWEEP_FAILURES + 1 ))
  fi

  mkdir -p results/sweep
  # Pool points only. Each process ALSO re-measures that workload's serial
  # baseline, under the same pinning, so the speedup ratio is computed
  # within one process. With one process per point, process identity --
  # ASLR, code layout, allocator state -- is otherwise perfectly
  # confounded with worker count, and this project has already measured a
  # benchmark whose cost moved 73% on a code-layout change alone.
  sweep_points="$("$sweep_binary" --benchmark_list_tests 2>/dev/null \
    | grep -v '/serial/real_time$' || true)"
  sweep_total=0
  [[ -n "$sweep_points" ]] && sweep_total="$(printf '%s\n' "$sweep_points" \
    | grep -c .)"
  if (( sweep_total == 0 )); then
    # Zero points measured, merged, and reported as a clean empty sweep
    # is the failure mode this guards: it looks identical to success.
    echo "ERROR: the sweep binary listed no pool points" >&2
    SWEEP_FAILURES=$(( SWEEP_FAILURES + 1 ))
  fi
  sweep_done=0
  sweep_deadline=$(( SECONDS + SWEEP_TIMEOUT_SECONDS ))
  while IFS= read -r point; do
    [[ -n "$point" ]] || continue
    # lab/thread_scaling/<workload>/<width>/real_time
    width="${point%/real_time}"; width="${width##*/}"
    workload="${point#lab/thread_scaling/}"; workload="${workload%%/*}"
    label="${point#lab/thread_scaling/}"; label="${label//\//_}"

    if (( SECONDS >= sweep_deadline )); then
      echo "ERROR: sweep budget of ${SWEEP_TIMEOUT_SECONDS}s exhausted with" \
        "$(( sweep_total - sweep_done )) point(s) unmeasured" >&2
      SWEEP_FAILURES=$(( SWEEP_FAILURES + 1 ))
      break
    fi

    if ! cpu_list="$(python3 tools/cloud/sweep_cpu_plan.py "$width" \
        2>/dev/null | cut -f2)" || [[ -z "$cpu_list" ]]; then
      echo "ERROR: no CPU plan for width $width ($point)" >&2
      SWEEP_FAILURES=$(( SWEEP_FAILURES + 1 ))
      continue
    fi

    pin=()
    if (( HAVE_TASKSET )); then
      pin=(taskset -c "$cpu_list")
    fi

    # The serial baseline shares the point's CPU set. It is one thread,
    # so a wider mask costs it nothing, and measuring it here is what
    # keeps the ratio inside a single process.
    if ! "${sweep_guard[@]+"${sweep_guard[@]}"}" \
        "${pin[@]+"${pin[@]}"}" \
        "${sweep_runner[@]+"${sweep_runner[@]}"}" "$sweep_binary" \
        --benchmark_filter="^lab/thread_scaling/${workload}/(serial|${width})/real_time\$" \
        --benchmark_format=json \
        --benchmark_repetitions=20 \
        --benchmark_min_time=0.2s \
        --benchmark_out="results/sweep/${label}.json" \
        --benchmark_out_format=json > /dev/null; then
      echo "ERROR: $point failed or timed out" >&2
      SWEEP_FAILURES=$(( SWEEP_FAILURES + 1 ))
    else
      sweep_done=$(( sweep_done + 1 ))
    fi
  done <<< "$sweep_points"
  echo "thread-scaling: measured $sweep_done of $sweep_total points"
  if (( sweep_done < sweep_total )); then
    SWEEP_FAILURES=$(( SWEEP_FAILURES + 1 ))
  fi

  # Merged into the single artifact the analysis and the runbook expect;
  # 77 separate uploads would be the same data, harder to use. A file
  # that will not parse is counted, not skipped: silently dropping it
  # would turn a lost point into a smaller-looking sweep.
  if ! python3 - <<'MERGE'
import glob, json, pathlib, sys
merged = []
context = None
bad = 0
for path in sorted(glob.glob("results/sweep/*.json")):
    try:
        blob = json.loads(pathlib.Path(path).read_text())
    except (OSError, json.JSONDecodeError) as error:
        print(f"ERROR: cannot read {path}: {error}", file=sys.stderr)
        bad += 1
        continue
    context = context or blob.get("context")
    merged.extend(blob.get("benchmarks", []))
pathlib.Path("results/tess_bench_thread_scaling.json").write_text(
    json.dumps({"context": context or {}, "benchmarks": merged})
)
print(f"merged {len(merged)} benchmark records from"
      f" {len(glob.glob('results/sweep/*.json')) - bad} file(s)")
sys.exit(1 if bad else 0)
MERGE
  then
    SWEEP_FAILURES=$(( SWEEP_FAILURES + 1 ))
  fi
  publish "results/$sweep_name.json"

  # The runbook promised the dispersion was checked before publication;
  # nothing was checking it. Run the report on the machine that produced
  # the data, and upload its verdict alongside the JSON.
  #
  # Deliberately NOT folded into CAMPAIGN_STATUS. "Too noisy to publish
  # as a curve" is a judgement about the data, not a failure of the run;
  # the JSON is still worth having, and marking the campaign failed would
  # blur that into the failures that mean something went wrong.
  if [[ -f "results/$sweep_name.json" ]]; then
    if python3 tools/thread_scaling_report.py "results/$sweep_name.json" \
        > "results/thread-scaling-report.md" 2> "results/thread-scaling.log"; then
      echo "thread-scaling sweep is publishable as a curve"
    else
      echo "NOTE: thread-scaling sweep is NOT publishable as a curve:"
      cat "results/thread-scaling.log" >&2 || true
    fi
    publish "results/thread-scaling-report.md"
    publish "results/thread-scaling.log"
  fi
else
  # Not fatal -- the campaign's primary results are already measured and
  # uploaded by this point -- but not silent either.
  echo "ERROR: $sweep_binary was not built; no thread-scaling data" >&2
  SWEEP_FAILURES=$(( SWEEP_FAILURES + 1 ))
fi

# ---- Per-benchmark counters -----------------------------------------
# A single `perf stat` over the whole binary averages ~200 heterogeneous
# benchmarks into one row, which attributes nothing. Counters are only
# useful per benchmark, so each is wrapped in its own filtered run.
#
# Deliberately a SEPARATE pass, after the timing pass: no published
# timing comes from a perf-wrapped process.
if (( PERF_OK )); then
  set_stage "counter attribution"
  # All eight of the fields family, not a subset. This campaign exists
  # because of a fields regression, the marginal cost is seconds, and a
  # one-shot run should not omit half the family it was run for.
  COUNTER_BENCHMARKS="${COUNTER_BENCHMARKS:-\
fields/goalset_build_1|\
fields/goalset_build_16|\
fields/goalset_build_256|\
fields/cache_hit|\
fields/cache_miss_store|\
fields/cache_eviction|\
fields/nearest_target|\
fields/build_alloc_gate}"
  COUNTER_FAILURES=0
  # These are PROCESS TOTALS, not per-operation costs. Comparing the
  # raw columns between benchmarks is invalid -- a faster benchmark
  # runs more iterations in the fixed min-time and accumulates more of
  # everything. Divide by the matching counter-iters-*.json first.
  echo "# process totals per benchmark, NOT per-operation; normalise" \
    "with counter-iters-<benchmark>.json before comparing rows" \
    > results/perf-per-benchmark.csv
  echo "benchmark,cycles,instructions,cache-misses,branch-misses,task-clock-ms" \
    >> results/perf-per-benchmark.csv
  IFS='|' read -ra COUNTER_LIST <<< "$COUNTER_BENCHMARKS"
  for bench in "${COUNTER_LIST[@]}"; do
    [[ -n "$bench" ]] || continue
    echo "counters for $bench"
    # --benchmark_out so the run can be PROVEN to have happened. A
    # filter matching nothing exits 0 and perf succeeds, so the row
    # would otherwise carry real numeric counters -- of process startup
    # -- attributed to a benchmark that never ran.
    rm -f "/tmp/counter-check-$$.json"
    if perf stat -x';' \
         -e cycles,instructions,cache-misses,branch-misses,task-clock \
         -o "/tmp/perf-$$.csv" -- \
         build/bench/tess_bench_diagnostics \
         --benchmark_filter="^${bench}\$" \
         --benchmark_min_time=1s \
         --benchmark_out="/tmp/counter-check-$$.json" \
         --benchmark_out_format=json >/dev/null 2>&1 \
       && grep -qF "\"$bench\"" "/tmp/counter-check-$$.json" 2>/dev/null; then
      # Match the EVENT FIELD exactly, not the whole record. perf's -x
      # output is "not-quite-CSV" and a substring match on the line can
      # hit metric text, a PMU-qualified duplicate, or another event
      # whose name contains the one wanted. Field 3 is the event name
      # for this invocation (no interval, no per-CPU aggregation);
      # field 1 is the value.
      field_for() {
        awk -F';' -v want="$1" \
          '$3 == want { print $1; exit }' "/tmp/perf-$$.csv"
      }
      cyc=$(field_for cycles)
      ins=$(field_for instructions)
      cms=$(field_for cache-misses)
      bms=$(field_for branch-misses)
      # "<not counted>" and "<not supported>" are not numbers. Accepting
      # them would publish a counter row that looks measured and is not.
      numeric() { [[ "$1" =~ ^[0-9]+$ ]]; }
      # ALL FOUR validated. Publishing "<not counted>" verbatim in the
      # cache-miss column would put a non-measurement in a column that
      # reads as one.
      if numeric "$cyc" && numeric "$ins" \
         && numeric "$cms" && numeric "$bms"; then
        tsk="$(field_for task-clock)"
        echo "$bench,${cyc},${ins},${cms},${bms},${tsk:-}" \
          >> results/perf-per-benchmark.csv
      else
        echo "warning: non-numeric counters for $bench" \
          "(cycles='$cyc' instructions='$ins' cache-misses='$cms'" \
          "branch-misses='$bms')" >&2
        echo "$bench,,,," >> results/perf-per-benchmark.csv
        # Counted, not just warned. An empty row followed by "campaign
        # complete" and exit 0 is the same silent success this file has
        # already produced twice.
        COUNTER_FAILURES=$(( COUNTER_FAILURES + 1 ))
      fi
      # Keep the raw output: without it a suspect row cannot be
      # diagnosed after the instance is gone.
      cp "/tmp/perf-$$.csv" "results/perf-raw-${bench//\//_}.csv" 2>/dev/null || true
      publish "results/perf-raw-${bench//\//_}.csv"
      # The counter run's OWN iteration count. Without it these totals
      # cannot be turned into per-operation figures: perf wraps the
      # whole process, so a cheaper benchmark simply runs more
      # iterations and accumulates more cycles. The first campaign
      # published the totals without it and they were misread.
      cp "/tmp/counter-check-$$.json" \
        "results/counter-iters-${bench//\//_}.json" 2>/dev/null || true
      publish "results/counter-iters-${bench//\//_}.json"
    else
      echo "warning: counters failed for $bench, or the filter matched" \
        "no benchmark" >&2
      echo "$bench,,,,," >> results/perf-per-benchmark.csv
      COUNTER_FAILURES=$(( COUNTER_FAILURES + 1 ))
    fi
    publish results/perf-per-benchmark.csv
  done
else
  echo "skipping counter attribution: PMU unavailable"
fi

# Sweep anything a per-stage publish missed, then report honestly. A
# blanket "all stages uploaded" after warnings is how a partial result
# gets mistaken for a complete one.
for leftover in results/*; do
  [[ -f "$leftover" ]] && publish "$leftover"
done
# Counting a failure and never reading the count is how a run reports
# success after a benchmark died. Both counters decide the exit status,
# which the EXIT handler records in status.txt.
CAMPAIGN_STATUS=0
if (( ${COUNTER_FAILURES:-0} > 0 )); then
  echo "ERROR: ${COUNTER_FAILURES} counter capture(s) failed" >&2
  CAMPAIGN_STATUS=1
fi
if (( BENCH_FAILURES > 0 )); then
  echo "ERROR: $BENCH_FAILURES benchmark binary/binaries FAILED" >&2
  CAMPAIGN_STATUS=1
fi
if (( ${SWEEP_FAILURES:-0} > 0 )); then
  echo "ERROR: ${SWEEP_FAILURES} thread-scaling problem(s); the sweep is" \
    "NOT publishable as a scaling result" >&2
  CAMPAIGN_STATUS=1
fi
if (( UPLOAD_FAILURES > 0 )); then
  echo "ERROR: $UPLOAD_FAILURES upload(s) failed; results are INCOMPLETE" >&2
  CAMPAIGN_STATUS=1
fi
if (( CAMPAIGN_STATUS == 0 )); then
  # Run id, not the bucket URI. The log itself is an artifact, and a
  # private bucket name in it is the one thing in these results that
  # must never reach the public data branch -- the JSON sanitiser does
  # not cover log files. The operator already knows the destination;
  # the log does not need to repeat it.
  echo "campaign complete; all stages uploaded (run $RUN_ID)"
fi
exit "$CAMPAIGN_STATUS"
