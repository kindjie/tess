#!/usr/bin/env bash
# Find and delete leftover campaign instances, independent of any driver.
#
# The driver has three kill switches (GCE's --max-run-duration, the
# instance self-deleting, and the driver's EXIT trap). All three share a
# blind spot: they only protect a run whose driver is still alive or
# whose instance reached its cap. A SIGKILLed driver, a laptop that
# slept, or a crash between `instances create` returning and the trap
# taking effect leaves an instance that nothing is watching.
#
# On a c3-standard-4 that blind spot costs pennies. On bare metal at
# roughly $10/hour it is about $240 for one forgotten overnight, so this
# repository treats "find orphans" as a first-class operation rather
# than something you remember to check.
#
# Every instance the driver creates carries the labels this reads, so an
# orphan is findable without knowing its name or when it was started.
set -euo pipefail

PROJECT="${TESS_GCP_PROJECT:-}"
LABEL="tess-campaign=1"
MAX_AGE_MINUTES=0
ASSUME_YES=0
DRY_RUN=0

# An unattached disk keeps billing and is invisible to an instance-only
# listing. --boot-disk-auto-delete covers the normal cascade, but a
# partial create or a failed cascade can strand one.
# Returns nonzero if a stranded disk could not be listed or deleted.
# The first version of this warned and returned success -- reproducing,
# in the new code, exactly the fail-open defect that had just been fixed
# for the instance listing.
reap_disks() {
  local out status err err_text
  err="$(mktemp -t reap-disk-err-XXXXXX)"
  set +e
  out="$(gcloud compute disks list --project="$PROJECT" \
    --filter='-users:* AND name~^tess-metal-' \
    --format='value(name,zone,sizeGb)' 2>"$err")"
  status=$?
  set -e
  err_text="$(cat "$err" 2>/dev/null || true)"
  rm -f "$err"
  # Same reasoning as the instance listing: gcloud can return 0 while
  # only warning that some requests failed, which means the answer is
  # incomplete rather than empty.
  if (( status != 0 )) \
     || grep -qiE "did not succeed|was not found|PERMISSION_DENIED" \
          <<<"$err_text"; then
    echo "error: could not reliably list disks in $PROJECT" >&2
    echo "$err_text" >&2
    echo "A stranded boot disk would keep billing; check manually." >&2
    return 1
  fi
  [[ -z "$out" ]] && return 0
  echo
  echo "unattached campaign disks (these bill):"
  echo "$out"
  if (( DRY_RUN )); then
    echo "dry run: leaving them"
    return 0
  fi
  if (( ! ASSUME_YES )); then
    read -r -p "delete these disks? [y/N] " reply
    [[ "$reply" == [yY] ]] || return 0
  fi
  local disk_failures=0
  while IFS= read -r row; do
    [[ -n "$row" ]] || continue
    local dname dzone
    dname=$(awk '{print $1}' <<<"$row")
    dzone=$(awk '{print $2}' <<<"$row")
    echo "deleting disk $dname in $dzone..."
    if ! gcloud compute disks delete "$dname" --project="$PROJECT" \
         --zone="$dzone" --quiet; then
      echo "error: FAILED to delete disk $dname in $dzone" >&2
      disk_failures=$(( disk_failures + 1 ))
    fi
  done <<< "$out"
  if (( disk_failures > 0 )); then
    echo "error: $disk_failures disk(s) still present and billing" >&2
    return 1
  fi
  return 0
}

usage() {
  cat <<'EOF'
Usage: reap_orphans.sh [options]

  --project=ID        GCP project            [$TESS_GCP_PROJECT]
  --older-than=MINS   only reap instances older than MINS  [0 = all]
  --yes / -y          delete without confirmation
  --dry-run           list what would be deleted, delete nothing
  --help              this text

Exit status is 0 when nothing was found, so this is safe to run from a
cron or a post-campaign check. It is nonzero only if a delete FAILED --
an orphan that could not be removed is the one case worth waking up for.
EOF
}

for arg in "$@"; do
  case "$arg" in
    --project=*)     PROJECT="${arg#*=}" ;;
    --older-than=*)  MAX_AGE_MINUTES="${arg#*=}" ;;
    --yes|-y)        ASSUME_YES=1 ;;
    --dry-run)       DRY_RUN=1 ;;
    --help|-h)       usage; exit 0 ;;
    *) echo "unknown argument: $arg" >&2; usage >&2; exit 2 ;;
  esac
done

if [[ -z "$PROJECT" ]]; then
  echo "error: --project or TESS_GCP_PROJECT is required" >&2
  exit 2
fi
if ! [[ "$MAX_AGE_MINUTES" =~ ^[0-9]+$ ]]; then
  echo "error: --older-than must be a whole number of minutes" >&2
  exit 2
fi

# Every zone, not a configured one: an orphan in a zone the current
# config does not name is exactly the orphan nobody finds by hand.
# while-read rather than mapfile: mapfile is bash 4+, and macOS ships
# bash 3.2, so the reaper would have exited 127 on the machine an
# operator is most likely to run it from.
#
# The list command's failure is NOT swallowed. Discarding stderr and
# forcing success turns an auth failure, a wrong project, or an API
# outage into "no campaign instances found" and exit 0 -- a clean bill
# of health from a tool that could not see anything at all. In the one
# place whose whole job is catching an expensive mistake, failing open
# is the worst possible default.
# stdout and stderr kept SEPARATE. Folding stderr into the row list
# turns a gcloud warning into a fake instance row -- observed: a
# "WARNING: The following..." line parsed as an instance named WARNING.
list_output=""
list_err="$(mktemp -t reap-err-XXXXXX)"
set +e
list_output="$(gcloud compute instances list \
  --project="$PROJECT" \
  --filter="labels.${LABEL%%=*}=${LABEL#*=}" \
  --format='value(name,zone,creationTimestamp,status)' 2>"$list_err")"
list_status=$?
set -e
# Exit status alone is NOT sufficient. `gcloud compute instances list`
# returns 0 for a nonexistent project, an auth failure, or a partial
# zone failure, reporting only "Some requests did not succeed" on
# stderr. A partial failure is the dangerous one: the list is
# incomplete, so an orphan can be hidden behind a warning while this
# prints a clean bill of health.
list_err_text="$(cat "$list_err" 2>/dev/null || true)"
rm -f "$list_err"
if (( list_status != 0 )) \
   || grep -qiE "did not succeed|was not found|PERMISSION_DENIED|\
Reauthentication|invalid.*credential" <<<"$list_err_text"; then
  echo "error: could not reliably list instances in $PROJECT" >&2
  echo "$list_err_text" >&2
  echo "Cannot confirm whether anything is running. Check manually" \
    "before assuming nothing is billing." >&2
  exit 1
fi
# This one is benign: it only means no resource carries the label yet.
if [[ -n "$list_err_text" ]] \
   && ! grep -qi "filter keys were not present" <<<"$list_err_text"; then
  echo "note: gcloud wrote diagnostics while listing:" >&2
  echo "$list_err_text" >&2
fi

FOUND=()
while IFS= read -r line; do
  [[ -n "$line" ]] && FOUND+=("$line")
done <<< "$list_output"

if (( ${#FOUND[@]} == 0 )); then
  echo "no campaign instances found in $PROJECT"
  reap_disks || exit 1
  exit 0
fi

now_epoch=$(date -u +%s)
declare -a REAP=()
printf '%-38s %-18s %-22s %s\n' INSTANCE ZONE CREATED AGE
for row in "${FOUND[@]}"; do
  [[ -z "$row" ]] && continue
  name=$(awk '{print $1}' <<<"$row")
  zone=$(awk '{print $2}' <<<"$row")
  created=$(awk '{print $3}' <<<"$row")

  # GNU and BSD date disagree on parsing; try both rather than assuming
  # the operator's machine.
  # GNU date parses the offset directly. BSD date does not accept the
  # fractional seconds, and dropping the OFFSET as well as the fraction
  # would mis-age an instance by hours -- enough to reap one early. Strip
  # only the fraction, keep the offset, and tell BSD date about it.
  created_epoch=$(date -u -d "$created" +%s 2>/dev/null || echo 0)
  if (( created_epoch == 0 )); then
    normalized="$(printf '%s' "$created" | sed -E 's/\.[0-9]+//')"
    normalized="${normalized/Z/+0000}"
    normalized="$(printf '%s' "$normalized" | sed -E 's/([+-][0-9]{2}):([0-9]{2})$/\1\2/')"
    created_epoch=$(date -u -j -f "%Y-%m-%dT%H:%M:%S%z" \
      "$normalized" +%s 2>/dev/null || echo 0)
  fi
  if (( created_epoch == 0 )); then
    # Unparseable timestamp must not silently exclude an instance from
    # reaping -- err toward listing it.
    age_minutes=999999
  else
    age_minutes=$(( (now_epoch - created_epoch) / 60 ))
  fi

  printf '%-38s %-18s %-22s %sm\n' "$name" "$zone" "$created" "$age_minutes"
  if (( age_minutes >= MAX_AGE_MINUTES )); then
    REAP+=("$name|$zone")
  fi
done

if (( ${#REAP[@]} == 0 )); then
  echo
  echo "nothing older than ${MAX_AGE_MINUTES}m; nothing to reap"
  exit 0
fi

echo
echo "${#REAP[@]} instance(s) selected for deletion"
if (( DRY_RUN )); then
  echo "dry run: deleting nothing"
  exit 0
fi

if (( ! ASSUME_YES )); then
  read -r -p "delete these instances? [y/N] " reply
  [[ "$reply" == [yY] ]] || { echo "aborted"; exit 0; }
fi

failures=0
for entry in "${REAP[@]}"; do
  name="${entry%%|*}"
  zone="${entry##*|}"
  echo "deleting $name in $zone..."
  if ! gcloud compute instances delete "$name" \
       --project="$PROJECT" --zone="$zone" --quiet; then
    echo "error: FAILED to delete $name in $zone" >&2
    failures=$(( failures + 1 ))
  fi
done

if (( failures > 0 )); then
  echo "error: $failures instance(s) still running and billing" >&2
  exit 1
fi
echo "all selected instances deleted"
reap_disks || exit 1
