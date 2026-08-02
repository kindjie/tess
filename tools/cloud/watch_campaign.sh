#!/usr/bin/env bash
# Watch a running campaign from outside the instance.
#
# Three independent signals, because they fail differently:
#
#   status.txt   the instance's own heartbeat, updated every 30s. Stale
#                means the instance is wedged or gone.
#   instance     whether GCE still has it, and how long it has run.
#   serial       the boot console, which shows failures that happen
#                before the startup script can upload anything at all.
#
# A stale heartbeat with a live instance is the case worth acting on:
# something hung, and it is billing until the duration cap.
set -euo pipefail

PROJECT="${TESS_GCP_PROJECT:-}"
BUCKET="${TESS_GCP_BUCKET:-}"
INTERVAL=30
ONCE=0
SERIAL=0

usage() {
  cat <<'EOF'
Usage: watch_campaign.sh [options]

  --project=ID        GCP project              [$TESS_GCP_PROJECT]
  --bucket=gs://PATH  campaign prefix, or the parent to auto-pick the
                      most recent run                [$TESS_GCP_BUCKET]
  --interval=SECS     poll interval                             [30]
  --once              print one snapshot and exit
  --serial            also show the last serial-console lines
  --help              this text

Read-only. Creates nothing and never deletes.
EOF
}

for arg in "$@"; do
  case "$arg" in
    --project=*)  PROJECT="${arg#*=}" ;;
    --bucket=*)   BUCKET="${arg#*=}" ;;
    --interval=*) INTERVAL="${arg#*=}" ;;
    --once)       ONCE=1 ;;
    --serial)     SERIAL=1 ;;
    --help|-h)    usage; exit 0 ;;
    *) echo "unknown argument: $arg" >&2; usage >&2; exit 2 ;;
  esac
done

[[ -n "$PROJECT" ]] || { echo "error: --project required" >&2; exit 2; }
[[ -n "$BUCKET" ]]  || { echo "error: --bucket required" >&2; exit 2; }
[[ "$INTERVAL" =~ ^[0-9]+$ ]] || {
  echo "error: --interval must be a whole number of seconds" >&2; exit 2; }

# If given a parent prefix, follow the newest campaign under it rather
# than making the operator paste a run id.
# An explicit campaign prefix must keep being treated as one even
# before status.txt exists, or the first minutes of a run get
# misclassified as a parent prefix and the suppressed `ls` failure ends
# the watch with no explanation.
resolve_prefix() {
  case "$BUCKET" in
    */campaigns/*) printf '%s' "${BUCKET%/}"; return ;;
  esac
  if gsutil -q stat "$BUCKET/status.txt" 2>/dev/null; then
    printf '%s' "${BUCKET%/}"
    return
  fi
  local newest
  newest="$(gsutil ls "$BUCKET/campaigns/" 2>/dev/null | sort | tail -1)" || true
  printf '%s' "${newest%/}"
}

snapshot() {
  local prefix; prefix="$(resolve_prefix)"
  echo "=============================================================="
  date -u +'%Y-%m-%dT%H:%M:%SZ'
  echo "prefix: ${prefix:-<none found>}"
  echo

  echo "-- instance --"
  local live
  local list_status
  set +e
  live="$(gcloud compute instances list --project="$PROJECT" \
    --filter='labels.tess-campaign=1' \
    --format='value(name,status,creationTimestamp)' 2>&1)"
  list_status=$?
  set -e
  if (( list_status != 0 )); then
    # Do not report "nothing running" when the answer is "could not
    # ask" -- that reads as a clean bill of health for a live machine.
    echo "ERROR: could not list instances; status unknown"
    echo "$live"
    live=""
  elif [[ -z "$live" ]]; then
    echo "no campaign instance running (finished, or not started)"
  else
    echo "$live"
  fi
  echo

  echo "-- heartbeat --"
  if [[ -n "$prefix" ]] \
     && gsutil -q cat "$prefix/status.txt" 2>/dev/null; then
    :
  else
    echo "no status.txt yet (instance still booting, or never started)"
  fi
  echo

  echo "-- last log lines --"
  if [[ -n "$prefix" ]]; then
    gsutil -q cat "$prefix/campaign.log" 2>/dev/null | tail -12 \
      || echo "no campaign.log yet"
  fi

  if (( SERIAL )) && [[ -n "$live" ]]; then
    local name zone
    name="$(awk '{print $1}' <<<"$live" | head -1)"
    zone="$(gcloud compute instances list --project="$PROJECT" \
      --filter="name=$name" --format='value(zone)' 2>/dev/null | head -1)"
    echo
    echo "-- serial console (last 15) --"
    gcloud compute instances get-serial-port-output "$name" \
      --project="$PROJECT" --zone="$zone" 2>/dev/null | tail -15 \
      || echo "serial output unavailable"
  fi
}

if (( ONCE )); then
  snapshot
  exit 0
fi

while true; do
  snapshot
  sleep "$INTERVAL"
done
