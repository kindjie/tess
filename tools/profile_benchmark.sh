#!/usr/bin/env sh
set -eu

usage() {
  cat <<'USAGE'
Usage: tools/profile_benchmark.sh [options] [-- benchmark args...]

Options:
  --preset NAME       Built CMake preset to profile (default: bench-profile)
  --filter REGEX      Google Benchmark filter (default: path/astar_open_2d_1024x1024)
  --min-time VALUE    Google Benchmark minimum time (default: 10s)
  --rate HZ           samply sample rate (default: 2000)
  --output PATH       profile output (default: $TMPDIR/tess-profile.json.gz)
  --no-presymbolicate skip samply presymbolication sidecar
  -h, --help          show this help

Additional benchmark args after -- are passed to tess_bench.

Build the profiling preset before running this tool:
  cmake --build --preset bench-profile --target tess_bench

This tool prints a command instead of running samply itself. Run the printed
command as its own separate shell command. In managed macOS sandboxes, samply
can fail with Unknown(1100) when any helper process runs immediately before it
in the same command.
USAGE
}

print_arg() {
  quoted=$(printf "%s" "$1" | sed "s/'/'\\\\''/g")
  printf " '%s'" "$quoted"
}

preset="bench-profile"
filter="path/astar_open_2d_1024x1024"
min_time="10s"
rate="2000"
output="${TMPDIR:-.}/tess-profile.json.gz"
presymbolicate="1"

while [ "$#" -gt 0 ]; do
  case "$1" in
    --preset)
      preset="$2"
      shift 2
      ;;
    --filter)
      filter="$2"
      shift 2
      ;;
    --min-time)
      min_time="$2"
      shift 2
      ;;
    --rate)
      rate="$2"
      shift 2
      ;;
    --output)
      output="$2"
      shift 2
      ;;
    --no-presymbolicate)
      presymbolicate="0"
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    --)
      shift
      break
      ;;
    *)
      echo "unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

bench="./build/${preset}/bench/tess_bench"
out_dir=$(dirname "$output")
mkdir -p "$out_dir"

if [ ! -x "$bench" ]; then
  echo "missing benchmark binary: $bench" >&2
  echo "build it first with:" >&2
  echo "  cmake --build --preset $preset --target tess_bench" >&2
  exit 1
fi

if [ "$presymbolicate" = "1" ]; then
  set -- --unstable-presymbolicate -- "$bench" \
    "--benchmark_filter=$filter" "--benchmark_min_time=$min_time" "$@"
else
  set -- -- "$bench" "--benchmark_filter=$filter" \
    "--benchmark_min_time=$min_time" "$@"
fi

printf "samply"
print_arg record
print_arg --save-only
print_arg --rate
print_arg "$rate"
print_arg --output
print_arg "$output"
while [ "$#" -gt 0 ]; do
  print_arg "$1"
  shift
done
printf "\n"
