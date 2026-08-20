#!/usr/bin/env bash
# Runs one immutable native-M3 maintenance evidence phase. The build manifest
# is created separately so its no-work provenance gate precedes all timing.
set -euo pipefail

if [ "$#" -ne 2 ]; then
  echo "usage: $0 <calibration|candidate> <results-directory>" >&2
  exit 2
fi

PHASE="$1"
RESULT_DIR="$2"
case "$PHASE" in
  calibration|candidate) ;;
  *) echo "invalid maintenance campaign phase" >&2; exit 2 ;;
esac

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$HERE/.." && pwd)"
RESULT_DIR="$(cd "$RESULT_DIR" && pwd)"
CONFIG="$REPO_ROOT/bench/maintenance-campaign.json"
TOOL="$REPO_ROOT/tools/maintenance_campaign.py"
BINARY="$REPO_ROOT/build/bench-only/bench/tess_bench_maintenance_campaign"
MANIFEST="$RESULT_DIR/build-manifest.json"

die() {
  printf '!! %s\n' "$*" >&2
  exit 2
}

verify_calibration_result_set() {
  [ -f "$RESULT_DIR/calibration-SHA256SUMS" ] || return 1
  (
    cd "$RESULT_DIR"
    shasum -a 256 -c calibration-SHA256SUMS
    diff -u \
      <(
        sed -E 's/^[0-9a-f]{64}  //' calibration-SHA256SUMS \
          | LC_ALL=C sort
      ) \
      <(
        find . -mindepth 1 ! -name '*-SHA256SUMS' -print \
          | LC_ALL=C sort
      )
    [ "$(find . -type f -name '*-SHA256SUMS' -print)" \
      = './calibration-SHA256SUMS' ]
  )
}

[ -f "$CONFIG" ] && [ -f "$TOOL" ] && [ -x "$BINARY" ] \
  || die "campaign tool, config, or binary is missing"
[ -f "$MANIFEST" ] || die "phase needs build-manifest.json"
manifest_source_sha="$(
  python3 - "$MANIFEST" <<'PY'
import json
import re
import sys

with open(sys.argv[1], encoding="utf-8") as manifest_file:
  source_sha = json.load(manifest_file).get("source_sha")
if (
  not isinstance(source_sha, str)
  or re.fullmatch(r"[0-9a-f]{40}", source_sha) is None
):
  raise SystemExit("build manifest has no valid source SHA")
print(source_sha)
PY
)"
if [ "$(git -C "$REPO_ROOT" rev-parse HEAD)" != "$manifest_source_sha" ] \
  || [ -n "$(
    git -C "$REPO_ROOT" status --porcelain --untracked-files=no
  )" ]; then
  die "native phase requires the build manifest's clean source commit"
fi
if [ "$PHASE" = "calibration" ]; then
  diff -u \
    <(printf '%s\n' './build-manifest.json') \
    <(cd "$RESULT_DIR" && find . -mindepth 1 -print | LC_ALL=C sort) \
    || die "calibration results directory is not pristine"
else
  verify_calibration_result_set \
    || die "retained calibration result set is invalid"
fi

logging_active=0
finish_phase() {
  local status=$? logging_status=0
  trap - EXIT
  if [ "$logging_active" -eq 1 ]; then
    exec 1>&3 2>&4
    wait "$stdout_tee_pid" || logging_status=$?
    wait "$stderr_tee_pid" || logging_status=$?
    exec 3>&- 4>&-
    rm -f "$stdout_fifo" "$stderr_fifo"
    if [ "$status" -eq 0 ] && [ "$logging_status" -ne 0 ]; then
      status="$logging_status"
    fi
  fi
  printf '%s\n' "$status" > "$RESULT_DIR/${PHASE}-exit-status.txt"
  (
    cd "$RESULT_DIR"
    find . -type f ! -name '*-SHA256SUMS' -print \
      | LC_ALL=C sort \
      | while IFS= read -r artifact; do
          shasum -a 256 "$artifact"
        done > "${PHASE}-SHA256SUMS"
  )
  exit "$status"
}
trap finish_phase EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

{
  date -u '+utc=%Y-%m-%dT%H:%M:%SZ'
  printf 'machine='; uname -m
  sw_vers 2>/dev/null || true
  python3 --version
} > "$RESULT_DIR/${PHASE}-environment.txt"
stdout_fifo="$RESULT_DIR/.${PHASE}.stdout.fifo"
stderr_fifo="$RESULT_DIR/.${PHASE}.stderr.fifo"
mkfifo "$stdout_fifo" "$stderr_fifo"
exec 3>&1 4>&2
tee "$RESULT_DIR/${PHASE}.stdout.log" < "$stdout_fifo" >&3 &
stdout_tee_pid=$!
tee "$RESULT_DIR/${PHASE}.stderr.log" < "$stderr_fifo" >&4 &
stderr_tee_pid=$!
exec > "$stdout_fifo" 2> "$stderr_fifo"
logging_active=1

settings="$(
  python3 - "$PHASE" "$CONFIG" <<'PY'
import json
import sys

phase = sys.argv[1]
with open(sys.argv[2], encoding="utf-8") as config_file:
  config = json.load(config_file)
collection = config["collection"]
device = collection["devices"]["m3"]
print(collection["repetitions"])
print(collection["minimum_time_seconds"])
key = "calibration_seed" if phase == "calibration" else "candidate_seed"
print(device[key])
PY
)"
REPETITIONS="$(printf '%s\n' "$settings" | sed -n '1p')"
MINIMUM_TIME="$(printf '%s\n' "$settings" | sed -n '2p')"
SEED="$(printf '%s\n' "$settings" | sed -n '3p')"

if [ "$PHASE" = "calibration" ]; then
  python3 "$TOOL" calibrate \
    --binary "$BINARY" --config "$CONFIG" --device m3 \
    --build-manifest "$MANIFEST" --repetitions "$REPETITIONS" \
    --minimum-time "$MINIMUM_TIME" --seed "$SEED" \
    --output "$RESULT_DIR/calibration.json"
  python3 "$TOOL" thresholds \
    --input "$RESULT_DIR/calibration.json" --config "$CONFIG" \
    --output "$RESULT_DIR/thresholds.json"
else
  python3 "$TOOL" collect \
    --binary "$BINARY" --config "$CONFIG" --device m3 \
    --build-manifest "$MANIFEST" \
    --thresholds "$RESULT_DIR/thresholds.json" \
    --calibration "$RESULT_DIR/calibration.json" \
    --repetitions "$REPETITIONS" --minimum-time "$MINIMUM_TIME" \
    --seed "$SEED" --output "$RESULT_DIR/candidate.json"
  python3 "$TOOL" analyze \
    --input "$RESULT_DIR/candidate.json" --config "$CONFIG" \
    --thresholds "$RESULT_DIR/thresholds.json" \
    --calibration "$RESULT_DIR/calibration.json" \
    --output "$RESULT_DIR/report.json"
fi
