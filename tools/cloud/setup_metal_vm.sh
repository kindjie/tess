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

exec > >(tee -a /var/log/tess-campaign.log) 2>&1
echo "campaign startup at $(date -u +%FT%TZ)"

metadata() {
  curl -fsS -H "Metadata-Flavor: Google" \
    "http://metadata.google.internal/computeMetadata/v1/$1" 2>/dev/null || true
}

INSTANCE_NAME="$(metadata instance/name)"
ZONE_PATH="$(metadata instance/zone)"
ZONE="${ZONE_PATH##*/}"
PROJECT="$(metadata project/project-id)"

# ---- Self-delete trap, armed before any fallible work ---------------
cleanup_and_die() {
  local code=$?
  # Stop the heartbeat first so it cannot race the final upload.
  if [[ -n "${HEARTBEAT_PID:-}" ]]; then
    kill "$HEARTBEAT_PID" 2>/dev/null || true
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
  if [[ -n "$INSTANCE_NAME" && -n "$ZONE" ]]; then
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

BUCKET="$(metadata instance/attributes/tess-bucket)"
SOURCE_URL="$(metadata instance/attributes/tess-source-url)"
RUN_ID="$(metadata instance/attributes/tess-run-id)"
GIT_COMMIT="$(metadata instance/attributes/tess-git-commit)"

[[ -n "$BUCKET" && -n "$SOURCE_URL" ]] || {
  echo "FATAL: missing bucket metadata" >&2; exit 1; }

# ---- Progress heartbeat ---------------------------------------------
# Without this the only signal is the instance still existing, and a run
# close to the duration cap is exactly when you want to know whether it
# is working or wedged. Pushes the log and a one-line status every 30s,
# so `gsutil cat .../campaign.log` from anywhere shows current progress.
STAGE="starting"
set_stage() {
  STAGE="$1"
  echo "[stage] $STAGE at $(date -u +%FT%TZ)"
}
heartbeat() {
  while true; do
    sleep 30
    {
      echo "stage: $STAGE"
      echo "updated: $(date -u +%FT%TZ)"
      echo "uptime_seconds: $(cut -d. -f1 /proc/uptime)"
    } > /tmp/status.txt
    gsutil -q cp /tmp/status.txt "$BUCKET/status.txt" 2>/dev/null || true
    gsutil -q cp /var/log/tess-campaign.log "$BUCKET/campaign.log" \
      2>/dev/null || true
  done
}
heartbeat &
HEARTBEAT_PID=$!

set_stage "installing packages"
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq \
  build-essential cmake ninja-build clang git python3 linux-tools-common \
  linux-tools-generic

set_stage "fetching source"
WORK=/opt/tess-campaign
mkdir -p "$WORK"
cd "$WORK"
gsutil -q cp "$SOURCE_URL" source.tar.gz
tar -xzf source.tar.gz

# Counters need this; a bare-metal instance is allowed to set it, which
# is the whole reason for using metal over a shared VM.
sysctl -w kernel.perf_event_paranoid=0 || \
  echo "warning: perf_event_paranoid not settable; counters may be limited"

export CC=clang CXX=clang++
set_stage "building"
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DTESS_BUILD_TESTING=OFF \
  -DTESS_BUILD_BENCHMARKS=ON \
  -DTESS_BUILD_EXAMPLES=OFF
cmake --build build --parallel

mkdir -p results
# Machine identity is recorded for the campaign record; this file goes
# to a private bucket, not the public data branch, so full CPU details
# are appropriate here.
{
  echo "run_id: $RUN_ID"
  echo "commit: $GIT_COMMIT"
  echo "machine: $(curl -fsS -H 'Metadata-Flavor: Google' \
    http://metadata.google.internal/computeMetadata/v1/instance/machine-type \
    | awk -F/ '{print $NF}')"
  lscpu
} > results/machine.txt

# Uploaded as each stage finishes, not in one batch at the end. The
# whole run is close to the duration cap, and GCE deletes at the cap
# regardless of state -- a single upload at the end means an overrun
# loses everything, including the stages that had already completed.
publish() {
  local path="$1"
  [[ -f "$path" ]] || return 0
  gsutil -q cp "$path" "$BUCKET/$(basename "$path")" \
    || echo "warning: could not upload $path"
}

set_stage "benchmarking"
publish results/machine.txt

for binary in build/bench/tess_bench build/bench/tess_bench_diagnostics; do
  [[ -x "$binary" ]] || continue
  name="$(basename "$binary")"
  echo "running $name at $(date -u +%FT%TZ)"
  "$binary" \
    --benchmark_format=json \
    --benchmark_repetitions=10 \
    --benchmark_min_time=0.2s \
    --benchmark_out="results/$name.json" \
    --benchmark_out_format=json
  publish "results/$name.json"
done

if command -v perf >/dev/null; then
  perf stat -x, -o results/perf-stat.csv \
    build/bench/tess_bench --benchmark_min_time=0.1s \
    || echo "warning: perf stat failed; continuing"
  publish results/perf-stat.csv
fi

echo "all stages uploaded to $BUCKET"
