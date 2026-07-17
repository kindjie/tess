#!/usr/bin/env bash
# Start a long-lived Steam Runtime build container on the development host and
# configure a preset. The repository is bind-mounted, so builds and tests run
# against the live source tree. Re-run to refresh the image or container.
#
# Usage: tools/steamdeck/container-up.sh [configure-preset]
# Default preset: linux-dev
set -euo pipefail

BUILD_IMAGE="${TESS_STEAMRT_BUILD_IMAGE:-tess-steamrt4:local}"
DEFAULT_STEAMRT_IMAGE="registry.gitlab.steamos.cloud/steamrt/steamrt4/sdk"
DEFAULT_STEAMRT_IMAGE+="@sha256:584939ebd7d2f1eec719e771fdde4ae3b"
DEFAULT_STEAMRT_IMAGE+="d469ee741c783abb7fe812ddaaf3ee4"
STEAMRT_IMAGE="${TESS_STEAMRT_IMAGE:-${DEFAULT_STEAMRT_IMAGE}}"
STEAMRT_IMAGE_LABEL="dev.tess.steamrt-image"
CONTAINER="${TESS_STEAMRT_CONTAINER:-tess-rt}"
PRESET="${1:-${PRESET:-linux-dev}}"

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${HERE}/../.." && pwd)"

# Re-run the cached build every time so changing STEAMRT_IMAGE cannot silently
# reuse a tag built from a different base. With unchanged inputs this is an
# idempotent cache lookup.
echo ">> ensuring ${BUILD_IMAGE} uses ${STEAMRT_IMAGE} (amd64) ..."
docker build --platform linux/amd64 \
  --build-arg "STEAMRT_IMAGE=${STEAMRT_IMAGE}" \
  -t "${BUILD_IMAGE}" "${HERE}"

# (Re)start the container with the repo bind-mounted at /src.
docker rm -f "${CONTAINER}" >/dev/null 2>&1 || true
docker run -d --name "${CONTAINER}" --platform linux/amd64 \
  --label "${STEAMRT_IMAGE_LABEL}=${STEAMRT_IMAGE}" \
  -v "${REPO_ROOT}":/src -w /src "${BUILD_IMAGE}" sleep infinity >/dev/null

# Configure the preset (first run fetches GoogleTest/Benchmark via FetchContent;
# the build/ tree is bind-mounted, so it is cached on the host afterwards).
docker exec "${CONTAINER}" cmake --preset "${PRESET}"

cat <<EOF
>> container '${CONTAINER}' is up, preset '${PRESET}' configured.

Inner loop (rebuild + test on save):
  watchexec -e cc,h,hpp,cpp,cmake,json -- \\
    docker exec ${CONTAINER} sh -c \\
    'cmake --build --preset ${PRESET} && ctest --preset ${PRESET}'

One-shot:
  docker exec ${CONTAINER} sh -c 'cmake --build --preset ${PRESET} && ctest --preset ${PRESET}'

Sanitizers:  container-up.sh linux-asan
On device:   tools/steamdeck/deck-bench.sh
EOF
