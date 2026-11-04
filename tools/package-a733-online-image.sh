#!/usr/bin/env bash
set -euo pipefail
script_dir=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$script_dir/.." && pwd)
base=${1:?base image required}
output=${2:?new output image required}
kernel=${3:?kernel required}
ca=${4:-/etc/ssl/certs/ca-certificates.crt}
test -s "$ca"
for tool in mdir mmd mcopy cmp; do command -v "$tool" >/dev/null; done
test ! -e "$output"
bash "$script_dir/package-a733-kernel-image.sh" "$base" "$output" "$kernel"
# GPT partition 4 starts at LBA 917504 on this board's established SD layout.
fat="$output@@469762048"
mdir -i "$fat" ::/ >/dev/null
for directory in ::/ai_agent ::/ai_agent/config; do
  if ! mdir -i "$fat" "$directory" >/dev/null 2>&1; then
    mmd -i "$fat" "$directory"
  fi
done
mcopy -o -i "$fat" "$ca" ::/ai_agent/ca.pem
mcopy -o -i "$fat" "$repo/assets/aipet-online/SOUL.md" ::/ai_agent/config/SOUL.md
# No user credential is copied from the development tree into a firmware image.
tmp=$(mktemp -d)
trap 'rm -f "$tmp/ca.pem"; rmdir "$tmp"' EXIT
mcopy -i "$fat" ::/ai_agent/ca.pem "$tmp/ca.pem"
cmp "$ca" "$tmp/ca.pem"
bash "$script_dir/verify-a733-image.sh" "$output"
