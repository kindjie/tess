#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -eq 0 ]; then
  echo "usage: tools/ci_apt_install.sh PACKAGE..." >&2
  exit 2
fi

# These are retry and inactivity bounds, not a total wall-clock deadline.
apt_options=(
  -o Acquire::Retries=3
  -o Acquire::http::Timeout=30
  -o Acquire::https::Timeout=30
  -o Acquire::Languages=none
)
sudo apt-get "${apt_options[@]}" update
sudo apt-get "${apt_options[@]}" install --no-install-recommends -y "$@"
