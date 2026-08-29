#!/usr/bin/env bash
# Verify and run the path-strategy bundle on a Steam Deck under one pinned
# governor interval. This script runs on the device.
set -euo pipefail

[ "$#" -eq 3 ] || {
  echo "usage: $0 <bundle> <results> <bundle-sha256>" >&2
  exit 2
}
BUNDLE="$1"
RESULTS="$2"
EXPECTED_SHA="$3"
case "$BUNDLE:$RESULTS:$EXPECTED_SHA" in
  "$HOME"/*:"$HOME"/*:[0-9a-f][0-9a-f]*) ;;
  *) echo "invalid campaign paths or identity" >&2; exit 2 ;;
esac

(
  cd "$BUNDLE"
  sha256sum -c SHA256SUMS
)
[ "$(sha256sum "$BUNDLE/SHA256SUMS" | awk '{print $1}')" = "$EXPECTED_SHA" ] \
  || { echo "bundle identity differs from host" >&2; exit 2; }
[ ! -e "$RESULTS" ] || { echo "results directory already exists" >&2; exit 2; }
mkdir -p "$RESULTS"

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

snapshot_governors "$RESULTS/governor-before.txt"
[ -s "$RESULTS/governor-before.txt" ] \
  || { echo "cannot read CPU governors" >&2; exit 2; }
governors_changed=0

restore() {
  local status=$? governor target restore_status=0
  trap - EXIT
  if [ "$governors_changed" -eq 1 ]; then
    while IFS=: read -r governor target; do
      case "$governor:$target" in
        /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor:[A-Za-z0-9_-]*)
          printf '%s\n' "$target" | sudo tee "$governor" >/dev/null \
            || restore_status=1
          ;;
        *) restore_status=1 ;;
      esac
    done < "$RESULTS/governor-before.txt"
  fi
  snapshot_governors "$RESULTS/governor-after.txt" || restore_status=1
  cmp -s "$RESULTS/governor-before.txt" "$RESULTS/governor-after.txt" \
    || restore_status=1
  printf '%s\n' "$status" > "$RESULTS/exit-status.txt"
  (
    cd "$RESULTS"
    find . -type f ! -name 'SHA256SUMS*' -print0 | sort -z \
      | xargs -0 sha256sum > SHA256SUMS.tmp
    mv SHA256SUMS.tmp SHA256SUMS
  )
  [ "$status" -ne 0 ] && exit "$status"
  [ "$restore_status" -eq 0 ] || exit 2
}
trap restore EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

if grep -qv ':performance$' "$RESULTS/governor-before.txt"; then
  set_governors performance
  governors_changed=1
fi
snapshot_governors "$RESULTS/governor-during.txt"
grep -qv ':performance$' "$RESULTS/governor-during.txt" \
  && { echo "not every governor is pinned" >&2; exit 2; }
external_power=0
for online in /sys/class/power_supply/*/online; do
  [ -f "$online" ] || continue
  [ "$(cat "$online")" = 1 ] && external_power=1
done
[ "$external_power" -eq 1 ] \
  || { echo "external power is required" >&2; exit 2; }

memory_kib="$(awk '/^MemTotal:/{print $2}' /proc/meminfo)"
cat > "$RESULTS/environment.json" <<EOF
{
  "platform": "Steam Deck AMD Custom APU 0932",
  "memory_bytes": $((memory_kib * 1024)),
  "operating_system": "Linux $(uname -r)",
  "compiler": "$(head -n1 "$BUNDLE/build-environment.txt")",
  "cmake": "$(sed -n '2p' "$BUNDLE/build-environment.txt")",
  "build_config": "linux-bench Release; project warnings as errors",
  "source_commit": "$(cat "$BUNDLE/source-commit.txt")",
  "affinity": "CPU 2",
  "power": "external power; performance governor",
  "notes": "Pinned Steam Runtime 4 SDK; single-threaded CPU time."
}
EOF

common=(
  --binary "$BUNDLE/bin/tess_bench_path_strategy_crossover"
  --source "$BUNDLE/bench/tess_path_strategy_crossover_bench.cc"
  --environment "$RESULTS/environment.json"
  --memory-limit-gib 12
  --cpu 2
)
python3 "$BUNDLE/tools/path_strategy_campaign.py" primary \
  "${common[@]}" --output "$RESULTS/primary.json" --minimum-time 0.01 \
  --timeout 60
python3 "$BUNDLE/tools/path_strategy_campaign.py" capacity \
  "${common[@]}" --output "$RESULTS/capacity.json" --minimum-time 0.001 \
  --timeout 20
