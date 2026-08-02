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
# support Shielded VM or vTPM -- and gcloud enables vTPM and integrity
# monitoring BY DEFAULT for Shielded-capable images such as Ubuntu
# 24.04, so they must be disabled EXPLICITLY rather than merely left
# unset. Platform requirements, not preferences: any one wrong fails
# the create.
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
# The bucket path is embedded in a single-quoted shell command on the
# instance, so an apostrophe would break or alter that command. Restrict
# to the characters GCS paths actually use rather than quoting around a
# hostile value.
[[ "$BUCKET" =~ ^gs://[A-Za-z0-9._/-]+$ ]] || {
  echo "error: --bucket may contain only letters, digits, dot, dash," \
    "underscore and slash" >&2
  exit 2; }
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
# Tier-dependent label and price. Quoting a metal hourly rate for a
# validation instance would overstate the cost by roughly sixty times
# and train the operator to ignore the number.
case "$MACHINE_TYPE" in
  *-metal)
    TIER_LABEL="bare metal"
    ;;
  *)
    TIER_LABEL="virtualized -- validation tier"
    HOURLY_USD="${HOURLY_USD_OVERRIDE:-0.20}"
    ;;
esac

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
  machine        $MACHINE_TYPE ($TIER_LABEL)
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
# The identity that matters is the instance's attached service account,
# NOT the operator: self-delete and every upload run as that account.
# test-iam-permissions answers for the caller, so checking only that
# produces confidence about the wrong principal.
echo "preflight: checking permissions..."
PERMS="$(gcloud projects test-iam-permissions "$PROJECT" \
  --permissions=compute.instances.delete \
  --format='value(permissions)' 2>/dev/null || true)"
if [[ "$PERMS" != *compute.instances.delete* ]]; then
  echo "warning: the OPERATOR cannot confirm compute.instances.delete" >&2
fi

# Asked for rather than constructed. Building the address from the
# project number would hardcode Google's service-account domain, which
# this repository's public-safety hook flags as an email address, and
# would also be wrong if the project uses a non-default account.
INSTANCE_SA="$(gcloud compute project-info describe --project="$PROJECT" \
  --format='value(defaultServiceAccount)' 2>/dev/null || true)"
if [[ -n "$INSTANCE_SA" ]]; then
  SA_ROLES="$(gcloud projects get-iam-policy "$PROJECT" \
    --flatten='bindings[].members' \
    --filter="bindings.members:${INSTANCE_SA}" \
    --format='value(bindings.role)' 2>/dev/null || true)"
  if [[ -z "$SA_ROLES" ]]; then
    echo "WARNING: the instance service account has no visible project" >&2
    echo "  role ($INSTANCE_SA). Newer organizations disable the" >&2
    echo "  automatic Editor grant; without it the source fetch and every" >&2
    echo "  upload fail, self-delete fails, and the duration cap becomes" >&2
    echo "  the only cleanup -- paying the cap for zero results." >&2
  else
    echo "  instance service account has role(s):" \
      "$(echo "$SA_ROLES" | tr '\n' ' ')"
    echo "  NOTE: role visibility only -- this does not prove the" \
      "account can delete instances or write the bucket, and" \
      "bucket-level or inherited grants are not visible here." >&2
  fi
else
  echo "warning: could not resolve the instance service account, so it" \
    "was NOT checked" >&2
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
# Shared by the interrupt handler and the failed-create path.
# gcloud has no global deadline, so a stalled call would make the
# "90 second" bound below meaningless. `timeout` is used when present;
# when it is not, the bound is best-effort and says so.
# -k sends KILL after TERM, so a child that ignores TERM is still
# bounded. Without a timeout binary at all, the cleanup deliberately
# does NOT ignore signals -- an unbounded call the operator cannot
# interrupt is worse than an interruptible one.
TIMEOUT_BIN=""
if command -v timeout >/dev/null 2>&1; then
  TIMEOUT_BIN="timeout"
elif command -v gtimeout >/dev/null 2>&1; then
  TIMEOUT_BIN="gtimeout"
fi
# Two bounds. A describe is a quick read, but deleting a bare-metal
# instance can keep the client waiting well past a minute, and a 60s cap
# there would report "DELETE FAILED -- may still be billing" while the
# server-side delete was in fact proceeding. That fails in the safe
# direction (it sends the operator to the reaper, which then finds
# nothing) but it is a false alarm about money, which is the last thing
# this tooling should cry wolf about.
bounded_gcloud() {
  local seconds="${BOUNDED_GCLOUD_SECONDS:-60}"
  if [[ -n "$TIMEOUT_BIN" ]]; then
    "$TIMEOUT_BIN" -k 10 "$seconds" gcloud "$@"
  else
    gcloud "$@"
  fi
}

remove_instance_if_present() {
  # Distinguish "not there" from "could not tell". Treating an API error
  # as absent is exactly how an instance gets left running.
  local describe_err describe_status
  set +e
  describe_err="$(bounded_gcloud compute instances describe "$INSTANCE_NAME" \
    --project="$PROJECT" --zone="$ZONE" \
    --format='value(name)' 2>&1 >/dev/null)"
  describe_status=$?
  set -e

  if (( describe_status == 0 )); then
    echo "deleting $INSTANCE_NAME..." >&2
    set +e
    BOUNDED_GCLOUD_SECONDS=300 bounded_gcloud compute instances delete \
      "$INSTANCE_NAME" \
      --project="$PROJECT" --zone="$ZONE" --delete-disks=all --quiet
    local delete_status=$?
    set -e
    if (( delete_status != 0 )); then
      echo "error: DELETE FAILED -- the instance may still be billing." >&2
      echo "  Run: tools/cloud/reap_orphans.sh --project=$PROJECT" >&2
    fi
  else
    # A 404 straight after an interrupted create is NOT proof of
    # absence: killing the client does not cancel the server-side
    # operation, so the instance may still be materializing. Re-check
    # before believing it, and never exit silently either way.
    # Only worth polling if a create was actually issued. Interrupting
    # during the tarball upload otherwise costs 90 seconds waiting for
    # an instance that was never requested.
    if (( ! CREATE_ATTEMPTED )); then
      return
    fi
    # About 90s of polling; each gcloud call is separately bounded by
    # `timeout` when available, so the total is bounded in practice
    # rather than guaranteed.
    # Wall clock, not accumulated sleep: each bounded gcloud can take up
    # to 60s, so counting only the sleeps understated the real bound by
    # roughly an order of magnitude.
    local deadline=$(( SECONDS + 90 )) recheck
    while (( SECONDS < deadline )); do
      sleep 10
      set +e
      bounded_gcloud compute instances describe "$INSTANCE_NAME" \
        --project="$PROJECT" --zone="$ZONE" \
        --format='value(name)' >/dev/null 2>&1
      recheck=$?
      set -e
      if (( recheck == 0 )); then
        echo "instance appeared during recheck; deleting..." >&2
        BOUNDED_GCLOUD_SECONDS=300 bounded_gcloud compute instances delete \
          "$INSTANCE_NAME" \
          --project="$PROJECT" --zone="$ZONE" --delete-disks=all --quiet \
          || echo "error: DELETE FAILED -- run reap_orphans.sh" >&2
        return
      fi
    done
    echo "no instance named $INSTANCE_NAME after the recheck window" >&2
    echo "  (describe said: ${describe_err:-nothing})" >&2
    echo "  Confirm with: tools/cloud/reap_orphans.sh --project=$PROJECT" >&2
  fi
}

CREATE_ATTEMPTED=0
CLEANUP_RUNNING=0
cleanup() {
  # Ignore further signals ONLY when every call inside is bounded.
  # Without a timeout binary a wedged gcloud would otherwise be
  # uninterruptible forever, which is worse than the stranding this
  # protects against -- and the reaper still covers that case.
  if [[ -n "$TIMEOUT_BIN" ]]; then
    trap '' INT TERM
  else
    trap - INT TERM
    echo "note: no timeout binary; cleanup stays interruptible" >&2
  fi
  if (( CLEANUP_RUNNING )); then
    return
  fi
  CLEANUP_RUNNING=1
  echo "cleaning up; further interrupts ignored..." >&2
  [[ -n "$SRC_TARBALL" && -f "$SRC_TARBALL" ]] && rm -f "$SRC_TARBALL"
  echo >&2
  remove_instance_if_present
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

# The create is checked explicitly. With only INT/TERM trapped, a
# nonzero exit here would otherwise end the script through errexit
# without running any cleanup -- and the API can accept the request and
# still return an error to the client, leaving an instance that never
# reaches RUNNING. Such an instance runs neither its own startup trap
# nor the duration timer, so nothing but the reaper would ever find it.
create_status=0
CREATE_ATTEMPTED=1
# The metal-only flags are applied only for a metal machine type. IDPF
# networking and the Shielded opt-outs are REQUIREMENTS on bare metal
# and REJECTED (or pointless) on a normal VM, so hardcoding them would
# make the cheap validation tier impossible to run through this same
# code -- and validating a different code path proves nothing.
CREATE_EXTRA=()
case "$MACHINE_TYPE" in
  *-metal)
    CREATE_EXTRA+=(
      --network-interface=nic-type=IDPF
      --maintenance-policy=TERMINATE
      --no-shielded-secure-boot
      --no-shielded-vtpm
      --no-shielded-integrity-monitoring
    )
    ;;
esac

gcloud compute instances create "$INSTANCE_NAME" \
  --project="$PROJECT" \
  --zone="$ZONE" \
  --machine-type="$MACHINE_TYPE" \
  --image-family="$IMAGE_FAMILY" \
  --image-project="$IMAGE_PROJECT" \
  --boot-disk-size="${BOOT_DISK_SIZE_GB}GB" \
  --boot-disk-type="$BOOT_DISK_TYPE" \
  --boot-disk-auto-delete \
  "${CREATE_EXTRA[@]}" \
  --no-restart-on-failure \
  --max-run-duration="$MAX_RUN_DURATION" \
  --instance-termination-action=DELETE \
  --labels="tess-campaign=1,tess-run-id=$RUN_ID" \
  --scopes="https://www.googleapis.com/auth/devstorage.read_write,https://www.googleapis.com/auth/compute" \
  --metadata-from-file="startup-script=$STARTUP_FILE" \
  --metadata="^;;^tess-bucket=$BUCKET_PREFIX;;tess-source-url=$BUCKET_PREFIX/source.tar.gz;;tess-run-id=$RUN_ID;;tess-git-commit=${GIT_COMMIT}${GIT_DIRTY};;tess-require-counters=$REQUIRE_COUNTERS" \
  || create_status=$?

if (( create_status != 0 )); then
  echo >&2
  echo "error: instance create failed (status $create_status)." >&2
  echo "Checking for a partially created instance..." >&2
  remove_instance_if_present
  exit 1
fi

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
