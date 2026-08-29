#!/usr/bin/env bash
# Stage a pinned Steam Runtime path-strategy bundle, then run and retrieve it.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$HERE/../.." && pwd)"
DECK_HOST="${DECK_HOST:-deck}"
BUILD_IMAGE="${TESS_STEAMRT_BUILD_IMAGE:-tess-path-strategy-steamrt4:local}"
BUILD_JOBS="${TESS_STEAMRT_BUILD_JOBS:-1}"
REMOTE_ROOT="${TESS_PATH_STRATEGY_REMOTE_ROOT:-tess-path-strategy-campaign}"
FROZEN_IMAGE="registry.gitlab.steamos.cloud/steamrt/steamrt4/sdk"
FROZEN_IMAGE+="@sha256:584939ebd7d2f1eec719e771fdde4ae3bd469ee741c783abb7fe812ddaaf3ee4"

die() { printf '!! %s\n' "$*" >&2; exit 1; }
simple() { case "$1" in ''|-*|*[!A-Za-z0-9._-]*) return 1;; esac; }

verify_bundle() {
  local bundle="$1"
  (
    cd "$bundle"
    diff -u \
      <(sed -E 's/^[0-9a-f]{64}  //' SHA256SUMS | LC_ALL=C sort) \
      <(find . -type f ! -name SHA256SUMS -print | sed 's#^./##' | LC_ALL=C sort)
    shasum -a 256 -c SHA256SUMS
  ) || die "bundle validation failed"
}

stage() {
  [ "$#" -eq 1 ] || die "stage needs one empty bundle directory"
  local bundle="$1" git_common image_id source_commit
  mkdir -p "$bundle"
  bundle="$(cd "$bundle" && pwd)"
  [ -z "$(find "$bundle" -mindepth 1 -print -quit)" ] \
    || die "bundle directory must be empty"
  source_commit="$(git -C "$REPO_ROOT" rev-parse HEAD)"
  git_common="$(git -C "$REPO_ROOT" rev-parse --path-format=absolute --git-common-dir)"
  [ -z "$(git -C "$REPO_ROOT" status --porcelain --untracked-files=all)" ] \
    || die "staging requires a clean publication commit"
  docker build --platform linux/amd64 \
    --build-arg "STEAMRT_IMAGE=$FROZEN_IMAGE" \
    -t "$BUILD_IMAGE" "$HERE"
  image_id="$(docker image inspect --format '{{.Id}}' "$BUILD_IMAGE")"
  case "$image_id" in
    sha256:????????????????????????????????????????????????????????????????) ;;
    *) die "invalid pinned build image ID" ;;
  esac
  mkdir -p "$bundle/bin" "$bundle/bench" "$bundle/tools/steamdeck"
  docker run --rm --platform linux/amd64 \
    -v "$REPO_ROOT:/src:ro" -v "$bundle:/stage" -w /src \
    -v "$git_common:$git_common:ro" "$BUILD_IMAGE" bash -ceu '
      cmake --fresh --preset linux-bench -B /stage/build
      cmake --build /stage/build --parallel '"$BUILD_JOBS"' \
        --target tess_bench_path_strategy_crossover
      cp /stage/build/bench/tess_bench_path_strategy_crossover /stage/bin/
      { clang++ --version | head -n1; cmake --version | head -n1; } \
        > /stage/build-environment.txt
      rm -rf /stage/build
    '
  cp "$REPO_ROOT/bench/tess_path_strategy_crossover_bench.cc" "$bundle/bench/"
  cp "$REPO_ROOT/tools/path_strategy_campaign.py" "$bundle/tools/"
  cp "$HERE/deck-run-path-strategy-campaign.sh" "$bundle/tools/steamdeck/"
  printf '%s\n' "$source_commit" > "$bundle/source-commit.txt"
  {
    printf 'sdk=%s\n' "$FROZEN_IMAGE"
    printf 'image_id=%s\n' "$image_id"
  } > "$bundle/build-image.txt"
  chmod +x "$bundle/bin/tess_bench_path_strategy_crossover" \
    "$bundle/tools/path_strategy_campaign.py" \
    "$bundle/tools/steamdeck/deck-run-path-strategy-campaign.sh"
  (
    cd "$bundle"
    find . -type f ! -name 'SHA256SUMS*' -print \
      | sed 's#^./##' | LC_ALL=C sort \
      | xargs shasum -a 256 > SHA256SUMS.tmp
    mv SHA256SUMS.tmp SHA256SUMS
  )
  verify_bundle "$bundle"
  printf '>> staged %s\n' "$bundle"
}

run() {
  [ "$#" -eq 3 ] || die "run needs bundle, run ID, and empty results directory"
  local bundle="$1" run_id="$2" results="$3" bundle_sha remote
  bundle="$(cd "$bundle" && pwd)"
  verify_bundle "$bundle"
  simple "$DECK_HOST" || die "invalid DECK_HOST"
  simple "$REMOTE_ROOT" || die "invalid remote root"
  simple "$run_id" || die "invalid run ID"
  mkdir -p "$results"
  results="$(cd "$results" && pwd)"
  [ -z "$(find "$results" -mindepth 1 -print -quit)" ] \
    || die "results directory must be empty"
  bundle_sha="$(shasum -a 256 "$bundle/SHA256SUMS" | awk '{print $1}')"
  remote="\$HOME/$REMOTE_ROOT/$run_id"
  "$HERE/deck" doctor
  # remote is constrained above and deliberately expands HOME on the device.
  # shellcheck disable=SC2029
  ssh "$DECK_HOST" "test ! -e \"$remote\" && mkdir -p \"$remote\""
  rsync -az "$bundle/" "$DECK_HOST:$REMOTE_ROOT/$run_id/bundle/"
  set +e
  ssh -t "$DECK_HOST" \
    "bash \"$remote/bundle/tools/steamdeck/deck-run-path-strategy-campaign.sh\" \
\"$remote/bundle\" \"$remote/results\" \"$bundle_sha\""
  status=$?
  set -e
  rsync -az "$DECK_HOST:$REMOTE_ROOT/$run_id/results/" "$results/"
  (cd "$results" && sha256sum -c SHA256SUMS) \
    || die "retrieved results failed validation"
  [ "$status" -eq 0 ] || die "remote campaign failed with $status"
}

case "${1:-help}" in
  stage) shift; stage "$@" ;;
  run) shift; run "$@" ;;
  *)
    echo "usage: $0 stage <bundle-dir> | run <bundle-dir> <run-id> <results-dir>"
    ;;
esac
