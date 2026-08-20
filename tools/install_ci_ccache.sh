#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
lock_file=${1:-"$repo_root/ci/tools.lock.json"}
install_root=${2:-"${RUNNER_TEMP:?RUNNER_TEMP must be set}/tess-ccache"}
github_path=${3:-"${GITHUB_PATH:?GITHUB_PATH must be set}"}

case "$(uname -s):$(uname -m)" in
  Linux:x86_64) ;;
  *)
    echo "error: the pinned CI ccache supports Linux x86_64 only" >&2
    exit 1
    ;;
esac

version=$(jq -er '.ccache.version' "$lock_file")
url=$(jq -er '.ccache.linux_x86_64' "$lock_file")
expected=$(jq -er '.ccache.sha256' "$lock_file")
archive="$install_root/ccache.tar.xz"
bin_dir="$install_root/bin"

mkdir -p "$bin_dir"
curl --proto '=https' --proto-redir '=https' \
  --fail --location --retry 3 --retry-all-errors \
  --connect-timeout 20 --max-time 120 --retry-max-time 300 \
  --output "$archive" "$url"
echo "$expected  $archive" | sha256sum --check --strict
tar --extract --xz --file "$archive" --directory "$bin_dir" \
  --strip-components=1 "ccache-$version-linux-x86_64-musl-static/ccache"

actual=$("$bin_dir/ccache" --version | head -1)
if [ "$actual" != "ccache version $version" ]; then
  echo "error: expected ccache version $version, got: $actual" >&2
  exit 1
fi
echo "$bin_dir" >> "$github_path"
