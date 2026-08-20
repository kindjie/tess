#!/usr/bin/env bash
# Runs on the Steam Deck. One invocation pins the governor around an entire
# calibration or candidate phase, including collection and analysis.
set -euo pipefail

if [ "$#" -ne 3 ]; then
  echo "usage: $0 <calibration|candidate> <bundle> <results>" >&2
  exit 2
fi
PHASE="$1"
BUNDLE="$2"
RESULT_DIR="$3"
case "$PHASE" in
  calibration|candidate) ;;
  *) echo "invalid maintenance campaign phase" >&2; exit 2 ;;
esac
case "$BUNDLE:$RESULT_DIR" in
  "$HOME"/*:"$HOME"/*) ;;
  *) echo "bundle and results must be below HOME" >&2; exit 2 ;;
esac

cd "$BUNDLE"
sha256sum -c SHA256SUMS

verify_result_set() {
  local directory="$1" phase="$2" manifest
  manifest="${phase}-SHA256SUMS"
  [ -f "$directory/$manifest" ] || return 1
  (
    cd "$directory"
    sha256sum -c "$manifest"
    diff -u \
      <(sed -E 's/^[0-9a-f]{64}  //' "$manifest" | LC_ALL=C sort) \
      <(find . -type f ! -name '*-SHA256SUMS' -print | LC_ALL=C sort)
    [ "$(find . -type f -name '*-SHA256SUMS' -print)" \
      = './calibration-SHA256SUMS' ]
  )
}

if [ "$PHASE" = "calibration" ]; then
  [ ! -e "$RESULT_DIR" ] \
    || { echo "calibration result directory already exists" >&2; exit 2; }
  mkdir -p "$RESULT_DIR"
else
  [ -d "$RESULT_DIR" ] \
    && [ -f "$RESULT_DIR/calibration.json" ] \
    && [ -f "$RESULT_DIR/thresholds.json" ] \
    || { echo "candidate needs retained calibration results" >&2; exit 2; }
  [ ! -e "$RESULT_DIR/candidate.json" ] \
    && [ ! -e "$RESULT_DIR/report.json" ] \
    || { echo "candidate result directory already contains outputs" >&2; exit 2; }
  verify_result_set "$RESULT_DIR" calibration \
    || { echo "retained calibration result set is invalid" >&2; exit 2; }
fi

snapshot_governors() {
  local output="$1" governor
  : > "$output"
  for governor in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
    [ -f "$governor" ] || continue
    printf '%s:%s\n' "$governor" "$(cat "$governor")" >> "$output"
  done
}

set_governors() {
  local target="$1" governor
  for governor in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
    [ -f "$governor" ] || continue
    printf '%s\n' "$target" | sudo tee "$governor" >/dev/null
  done
}

snapshot_governors "$RESULT_DIR/${PHASE}-governor-before.txt"
[ -s "$RESULT_DIR/${PHASE}-governor-before.txt" ] \
  || { echo "cannot read CPU governors" >&2; exit 2; }

restore_governors() {
  local status=$? governor target logging_status=0
  trap - EXIT
  if [ "${logging_active:-0}" -eq 1 ]; then
    exec 1>&3 2>&4
    wait "$stdout_tee_pid" || logging_status=$?
    wait "$stderr_tee_pid" || logging_status=$?
    exec 3>&- 4>&-
    rm -f "$stdout_fifo" "$stderr_fifo"
    if [ "$status" -eq 0 ] && [ "$logging_status" -ne 0 ]; then
      status="$logging_status"
    fi
  fi
  while IFS=: read -r governor target; do
    case "$governor:$target" in
      /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor:[A-Za-z0-9_-]*)
        printf '%s\n' "$target" | sudo tee "$governor" >/dev/null || true
        ;;
      *) echo "refusing invalid saved governor entry" >&2 ;;
    esac
  done < "$RESULT_DIR/${PHASE}-governor-before.txt"
  snapshot_governors "$RESULT_DIR/${PHASE}-governor-after.txt"
  printf '%s\n' "$status" > "$RESULT_DIR/${PHASE}-exit-status.txt"
  (
    cd "$RESULT_DIR"
    find . -type f ! -name '*-SHA256SUMS' -print0 \
      | sort -z \
      | xargs -0 sha256sum > "${PHASE}-SHA256SUMS"
  )
  exit "$status"
}
trap restore_governors EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

set_governors performance
snapshot_governors "$RESULT_DIR/${PHASE}-governor-during.txt"
if grep -v ':performance$' \
  "$RESULT_DIR/${PHASE}-governor-during.txt" >/dev/null; then
  echo "not every CPU governor is pinned to performance" >&2
  exit 2
fi
{
  date -u '+utc=%Y-%m-%dT%H:%M:%SZ'
  printf 'loadavg='; cat /proc/loadavg
  printf 'uname='; uname -a
  printf 'python='; python3 --version
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

mapfile -t SETTINGS < <(
  python3 - "$PHASE" <<'PY'
import json
import sys

config = json.load(open("bench/maintenance-campaign.json", encoding="utf-8"))
phase = sys.argv[1]
device = config["collection"]["devices"]["steam-deck"]
print(config["collection"]["repetitions"])
print(config["collection"]["minimum_time_seconds"])
key = "calibration_seed" if phase == "calibration" else "candidate_seed"
print(device[key])
PY
)
REPETITIONS="${SETTINGS[0]}"
MINIMUM_TIME="${SETTINGS[1]}"
SEED="${SETTINGS[2]}"
TOOL="tools/maintenance_campaign.py"
BINARY="bin/tess_bench_maintenance_campaign"
CONFIG="bench/maintenance-campaign.json"
MANIFEST="build-manifest.json"

if [ "$PHASE" = "calibration" ]; then
  python3 "$TOOL" calibrate \
    --binary "$BINARY" --config "$CONFIG" --device steam-deck \
    --build-manifest "$MANIFEST" --repetitions "$REPETITIONS" \
    --minimum-time "$MINIMUM_TIME" --seed "$SEED" \
    --output "$RESULT_DIR/calibration.json"
  python3 "$TOOL" thresholds \
    --input "$RESULT_DIR/calibration.json" --config "$CONFIG" \
    --output "$RESULT_DIR/thresholds.json"
else
  [ -f "$RESULT_DIR/thresholds.json" ] \
    || { echo "candidate phase requires retained thresholds.json" >&2; exit 2; }
  python3 "$TOOL" collect \
    --binary "$BINARY" --config "$CONFIG" --device steam-deck \
    --build-manifest "$MANIFEST" \
    --thresholds "${RESULT_DIR}/thresholds.json" \
    --calibration "${RESULT_DIR}/calibration.json" \
    --repetitions "$REPETITIONS" --minimum-time "$MINIMUM_TIME" \
    --seed "$SEED" --output "$RESULT_DIR/candidate.json"
  python3 "$TOOL" analyze \
    --input "$RESULT_DIR/candidate.json" --config "$CONFIG" \
    --thresholds "$RESULT_DIR/thresholds.json" \
    --calibration "$RESULT_DIR/calibration.json" \
    --output "$RESULT_DIR/report.json"
fi
