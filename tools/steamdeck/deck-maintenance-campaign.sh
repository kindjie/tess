#!/usr/bin/env bash
# Build and stage the exact MNT-3 Deck evidence bundle locally, then run one
# whole governor-pinned phase when the physical device is explicitly available.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${HERE}/../.." && pwd)"
BUILD_IMAGE="${TESS_STEAMRT_BUILD_IMAGE:-tess-maintenance-steamrt4:local}"
BUILD_JOBS="${TESS_STEAMRT_BUILD_JOBS:-1}"
DECK_HOST="${DECK_HOST:-deck}"
REMOTE_ROOT="${TESS_MAINTENANCE_REMOTE_ROOT:-tess-maintenance-campaign}"
BUILD_CONTEXT="steamrt4@sha256:584939ebd7d2f1eec719e771fdde4ae3b"
BUILD_CONTEXT+="d469ee741c783abb7fe812ddaaf3ee4"
FROZEN_STEAMRT_IMAGE="registry.gitlab.steamos.cloud/steamrt/steamrt4/sdk"
FROZEN_STEAMRT_IMAGE+="@sha256:584939ebd7d2f1eec719e771fdde4ae3b"
FROZEN_STEAMRT_IMAGE+="d469ee741c783abb7fe812ddaaf3ee4"

usage() {
  cat <<'EOF'
Usage:
  tools/steamdeck/deck-maintenance-campaign.sh stage <empty-bundle-dir>
  tools/steamdeck/deck-maintenance-campaign.sh run <bundle-dir> \
    <calibration|candidate> <run-id> <local-results-dir>

`stage` performs no Deck access. `run` executes `tools/steamdeck/deck doctor`
immediately before transfer, pins the governor for the entire selected phase,
and retrieves all raw results even when the remote phase fails.
EOF
}

die() {
  printf '!! %s\n' "$*" >&2
  exit 1
}

validate_simple_name() {
  case "$1" in
    ''|-*|*[!A-Za-z0-9._-]*) die "invalid $2" ;;
  esac
}

verify_bundle() {
  local bundle="$1"
  [ -f "$bundle/SHA256SUMS" ] || die "bundle has no SHA256SUMS"
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
    shasum -a 256 -c SHA256SUMS || exit 1
  ) || die "bundle checksum or inventory validation failed"
}

verify_result_set() {
  local directory="$1" phase="$2" manifest
  manifest="${phase}-SHA256SUMS"
  [ -f "$directory/$manifest" ] || die "missing ${manifest}"
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
    diff -u \
      <(
        if [ "$phase" = "calibration" ]; then
          printf '%s\n' './calibration-SHA256SUMS'
        else
          printf '%s\n' \
            './calibration-SHA256SUMS' './candidate-SHA256SUMS'
        fi
      ) \
      <(find . -type f -name '*-SHA256SUMS' -print | LC_ALL=C sort) \
      || exit 1
    shasum -a 256 -c "$manifest" || exit 1
  ) || die "${phase} result set failed checksum or completeness validation"
}

stage_bundle() {
  [ "$#" -eq 1 ] || die "stage needs one bundle directory"
  local bundle="$1" git_common_dir image_id source_sha
  mkdir -p "$bundle"
  bundle="$(cd "$bundle" && pwd)"
  [ -z "$(find "$bundle" -mindepth 1 -maxdepth 1 -print -quit)" ] \
    || die "bundle directory must be empty"
  source_sha="$(git -C "$REPO_ROOT" rev-parse HEAD)"
  git_common_dir="$(
    git -C "$REPO_ROOT" rev-parse --path-format=absolute --git-common-dir
  )"
  [ -d "$git_common_dir" ] || die "cannot resolve the common Git directory"
  [ -z "$(
    git -C "$REPO_ROOT" status --porcelain --untracked-files=all \
      --ignored=matching
  )" ] || die "staging requires a completely clean source tree"
  case "$BUILD_JOBS" in
    ''|0|*[!0-9]*) die "invalid TESS_STEAMRT_BUILD_JOBS" ;;
  esac

  docker build --platform linux/amd64 \
    --build-arg "STEAMRT_IMAGE=${FROZEN_STEAMRT_IMAGE}" \
    -t "$BUILD_IMAGE" "$HERE"
  image_id="$(docker image inspect --format '{{.Id}}' "$BUILD_IMAGE")"
  case "$image_id" in
    sha256:????????????????????????????????????????????????????????????????) ;;
    *) die "built Steam Runtime wrapper has an invalid image ID" ;;
  esac

  mkdir -p "$bundle/bin" "$bundle/bench" "$bundle/tools/steamdeck"
  docker run --rm --platform linux/amd64 \
    -e "SOURCE_SHA=${source_sha}" \
    -e "BUILD_CONTEXT=${BUILD_CONTEXT}" \
    -e "BUILD_JOBS=${BUILD_JOBS}" \
    -e "CONTAINER_IMAGE_ID=${image_id}" \
    -e GIT_OPTIONAL_LOCKS=0 \
    -v "${REPO_ROOT}:/src:ro" -v "${bundle}:/stage" -w /src \
    -v "${git_common_dir}:${git_common_dir}:ro" \
    "$image_id" bash -ceu '
      cmake --fresh --preset linux-bench -B /stage/build
      cmake --build /stage/build --parallel "$BUILD_JOBS" \
        --target tess_bench_maintenance_campaign
      python3 tools/maintenance_campaign.py build-manifest \
        --source-root /src \
        --source-sha "$SOURCE_SHA" \
        --binary /stage/build/bench/tess_bench_maintenance_campaign \
        --config bench/maintenance-campaign.json \
        --compiler /usr/bin/clang++ \
        --compile-commands /stage/build/compile_commands.json \
        --link-command /stage/build/bench/CMakeFiles/\
tess_bench_maintenance_campaign.dir/link.txt \
        --device steam-deck \
        --build-context "$BUILD_CONTEXT" \
        --container-image-id "$CONTAINER_IMAGE_ID" \
        --output /stage/build-manifest.json
      cp /stage/build/bench/tess_bench_maintenance_campaign \
        /stage/bin/
      rm -rf /stage/build
    '
  cp "$REPO_ROOT/bench/maintenance-campaign.json" "$bundle/bench/"
  cp "$REPO_ROOT/bench/tess_maintenance_campaign_bench.cc" "$bundle/bench/"
  cp "$REPO_ROOT/tools/maintenance_campaign.py" "$bundle/tools/"
  cp "$HERE/deck-run-maintenance-campaign.sh" "$bundle/tools/steamdeck/"
  chmod +x "$bundle/bin/tess_bench_maintenance_campaign"
  chmod +x "$bundle/tools/maintenance_campaign.py"
  chmod +x "$bundle/tools/steamdeck/deck-run-maintenance-campaign.sh"
  (
    cd "$bundle"
    shasum -a 256 \
      bench/maintenance-campaign.json \
      bench/tess_maintenance_campaign_bench.cc \
      bin/tess_bench_maintenance_campaign \
      build-manifest.json \
      tools/maintenance_campaign.py \
      tools/steamdeck/deck-run-maintenance-campaign.sh \
      > SHA256SUMS
  )
  verify_bundle "$bundle"
  printf '>> staged exact Deck bundle at %s\n' "$bundle"
  printf '>> source %s\n' "$source_sha"
}

run_phase() {
  [ "$#" -eq 4 ] \
    || die "run needs bundle, phase, run ID, and results directory"
  local bundle="$1" phase="$2" run_id="$3" results="$4"
  local bundle_sha source_sha remote_dir status
  local remote_bundle remote_results remote_runner
  bundle="$(cd "$bundle" && pwd)"
  verify_bundle "$bundle"
  bundle_sha="$(shasum -a 256 "$bundle/SHA256SUMS" | awk '{print $1}')"
  validate_simple_name "$bundle_sha" "bundle identity"
  case "$phase" in
    calibration|candidate) ;;
    *) die "phase must be calibration or candidate" ;;
  esac
  validate_simple_name "$DECK_HOST" "DECK_HOST"
  validate_simple_name "$REMOTE_ROOT" "remote campaign directory"
  validate_simple_name "$run_id" "campaign run ID"
  source_sha="$(python3 -c \
    'import json,sys; print(json.load(open(sys.argv[1]))["source_sha"])' \
    "$bundle/build-manifest.json")"
  validate_simple_name "$source_sha" "bundle source SHA"
  remote_dir="${REMOTE_ROOT}-${source_sha}"
  remote_bundle="\$HOME/${remote_dir}/runs/${run_id}/bundle"
  remote_results="\$HOME/${remote_dir}/runs/${run_id}/results"
  remote_runner="${remote_bundle}/tools/steamdeck/"
  remote_runner+="deck-run-maintenance-campaign.sh"
  if [ "$phase" = "calibration" ]; then
    mkdir -p "$results"
    results="$(cd "$results" && pwd)"
    [ -z "$(find "$results" -mindepth 1 -maxdepth 1 -print -quit)" ] \
      || die "calibration results directory must be empty"
  else
    results="$(cd "$results" && pwd)"
    [ -f "$results/calibration.json" ] \
      && [ -f "$results/thresholds.json" ] \
      || die "candidate results need retained calibration and thresholds"
    [ ! -e "$results/candidate.json" ] && [ ! -e "$results/report.json" ] \
      || die "candidate results already exist; use a new run ID"
    verify_result_set "$results" calibration
  fi

  # Required immediately before the only commands that contact the Deck.
  "${HERE}/deck" doctor
  # remote_dir and phase have already been restricted to simple values.
  # shellcheck disable=SC2029
  if [ "$phase" = "calibration" ]; then
    # The run directory must not exist; rsync creates its bundle exactly once.
    # shellcheck disable=SC2029
    ssh "$DECK_HOST" "test ! -e \"\$HOME/${remote_dir}/runs/${run_id}\" \
      && mkdir -p \"\$HOME/${remote_dir}/runs/${run_id}\""
    rsync -az "$bundle/" \
      "$DECK_HOST:${remote_dir}/runs/${run_id}/bundle/"
  else
    # Candidate consumes the immutable bundle retained by calibration.
    # shellcheck disable=SC2029
    ssh "$DECK_HOST" "test -d \"${remote_bundle}\""
  fi
  set +e
  # shellcheck disable=SC2029
  ssh -t "$DECK_HOST" \
    "bash \"${remote_runner}\" \"${phase}\" \
\"${remote_bundle}\" \"${remote_results}\" \"${bundle_sha}\""
  status=$?
  set -e
  rsync -az "$DECK_HOST:${remote_dir}/runs/${run_id}/results/" "$results/"
  verify_result_set "$results" "$phase"
  [ "$status" -eq 0 ] || die "remote ${phase} phase failed with ${status}"
  printf '>> retrieved %s results at %s\n' "$phase" "$results"
}

command="${1:-help}"
shift 2>/dev/null || true
case "$command" in
  stage) stage_bundle "$@" ;;
  run) run_phase "$@" ;;
  help|-h|--help) usage ;;
  *) usage >&2; die "unknown campaign command" ;;
esac
