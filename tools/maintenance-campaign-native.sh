#!/usr/bin/env bash
# Stages one exact native-M3 campaign bundle, then runs immutable evidence
# phases. The identity-only provenance gate executes no benchmark cell.
set -euo pipefail

if [ "$#" -ne 2 ]; then
  echo "usage: $0 <stage|calibration|candidate> <results-directory>" >&2
  exit 2
fi

PHASE="$1"
RESULT_DIR="$2"
case "$PHASE" in
  stage|calibration|candidate) ;;
  *) echo "invalid maintenance campaign phase" >&2; exit 2 ;;
esac

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$HERE/.." && pwd)"
RESULT_DIR="$(cd "$RESULT_DIR" && pwd)"
CONFIG="$RESULT_DIR/bench/maintenance-campaign.json"
TOOL="$RESULT_DIR/tools/maintenance_campaign.py"
BINARY="$RESULT_DIR/bin/tess_bench_maintenance_campaign"
MANIFEST="$RESULT_DIR/build-manifest.json"

die() {
  printf '!! %s\n' "$*" >&2
  exit 2
}

source_is_clean_at() {
  local source_sha="$1"
  [ "$(git -C "$REPO_ROOT" rev-parse HEAD)" = "$source_sha" ] \
    && [ -z "$(
      git -C "$REPO_ROOT" status --porcelain --untracked-files=all \
        --ignored=matching
    )" ]
}

verify_stage_bundle() {
  [ -f "$RESULT_DIR/BUNDLE_SHA256SUMS" ] || return 1
  (
    cd "$RESULT_DIR"
    [ -z "$(
      find . -mindepth 1 ! -type f ! -type d -print -quit
    )" ] || exit 1
    diff -u \
      <(
        sed -E 's/^[0-9a-f]{64}  //' BUNDLE_SHA256SUMS \
          | LC_ALL=C sort
      ) \
      <(
        find . -type f ! -name BUNDLE_SHA256SUMS -print \
          | LC_ALL=C sort
      ) || exit 1
    shasum -a 256 -c BUNDLE_SHA256SUMS || exit 1
  )
}

stage_bundle() {
  local build_dir build_jobs link_command source_sha temp_root
  [ -z "$(find "$RESULT_DIR" -mindepth 1 -print -quit)" ] \
    || die "native stage directory must be empty"
  source_sha="$(git -C "$REPO_ROOT" rev-parse HEAD)"
  source_is_clean_at "$source_sha" \
    || die "native stage requires a clean source commit before building"
  build_jobs="${TESS_MAINTENANCE_BUILD_JOBS:-1}"
  case "$build_jobs" in
    ''|0|*[!0-9]*) die "invalid TESS_MAINTENANCE_BUILD_JOBS" ;;
  esac
  temp_root="$(cd "${TMPDIR:-/tmp}" && pwd -P)"
  build_dir="$(mktemp -d "$temp_root/tess-mnt3-native-stage.XXXXXX")"
  case "$build_dir" in
    "$temp_root"/tess-mnt3-native-stage.*) ;;
    *) die "cannot create controlled native build directory" ;;
  esac
  case "$build_dir/" in
    "$REPO_ROOT/"*) die "native build directory overlaps the source tree" ;;
  esac
  cleanup_stage() {
    rm -rf "$build_dir"
  }
  trap cleanup_stage EXIT

  cmake -S "$REPO_ROOT" -B "$build_dir" -G "Unix Makefiles" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_COMPILER=/usr/bin/c++ \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DTESS_BUILD_TESTING=OFF -DTESS_BUILD_EXAMPLES=OFF \
    -DTESS_BUILD_BENCHMARKS=ON -DTESS_ENABLE_WARNINGS=ON \
    -DTESS_ENABLE_ENTT=ON -DTESS_ENABLE_FLECS=ON \
    -DTESS_WARNINGS_AS_ERRORS=ON
  cmake --build "$build_dir" --parallel "$build_jobs" \
    --target tess_bench_maintenance_campaign
  link_command="$build_dir/bench/CMakeFiles/"
  link_command+="tess_bench_maintenance_campaign.dir/link.txt"
  python3 "$REPO_ROOT/tools/maintenance_campaign.py" build-manifest \
    --source-root "$REPO_ROOT" --source-sha "$source_sha" \
    --binary "$build_dir/bench/tess_bench_maintenance_campaign" \
    --config "$REPO_ROOT/bench/maintenance-campaign.json" \
    --compiler /usr/bin/c++ \
    --compile-commands "$build_dir/compile_commands.json" \
    --link-command "$link_command" \
    --device m3 --build-context macos-native-xcode \
    --output "$build_dir/build-manifest.json"
  source_is_clean_at "$source_sha" \
    || die "source changed during native stage"

  mkdir -p "$RESULT_DIR/bin" "$RESULT_DIR/bench" "$RESULT_DIR/tools"
  cp "$build_dir/bench/tess_bench_maintenance_campaign" "$RESULT_DIR/bin/"
  cp "$build_dir/build-manifest.json" "$RESULT_DIR/"
  cp "$REPO_ROOT/bench/maintenance-campaign.json" "$RESULT_DIR/bench/"
  cp "$REPO_ROOT/bench/tess_maintenance_campaign_bench.cc" \
    "$RESULT_DIR/bench/"
  cp "$REPO_ROOT/tools/maintenance_campaign.py" "$RESULT_DIR/tools/"
  cp "$REPO_ROOT/tools/maintenance-campaign-native.sh" "$RESULT_DIR/tools/"
  (
    cd "$RESULT_DIR"
    find . -type f -print \
      | LC_ALL=C sort \
      | while IFS= read -r artifact; do
          shasum -a 256 "$artifact"
        done > "$build_dir/BUNDLE_SHA256SUMS"
  )
  cp "$build_dir/BUNDLE_SHA256SUMS" "$RESULT_DIR/"
  verify_stage_bundle || die "native stage bundle validation failed"
  trap - EXIT
  cleanup_stage
  printf '>> staged exact native bundle at %s\n' "$RESULT_DIR"
  printf '>> source %s\n' "$source_sha"
}

if [ "$PHASE" = "stage" ]; then
  stage_bundle
  exit 0
fi

verify_calibration_result_set() {
  [ -f "$RESULT_DIR/calibration-SHA256SUMS" ] || return 1
  (
    cd "$RESULT_DIR"
    [ -z "$(
      find . -mindepth 1 ! -type f ! -type d -print -quit
    )" ] || exit 1
    diff -u \
      <(
        sed -E 's/^[0-9a-f]{64}  //' calibration-SHA256SUMS \
          | LC_ALL=C sort
      ) \
      <(
        find . -type f ! -name '*-SHA256SUMS' -print \
          | LC_ALL=C sort
      ) || exit 1
    [ "$(find . -type f -name '*-SHA256SUMS' -print)" \
      = './calibration-SHA256SUMS' ] || exit 1
    shasum -a 256 -c calibration-SHA256SUMS || exit 1
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
if ! source_is_clean_at "$manifest_source_sha"; then
  die "native phase requires the build manifest's clean source commit"
fi
if [ "$PHASE" = "calibration" ]; then
  verify_stage_bundle || die "native stage bundle is invalid or not pristine"
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
