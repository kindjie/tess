#!/usr/bin/env bash
# Run the tess benchmark campaign on a GCE bare-metal instance.
#
# Redesign section 8's cloud tier: hardware counters and a quiet machine,
# for numbers the shared-runner pool cannot produce. Bare metal is about
# $10/hour against $0.12 for a full sweep on a small shared instance, so
# everything here is arranged around never leaving one running.
#
# Cleanup mechanisms, in the order they would actually save you:
#
#   1. The instance self-deletes at the end of its startup script,
#      success or failure, with the trap armed before any fallible work.
#   2. This driver's INT/TERM trap deletes the instance if interrupted
#      BEFORE the handoff point. After a successful create the trap is
#      disarmed on purpose -- the run is meant to outlive this process,
#      and deleting what we just launched would waste the whole run.
#   3. GCE's --max-run-duration with --instance-termination-action=DELETE.
#      That clock starts when the instance reaches RUNNING, so it does
#      NOT protect a resource that never boots; the reaper covers that.
#   4. reap_orphans.sh, which finds anything the others missed, by label
#      and across every zone.
#
# --boot-disk-auto-delete matters as much as the instance deletes: a
# deleted instance can still leave a billing disk behind.
#
# C3 bare metal is not a normal VM. It takes Hyperdisk only, requires an
# IDPF network interface (no gVNIC or VirtIO, since there is no
# hypervisor), requires TERMINATE maintenance behaviour, and does not
# support Shielded VM or vTPM. Those are platform requirements, not
# preferences: getting any wrong fails the create.
set -euo pipefail

PROJECT="${TESS_GCP_PROJECT:-}"
BUCKET="${TESS_GCP_BUCKET:-}"
ZONE="${TESS_GCP_ZONE:-us-central1-a}"
MACHINE_TYPE="c3-standard-192-metal"
IMAGE_FAMILY="ubuntu-2404-lts-amd64"
IMAGE_PROJECT="ubuntu-os-cloud"
BOOT_DISK_SIZE_GB=100
BOOT_DISK_TYPE="hyperdisk-balanced"
# 90m against a 40-50m estimate. The cap is a ceiling, not a cost: the
# instance self-deletes when it finishes, so headroom is only charged if
# it is actually used.
MAX_RUN_DURATION="90m"
HOURLY_USD="10.00"
REQUIRE_COUNTERS=1
ASSUME_YES=0
DRY_RUN=0

usage() {
  cat <<'EOF'
Usage: run_metal_bench.sh [options]

  --project=ID          GCP project             [$TESS_GCP_PROJECT]
  --bucket=gs://NAME    results bucket          [$TESS_GCP_BUCKET]
  --zone=ZONE           GCE zone                [$TESS_GCP_ZONE]
  --machine-type=TYPE   instance type           [c3-standard-192-metal]
  --max-run-duration=D  hard kill cap           [90m]
  --allow-no-counters   run even if the PMU is unavailable
  --yes / -y            skip the confirmation prompt
  --dry-run             print the plan and exit; creates nothing
  --help                this text

Prints an estimated cost and asks for confirmation unless --yes.

By default the instance ABORTS if hardware counters are unavailable,
because counters are the only reason this tier costs what it does.

After any campaign:
  tools/cloud/reap_orphans.sh --project=ID
EOF
}

for arg in "$@"; do
  case "$arg" in
    --project=*)          PROJECT="${arg#*=}" ;;
    --bucket=*)           BUCKET="${arg#*=}" ;;
    --zone=*)             ZONE="${arg#*=}" ;;
    --machine-type=*)     MACHINE_TYPE="${arg#*=}" ;;
    --max-run-duration=*) MAX_RUN_DURATION="${arg#*=}" ;;
    --allow-no-counters)  REQUIRE_COUNTERS=0 ;;
    --yes|-y)             ASSUME_YES=1 ;;
    --dry-run)            DRY_RUN=1 ;;
    --help|-h)            usage; exit 0 ;;
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

# Dirty means "the tarball differs from the commit", which includes
# STAGED and UNTRACKED files, not only unstaged edits. The package is
# built from `ls-files --cached --others`, so anything it picks up that
# the commit does not contain must show in the provenance -- otherwise a
# result claims a clean commit while measuring different source.
GIT_DIRTY=""
if ! git -C "$REPO_ROOT" diff --quiet \
   || ! git -C "$REPO_ROOT" diff --cached --quiet \
   || [[ -n "$(git -C "$REPO_ROOT" ls-files --others --exclude-standard)" ]]
then
  GIT_DIRTY="-dirty"
fi

# Lowercase: GCE instance names allow only lowercase letters, digits and
# hyphens, and a UTC timestamp carries an uppercase T and Z.
RUN_ID="$(date -u +%Y%m%dt%H%M%Sz)"
INSTANCE_NAME="tess-metal-$RUN_ID"
if ! [[ "$INSTANCE_NAME" =~ ^[a-z]([-a-z0-9]*[a-z0-9])?$ ]]; then
  echo "error: computed instance name '$INSTANCE_NAME' is not a valid" \
    "GCE name" >&2
  exit 1
fi
BUCKET_PREFIX="$BUCKET/campaigns/$RUN_ID"

cap_minutes() {
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
if (( REQUIRE_COUNTERS )); then
  COUNTER_NOTE="required (the instance aborts if absent)"
else
  COUNTER_NOTE="optional"
fi

cat <<EOF

Campaign plan
  project        $PROJECT
  zone           $ZONE
  machine        $MACHINE_TYPE
  instance       $INSTANCE_NAME
  boot disk      ${BOOT_DISK_SIZE_GB}GB $BOOT_DISK_TYPE
  commit         ${GIT_COMMIT:0:12}${GIT_DIRTY}
  results        $BUCKET_PREFIX
  counters       $COUNTER_NOTE
  hard cap       $MAX_RUN_DURATION  (counted from reaching RUNNING)
  worst-case     \$${WORST_USD} at \$${HOURLY_USD}/hr

EOF

if (( DRY_RUN )); then
  echo "dry run: no instance created, nothing billed"
  exit 0
fi

# ---- Preflight -------------------------------------------------------
# OAuth scopes are not IAM permissions. The instance self-deletes using
# its attached service account, so if that account cannot delete
# instances the primary cleanup mechanism is gone and only the duration
# cap remains. Better to learn that now than from a bill.
echo "preflight: checking delete permission..."
PERMS="$(gcloud projects test-iam-permissions "$PROJECT" \
  --permissions=compute.instances.delete \
  --format='value(permissions)' 2>/dev/null || true)"
if [[ "$PERMS" != *compute.instances.delete* ]]; then
  echo "warning: could not confirm compute.instances.delete for this" \
    "account. If the INSTANCE service account also lacks it, self-delete" \
    "fails and only the duration cap remains." >&2
fi

if (( ! ASSUME_YES )); then
  read -r -p "create this bare-metal instance? [y/N] " reply
  [[ "$reply" == [yY] ]] || { echo "aborted"; exit 0; }
fi

# ---- Cleanup trap ----------------------------------------------------
# Armed BEFORE the create, so an interrupted or partially failed create
# is covered, and disarmed after a successful create.
#
# Deliberately NOT trapping EXIT. A normal exit after handoff must not
# delete the instance we just launched, and trapping EXIT alongside the
# signals makes the handler reentrant, since it calls exit itself.
SRC_TARBALL=""
cleanup() {
  trap - INT TERM
  [[ -n "$SRC_TARBALL" && -f "$SRC_TARBALL" ]] && rm -f "$SRC_TARBALL"

  # Distinguish "not there" from "could not tell". Treating an API error
  # as absent is exactly how an instance gets left running.
  local describe_err describe_status
  set +e
  describe_err="$(gcloud compute instances describe "$INSTANCE_NAME" \
    --project="$PROJECT" --zone="$ZONE" \
    --format='value(name)' 2>&1 >/dev/null)"
  describe_status=$?
  set -e

  if (( describe_status == 0 )); then
    echo >&2
    echo "cleanup: deleting $INSTANCE_NAME..." >&2
    gcloud compute instances delete "$INSTANCE_NAME" \
      --project="$PROJECT" --zone="$ZONE" --delete-disks=all --quiet \
      || {
        echo "error: cleanup FAILED. Run:" >&2
        echo "  tools/cloud/reap_orphans.sh --project=$PROJECT" >&2
      }
  elif [[ "$describe_err" != *"not found"* && "$describe_err" != *404* ]]; then
    echo "warning: could not determine whether $INSTANCE_NAME exists:" >&2
    echo "  $describe_err" >&2
    echo "  Run tools/cloud/reap_orphans.sh --project=$PROJECT" >&2
  fi
  exit 130
}
trap cleanup INT TERM

# ---- Package the working tree ---------------------------------------
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
  --network-interface=nic-type=IDPF \
  --maintenance-policy=TERMINATE \
  --no-restart-on-failure \
  --max-run-duration="$MAX_RUN_DURATION" \
  --instance-termination-action=DELETE \
  --labels="tess-campaign=1,tess-run-id=$RUN_ID" \
  --scopes="https://www.googleapis.com/auth/devstorage.read_write,https://www.googleapis.com/auth/compute" \
  --metadata-from-file="startup-script=$STARTUP_FILE" \
  --metadata="^;;^tess-bucket=$BUCKET_PREFIX;;tess-source-url=$BUCKET_PREFIX/source.tar.gz;;tess-run-id=$RUN_ID;;tess-git-commit=${GIT_COMMIT}${GIT_DIRTY};;tess-require-counters=$REQUIRE_COUNTERS"

# Handed off: the instance owns its own lifecycle from here.
trap - INT TERM
rm -f "$SRC_TARBALL"
SRC_TARBALL=""

cat <<EOF

Instance created. It self-deletes when finished, and GCE deletes it at
$MAX_RUN_DURATION after it reaches RUNNING regardless.

Watch it:
  tools/cloud/watch_campaign.sh --project=$PROJECT --bucket=$BUCKET_PREFIX

Results:
  $BUCKET_PREFIX

Confirm nothing was left behind afterwards:
  tools/cloud/reap_orphans.sh --project=$PROJECT
EOF
