#!/usr/bin/env bash
# Build the benchmark locally in the Steam Runtime container, ship the binaries
# to the Steam Deck, and run them on the real Zen 2 hardware.
#
# By default the binary runs DIRECTLY on stock SteamOS -- no container, nothing
# installed. Binaries built in the steamrt4 SDK require only up to GLIBC_2.38,
# and SteamOS ships glibc 2.41 (GLIBCXX_3.4.34), so they run as-is. This is the
# closest-to-stock demonstration that it works on real hardware.
#
# Flags / env:
#   --pin  (or PIN_GOVERNOR=1)  pin the CPU governor to 'performance' for the
#          run and restore it afterward -- removes the "CPU scaling is enabled"
#          noise for accurate numbers. Needs sudo on the Deck, so it runs
#          interactively (you type the Deck password once). Run it yourself:
#          `tools/steamdeck/deck-bench.sh --pin`  (not via a non-interactive job).
#   USE_CONTAINER=1  run inside the steamrt4 SDK image on the Deck instead of
#          direct (guaranteed ABI match; needs the image pulled there). Cannot
#          be combined with --pin.
#
# Prereqs: tools/steamdeck/container-up.sh linux-bench   (build container up)
#          SSH access to the Deck as `deck` (see README).
#
# Usage:  DECK_HOST=deck tools/steamdeck/deck-bench.sh [--pin] [tess_bench args...]
set -euo pipefail

CONTAINER="${TESS_STEAMRT_CONTAINER:-tess-rt}"
PRESET="${PRESET:-linux-bench}"
BENCH_BIN="${BENCH_BIN:-tess_bench}"
DECK="${DECK_HOST:-deck}"
DECK_DIR="${DECK_DIR:-tess-bench}"        # relative to the Deck user's $HOME
USE_CONTAINER="${USE_CONTAINER:-0}"
PIN_GOVERNOR="${PIN_GOVERNOR:-0}"
DEFAULT_STEAMRT_IMAGE="registry.gitlab.steamos.cloud/steamrt/steamrt4/sdk"
DEFAULT_STEAMRT_IMAGE+="@sha256:584939ebd7d2f1eec719e771fdde4ae3b"
DEFAULT_STEAMRT_IMAGE+="d469ee741c783abb7fe812ddaaf3ee4"
RUN_IMAGE="${TESS_STEAMRT_IMAGE:-${DEFAULT_STEAMRT_IMAGE}}"
CONTAINER_RUNTIME="${DECK_CONTAINER_RUNTIME:-podman}"

# DECK is passed to ssh-family commands. Keep it to a simple alias so a value
# beginning with '-' cannot become an option and config syntax cannot leak in.
case "${DECK}" in
  ''|[!A-Za-z0-9]*|*[!A-Za-z0-9._-]*)
    echo "!! invalid DECK_HOST: use a simple SSH alias" >&2
    exit 1
    ;;
esac

# DECK_DIR is rsync'd with --delete and spliced into remote commands: reject
# values that would escape $HOME (or wipe it) before anything touches the Deck.
case "${DECK_DIR}" in
  ''|.|..|/*|-*|*/-*|./*|*/.|*/./*|../*|*/..|*/../*|*//*|*/)
    echo "!! invalid DECK_DIR: use a normalized relative path in HOME" >&2
    exit 1
    ;;
  *[!A-Za-z0-9._/-]*)
    echo "!! invalid DECK_DIR: only [A-Za-z0-9._/-] is allowed" >&2
    exit 1
    ;;
esac

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${HERE}/../.." && pwd)"

# Parse --pin out of the args; everything else is forwarded to tess_bench.
ARGS=()
for a in "$@"; do
  case "$a" in
    --pin) PIN_GOVERNOR=1 ;;
    *) ARGS+=("$a") ;;
  esac
done
BENCH_ARGS=("${ARGS[@]:---benchmark_min_time=0.20s}")

# The remote side of ssh re-parses the command line with the login shell. The
# alias and path are constrained above; remaining remote values are %q-quoted.
# All three run branches use the same BENCH_ARGS_Q/BENCH_BIN_Q.
BENCH_ARGS_Q="$(printf '%q ' "${BENCH_ARGS[@]}")"
BENCH_BIN_Q="$(printf '%q' "${BENCH_BIN}")"
RUN_IMAGE_Q="$(printf '%q' "${RUN_IMAGE}")"
CONTAINER_RUNTIME_Q="$(printf '%q' "${CONTAINER_RUNTIME}")"

# 1. Build the benchmark locally in the container.
echo ">> building ${PRESET} in container '${CONTAINER}' ..."
docker exec "${CONTAINER}" cmake --build --preset "${PRESET}" -j

# 2. Ship the built tree to the Deck (whole tree so bench/ paths resolve).
echo ">> rsyncing build/${PRESET}/ -> ${DECK}:~/${DECK_DIR}/ ..."
rsync -az --delete "${REPO_ROOT}/build/${PRESET}/" "${DECK}:${DECK_DIR}/"

# 3. Run on the Deck's real Zen 2 hardware.
if [ "${USE_CONTAINER}" = "1" ]; then
  [ "${PIN_GOVERNOR}" = "1" ] && echo ">> note: --pin is ignored with USE_CONTAINER=1" >&2
  echo ">> running ${BENCH_BIN} on ${DECK} inside ${RUN_IMAGE} ..."
  # The container's sh gets the binary name and bench args as positional
  # parameters, so nothing user-controlled is spliced into its -c script.
  ssh "${DECK}" "${CONTAINER_RUNTIME_Q} run --rm \
    -v \"\$HOME/${DECK_DIR}:/b\" -w /b ${RUN_IMAGE_Q} \
    sh -c 'bin=\$(find . -type f -name \"\$1\" | head -n1); \
           [ -n \"\$bin\" ] || { echo \"\$1 not found under /b\" >&2; exit 1; }; \
           shift; exec \"\$bin\" \"\$@\"' run-bench ${BENCH_BIN_Q} ${BENCH_ARGS_Q}"
elif [ "${PIN_GOVERNOR}" = "1" ]; then
  # Ship the on-Deck pin helper (kept outside DECK_DIR so --delete won't drop it),
  # then run it over a TTY so sudo can prompt for the Deck password.
  echo ">> pinning governor + running ${BENCH_BIN} on ${DECK} (sudo may prompt) ..."
  scp -q "${HERE}/deck-run-pinned.sh" "${DECK}:.tess-deck-run-pinned.sh"
  ssh -t "${DECK}" "TESS_BENCH_DIR=\"\$HOME/${DECK_DIR}\" \
    bash .tess-deck-run-pinned.sh ${BENCH_ARGS_Q}"
else
  echo ">> running ${BENCH_BIN} DIRECTLY on stock SteamOS (no container) ..."
  ssh "${DECK}" "cd \"\$HOME/${DECK_DIR}\" && \
    bin=\$(find . -type f -name ${BENCH_BIN_Q} | head -n1); \
    [ -n \"\$bin\" ] || { echo \"benchmark binary not found under ~/${DECK_DIR}\" >&2; exit 1; }; \
    exec \"\$bin\" ${BENCH_ARGS_Q}"
fi
