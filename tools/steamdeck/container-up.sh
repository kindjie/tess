#!/usr/bin/env bash
# Start a long-lived Steam Runtime build container on the Mac and configure a
# preset. Edit natively in Neovim; the repo is bind-mounted, so builds/tests run
# against your live tree. Re-run any time to rebuild the image or reset the
# container.
#
# Usage:  tools/steamdeck/container-up.sh [configure-preset]   (default linux-dev)
set -euo pipefail

BUILD_IMAGE="${TESS_STEAMRT_BUILD_IMAGE:-tess-steamrt4:local}"
CONTAINER="${TESS_STEAMRT_CONTAINER:-tess-rt}"
PRESET="${1:-${PRESET:-linux-dev}}"

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${HERE}/../.." && pwd)"

# Build the SDK+clang image if it is missing (idempotent; cached afterwards).
if ! docker image inspect "${BUILD_IMAGE}" >/dev/null 2>&1; then
  echo ">> building ${BUILD_IMAGE} (amd64) ..."
  docker build --platform linux/amd64 -t "${BUILD_IMAGE}" "${HERE}"
fi

# (Re)start the container with the repo bind-mounted at /src.
docker rm -f "${CONTAINER}" >/dev/null 2>&1 || true
docker run -d --name "${CONTAINER}" --platform linux/amd64 \
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
