#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Read-only existing IMAGE validation; temporary files only, no debugfs -w.
set -euo pipefail
image=${1:?};assets=${2:?};release=${3:?}
[[ "$release" == 192 || "$release" == 193 || "$release" == 194 || "$release" == 195 || "$release" == 196 || "$release" == 197 || "$release" == 198 || "$release" == 199 ]]
[[ -f "$image" && ! -L "$image" && -d "$assets" ]]
work=$(mktemp -d)
names=("camera-v$release.cmd" "camera-v$release.scr")
if [[ "$release" -ge 194 ]];then names+=(imx214-1080p.a7cm imx214-reference.c SOURCE-LICENSE.txt);fi
cleanup() {
  rm -f -- "$work/root.ext4"
  for name in "${names[@]}";do rm -f -- "$work/$name";done
  rmdir -- "$work" 2>/dev/null || true
}
trap cleanup EXIT
dd if="$image" of="$work/root.ext4" bs=64K iflag=skip_bytes,count_bytes \
  skip=348127232 count=121634816 status=none
e2fsck -fn "$work/root.ext4"
for name in "${names[@]}";do
  [[ -f "$assets/$name" && ! -L "$assets/$name" ]]
  debugfs -R "dump /boot/openvela/a733/$name $work/$name" "$work/root.ext4" >/dev/null 2>&1
  cmp -- "$assets/$name" "$work/$name"
done
echo "PASS: all $release camera assets byte-equal; existing image opened READ-ONLY"
