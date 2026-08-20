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

mkdir -p "$RESULT_DIR"
cd "$BUNDLE"
sha256sum -c SHA256SUMS

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

snapshot_governors "$RESULT_DIR/governor-before.txt"
[ -s "$RESULT_DIR/governor-before.txt" ] \
  || { echo "cannot read CPU governors" >&2; exit 2; }

restore_governors() {
  local status=$? governor target
  trap - EXIT
  while IFS=: read -r governor target; do
    case "$governor:$target" in
      /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor:[A-Za-z0-9_-]*)
        printf '%s\n' "$target" | sudo tee "$governor" >/dev/null || true
        ;;
      *) echo "refusing invalid saved governor entry" >&2 ;;
    esac
  done < "$RESULT_DIR/governor-before.txt"
  snapshot_governors "$RESULT_DIR/governor-after.txt"
  printf '%s\n' "$status" > "$RESULT_DIR/phase-exit-status.txt"
  exit "$status"
}
trap restore_governors EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

set_governors performance
snapshot_governors "$RESULT_DIR/governor-during.txt"
if grep -v ':performance$' "$RESULT_DIR/governor-during.txt" >/dev/null; then
  echo "not every CPU governor is pinned to performance" >&2
  exit 2
fi
{
  date -u '+utc=%Y-%m-%dT%H:%M:%SZ'
  printf 'loadavg='; cat /proc/loadavg
  printf 'uname='; uname -a
  printf 'python='; python3 --version
} > "$RESULT_DIR/${PHASE}-environment.txt"
exec > >(tee "$RESULT_DIR/${PHASE}.stdout.log") \
  2> >(tee "$RESULT_DIR/${PHASE}.stderr.log" >&2)

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
    --repetitions "$REPETITIONS" --minimum-time "$MINIMUM_TIME" \
    --seed "$SEED" --output "$RESULT_DIR/candidate.json"
  python3 "$TOOL" analyze \
    --input "$RESULT_DIR/candidate.json" --config "$CONFIG" \
    --thresholds "$RESULT_DIR/thresholds.json" \
    --output "$RESULT_DIR/report.json"
fi
