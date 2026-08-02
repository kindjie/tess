#!/usr/bin/env bash
# Run the tess benchmark campaign on a GCE bare-metal instance.
#
# Redesign section 8's cloud bare-metal tier: hardware counters and a
# quiet machine, for the numbers the shared-runner pool cannot produce.
# Bare metal is the expensive tier -- roughly $10/hour, against about
# $0.12 for a full sweep on a c3-standard-4 -- so everything here is
# arranged around never leaving one running.
#
# FOUR independent cleanup mechanisms, any one of which is sufficient:
#
#   1. GCE enforces --max-run-duration with --instance-termination-action
#      =DELETE. This holds even if the instance never boots, the startup
#      script dies, or this driver is SIGKILLed.
#   2. The startup script self-deletes at the end, success or failure,
#      with its trap armed as soon as the delete arguments are known.
#   3. This driver's EXIT/INT/TERM trap deletes the instance if it is
#      interrupted.
#   4. reap_orphans.sh finds anything the other three missed, by label,
#      across every zone. Run it after a campaign; it is cheap and exits
#      0 when clean.
#
# --boot-disk-auto-delete matters as much as the instance deletes: a
# deleted instance can still leave a billing disk behind otherwise.
set -euo pipefail

PROJECT="${TESS_GCP_PROJECT:-}"
BUCKET="${TESS_GCP_BUCKET:-}"
ZONE="${TESS_GCP_ZONE:-us-central1-a}"
# The smallest x86 bare-metal SKU GCE offers; no smaller variant exists.
# 192 vCPU, of which the single-threaded benchmarks use one -- the point
# is the quiet machine and the counters, not the core count.
MACHINE_TYPE="c3-standard-192-metal"
IMAGE_FAMILY="ubuntu-2404-lts-amd64"
IMAGE_PROJECT="ubuntu-os-cloud"
BOOT_DISK_SIZE_GB=100
BOOT_DISK_TYPE="pd-balanced"
# Deliberately tighter than the reference project's 90m. At metal
# pricing the cap is a cost ceiling, not just a safety net: 45m is about
# $7.50 worst case. Raise it explicitly for a longer sweep, knowing what
# that costs.
MAX_RUN_DURATION="45m"
HOURLY_USD="10.00"
ASSUME_YES=0
DRY_RUN=0

usage() {
  cat <<'EOF'
Usage: run_metal_bench.sh [options]

  --project=ID          GCP project             [$TESS_GCP_PROJECT]
  --bucket=gs://NAME    results bucket          [$TESS_GCP_BUCKET]
  --zone=ZONE           GCE zone                [$TESS_GCP_ZONE]
  --machine-type=TYPE   instance type           [c3-standard-192-metal]
  --max-run-duration=D  hard kill cap           [45m]
  --yes / -y            skip the confirmation prompt
  --dry-run             print the plan and exit; creates nothing
  --help                this text

Prints an estimated cost and asks for confirmation unless --yes.

After any campaign, run:
  tools/cloud/reap_orphans.sh --project=ID
EOF
}

for arg in "$@"; do
  case "$arg" in
    --project=*)           PROJECT="${arg#*=}" ;;
    --bucket=*)            BUCKET="${arg#*=}" ;;
    --zone=*)              ZONE="${arg#*=}" ;;
    --machine-type=*)      MACHINE_TYPE="${arg#*=}" ;;
    --max-run-duration=*)  MAX_RUN_DURATION="${arg#*=}" ;;
    --yes|-y)              ASSUME_YES=1 ;;
    --dry-run)             DRY_RUN=1 ;;
    --help|-h)             usage; exit 0 ;;
    *) echo "unknown argument: $arg" >&2; usage >&2; exit 2 ;;
  esac
done

[[ -n "$PROJECT" ]] || { echo "error: --project required" >&2; exit 2; }
[[ -n "$BUCKET" ]]  || { echo "error: --bucket required" >&2; exit 2; }
[[ "$BUCKET" == gs://* ]] || {
  echo "error: --bucket must start with gs://" >&2; exit 2; }
[[ "$MAX_RUN_DURATION" =~ ^[0-9]+[smh]$ ]] || {
  echo "error: --max-run-duration must look like 45m or 2h" >&2; exit 2; }

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
GIT_COMMIT="$(git -C "$REPO_ROOT" rev-parse HEAD)"
GIT_DIRTY=""
git -C "$REPO_ROOT" diff --quiet || GIT_DIRTY="-dirty"
RUN_ID="$(date -u +%Y%m%dT%H%M%SZ)"
# GCE labels must be lowercase. Done with tr, not ${VAR,,}, which is
# bash 4+ and fails on the bash 3.2 macOS ships.
RUN_ID_LOWER="$(printf '%s' "$RUN_ID" | tr '[:upper:]' '[:lower:]')"
INSTANCE_NAME="tess-metal-$RUN_ID"
BUCKET_PREFIX="$BUCKET/campaigns/$RUN_ID"

# Minutes, for the estimate. The cap is the worst case; a normal run
# finishes sooner and is billed for what it used.
cap_minutes() {
  # Separate `local` statements: assignments in a single `local` do not
  # take effect for later expansions on the same line, so a combined
  # form silently produced an empty unit and an empty estimate.
  local d="$1"
  local n="${d%[smh]}"
  local unit="${d: -1}"
  case "$unit" in
    s) echo $(( (n + 59) / 60 )) ;;
    m) echo "$n" ;;
    h) echo $(( n * 60 )) ;;
  esac
}
CAP_MIN="$(cap_minutes "$MAX_RUN_DURATION")"
WORST_USD="$(awk -v h="$HOURLY_USD" -v m="$CAP_MIN" \
  'BEGIN { printf "%.2f", h * m / 60 }')"

cat <<EOF

Campaign plan
  project        $PROJECT
  zone           $ZONE
  machine        $MACHINE_TYPE
  instance       $INSTANCE_NAME
  commit         ${GIT_COMMIT:0:12}${GIT_DIRTY}
  results        $BUCKET_PREFIX
  hard cap       $MAX_RUN_DURATION  (GCE deletes the instance at the cap)
  worst-case     \$${WORST_USD} at \$${HOURLY_USD}/hr

EOF

if (( DRY_RUN )); then
  echo "dry run: no instance created, nothing billed"
  exit 0
fi

if (( ! ASSUME_YES )); then
  read -r -p "create this bare-metal instance? [y/N] " reply
  [[ "$reply" == [yY] ]] || { echo "aborted"; exit 0; }
fi

# ---- Cleanup trap ---------------------------------------------------
# Armed BEFORE the instance is created, so a failure inside `instances
# create` -- which can leave a partially created instance behind -- is
# still covered.
SRC_TARBALL=""
cleanup() {
  local exit_code=$?
  [[ -n "$SRC_TARBALL" && -f "$SRC_TARBALL" ]] && rm -f "$SRC_TARBALL"
  if gcloud compute instances describe "$INSTANCE_NAME" \
       --project="$PROJECT" --zone="$ZONE" \
       --format='value(name)' >/dev/null 2>&1; then
    echo
    echo "cleanup: $INSTANCE_NAME still present; deleting..."
    gcloud compute instances delete "$INSTANCE_NAME" \
      --project="$PROJECT" --zone="$ZONE" --quiet 2>&1 | sed 's/^/  /' || {
      echo "error: cleanup FAILED; run tools/cloud/reap_orphans.sh" >&2
      exit 1
    }
  fi
  exit "$exit_code"
}
trap cleanup EXIT INT TERM

# ---- Package the working tree --------------------------------------
# Uncommitted changes included on purpose: the instance measures exactly
# what is checked out here, and the commit is recorded with -dirty so a
# result is never mistaken for a clean-tree number.
SRC_TARBALL="$(mktemp -t tess-campaign-XXXXXX.tar.gz)"
git -C "$REPO_ROOT" ls-files --exclude-standard --cached --others -z \
  | tar --null -czf "$SRC_TARBALL" -C "$REPO_ROOT" -T -
gsutil -q cp "$SRC_TARBALL" "$BUCKET_PREFIX/source.tar.gz"

STARTUP_FILE="$REPO_ROOT/tools/cloud/setup_metal_vm.sh"
[[ -f "$STARTUP_FILE" ]] || {
  echo "error: missing $STARTUP_FILE" >&2; exit 1; }

gcloud compute instances create "$INSTANCE_NAME" \
  --project="$PROJECT" \
  --zone="$ZONE" \
  --machine-type="$MACHINE_TYPE" \
  --image-family="$IMAGE_FAMILY" \
  --image-project="$IMAGE_PROJECT" \
  --boot-disk-size="${BOOT_DISK_SIZE_GB}GB" \
  --boot-disk-type="$BOOT_DISK_TYPE" \
  --boot-disk-auto-delete \
  --shielded-secure-boot --shielded-vtpm --shielded-integrity-monitoring \
  --no-restart-on-failure \
  --max-run-duration="$MAX_RUN_DURATION" \
  --instance-termination-action=DELETE \
  --labels="tess-campaign=1,tess-run-id=$RUN_ID_LOWER" \
  --scopes="https://www.googleapis.com/auth/devstorage.read_write,https://www.googleapis.com/auth/compute" \
  --metadata-from-file="startup-script=$STARTUP_FILE" \
  --metadata="^;;^tess-bucket=$BUCKET_PREFIX;;tess-source-url=$BUCKET_PREFIX/source.tar.gz;;tess-run-id=$RUN_ID;;tess-git-commit=${GIT_COMMIT}${GIT_DIRTY}"

echo
echo "instance created; it self-deletes when finished."
echo "results will appear under $BUCKET_PREFIX"
echo
echo "if this driver dies, the instance is still capped at"
echo "$MAX_RUN_DURATION by GCE. Confirm with:"
echo "  tools/cloud/reap_orphans.sh --project=$PROJECT"
