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
FOUND=()
while IFS= read -r line; do
  [[ -n "$line" ]] && FOUND+=("$line")
done < <(
  gcloud compute instances list \
    --project="$PROJECT" \
    --filter="labels.${LABEL%%=*}=${LABEL#*=}" \
    --format='value(name,zone,creationTimestamp,status)' 2>/dev/null || true
)

if (( ${#FOUND[@]} == 0 )); then
  echo "no campaign instances found in $PROJECT"
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
  created_epoch=$(date -u -d "$created" +%s 2>/dev/null \
    || date -u -j -f "%Y-%m-%dT%H:%M:%S" "${created%%.*}" +%s 2>/dev/null \
    || echo 0)
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
