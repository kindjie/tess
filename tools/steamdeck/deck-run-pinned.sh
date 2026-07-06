#!/usr/bin/env bash
# Runs ON the Steam Deck. Pins the CPU governor to 'performance' for stable
# benchmark timings, runs tess_bench, then restores the original governor on
# exit (including Ctrl-C / errors). Needs sudo on the Deck.
#
# Invoked by `tools/steamdeck/deck-bench.sh --pin`; not meant to be run from the
# Mac directly. All args are forwarded to tess_bench.
#
# Note: this fixes CPU *frequency scaling* (the Google Benchmark warning). The
# Deck's TDP/thermal cap still applies; raising it needs SteamOS power controls
# (non-stock) and is out of scope here.
set -u

BIN_DIR="${TESS_BENCH_DIR:-$HOME/tess-bench}"
gov() { cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null; }

set_gov() {
  if command -v cpupower >/dev/null 2>&1; then
    sudo cpupower frequency-set -g "$1" >/dev/null
  else
    sudo sh -c "for f in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do echo $1 > \"\$f\"; done"
  fi
}

OLD="$(gov)"
echo ">> [deck] governor: ${OLD:-?} -> performance (sudo; enter Deck password if prompted)"
set_gov performance

restore() {
  [ -n "$OLD" ] || return 0
  set_gov "$OLD" 2>/dev/null && echo ">> [deck] governor restored -> $OLD"
}
trap restore EXIT INT TERM

echo ">> [deck] governor now: $(gov)  |  load: $(cut -d' ' -f1-3 /proc/loadavg)"
bin="$(find "$BIN_DIR" -type f -name tess_bench | head -n1)"
[ -n "$bin" ] || { echo ">> [deck] tess_bench not found under $BIN_DIR" >&2; exit 1; }
"$bin" "$@"
