#!/usr/bin/env bash
# Runs on the Steam Deck. One invocation pins the governor around an entire
# calibration or candidate phase, including collection and analysis.
set -euo pipefail

if [ "$#" -ne 4 ]; then
  echo "usage: $0 <calibration|candidate> <bundle> <results> <bundle-sha>" >&2
  exit 2
fi
PHASE="$1"
BUNDLE="$2"
RESULT_DIR="$3"
EXPECTED_BUNDLE_SHA="$4"
[[ "$EXPECTED_BUNDLE_SHA" =~ ^[0-9a-f]{64}$ ]] \
  || { echo "invalid expected bundle identity" >&2; exit 2; }
case "$PHASE" in
  calibration|candidate) ;;
  *) echo "invalid maintenance campaign phase" >&2; exit 2 ;;
esac
case "$BUNDLE:$RESULT_DIR" in
  "$HOME"/*:"$HOME"/*) ;;
  *) echo "bundle and results must be below HOME" >&2; exit 2 ;;
esac

verify_bundle() {
  local bundle="$1"
  [ -f "$bundle/SHA256SUMS" ] || return 1
  (
    cd "$bundle"
    [ -z "$(
      find . -mindepth 1 ! -type f ! -type d -print -quit
    )" ] || exit 1
    diff -u \
      <(
        sed -E 's/^[0-9a-f]{64}  //' SHA256SUMS \
          | sed 's#^\./##' | LC_ALL=C sort
      ) \
      <(
        find . -type f ! -name SHA256SUMS -print \
          | sed 's#^\./##' | LC_ALL=C sort
      ) || exit 1
    sha256sum -c SHA256SUMS || exit 1
  )
}

verify_bundle "$BUNDLE" \
  || { echo "bundle checksum or inventory validation failed" >&2; exit 2; }
[ "$(sha256sum "$BUNDLE/SHA256SUMS" | awk '{print $1}')" \
  = "$EXPECTED_BUNDLE_SHA" ] \
  || { echo "remote bundle identity differs from host" >&2; exit 2; }
cd "$BUNDLE"

verify_result_set() {
  local directory="$1" phase="$2" manifest
  manifest="${phase}-SHA256SUMS"
  [ -f "$directory/$manifest" ] || return 1
  (
    cd "$directory"
    [ -z "$(
      find . -mindepth 1 ! -type f ! -type d -print -quit
    )" ] || exit 1
    diff -u \
      <(sed -E 's/^[0-9a-f]{64}  //' "$manifest" | LC_ALL=C sort) \
      <(
        find . -type f ! -name '*-SHA256SUMS' -print \
          | LC_ALL=C sort
      ) || exit 1
    [ "$(find . -type f -name '*-SHA256SUMS' -print)" \
      = './calibration-SHA256SUMS' ] || exit 1
    sha256sum -c "$manifest" || exit 1
  )
}

if [ "$PHASE" = "calibration" ]; then
  [ ! -e "$RESULT_DIR" ] \
    || { echo "calibration result directory already exists" >&2; exit 2; }
  mkdir -p "$RESULT_DIR"
  printf '%s\n' "$EXPECTED_BUNDLE_SHA" > "$RESULT_DIR/bundle-sha256.txt"
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
  [ "$(cat "$RESULT_DIR/bundle-sha256.txt")" = "$EXPECTED_BUNDLE_SHA" ] \
    || { echo "candidate bundle identity differs from calibration" >&2; exit 2; }
fi

snapshot_governors() {
  local output="$1" governor target
  : > "$output"
  for governor in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
    [ -f "$governor" ] || continue
    target="$(cat "$governor")" || return 1
    printf '%s:%s\n' "$governor" "$target" >> "$output"
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

logging_active=0
restore_governors() {
  local status=$? governor target logging_status=0 restore_status=0
  trap - EXIT
  while IFS=: read -r governor target; do
    case "$governor:$target" in
      /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor:[A-Za-z0-9_-]*)
        if ! printf '%s\n' "$target" | sudo tee "$governor" >/dev/null; then
          echo "failed to restore governor: $governor" >&2
          restore_status=1
        fi
        ;;
      *)
        echo "refusing invalid saved governor entry" >&2
        restore_status=1
        ;;
    esac
  done < "$RESULT_DIR/${PHASE}-governor-before.txt"
  if ! snapshot_governors "$RESULT_DIR/${PHASE}-governor-after.txt" \
    || ! cmp -s "$RESULT_DIR/${PHASE}-governor-before.txt" \
      "$RESULT_DIR/${PHASE}-governor-after.txt"; then
    echo "governor restoration was incomplete" >&2
    restore_status=1
  fi
  if [ "$status" -eq 0 ] && [ "$restore_status" -ne 0 ]; then
    status=2
  fi
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
    find . -type f ! -name '*-SHA256SUMS' -print0 \
      | sort -z \
      | xargs -0 sha256sum > "${PHASE}-SHA256SUMS"
  )
  exit "$status"
}
trap restore_governors EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

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
  printf 'uname='; uname -srm
  printf 'python='; python3 --version
} > "$RESULT_DIR/${PHASE}-environment.txt"

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
