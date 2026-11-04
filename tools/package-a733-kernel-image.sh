#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

# Replace only /boot/openvela/a733/Image in partition 3 of a known-good A733
# SD image.  debugfs keeps this operation unprivileged and the source image is
# never modified.
#
# Usage: package-a733-kernel-image.sh BASE.img OUTPUT.img KERNEL.Image

base_image=${1:?base image required}
output_image=${2:?output image required}
kernel=${3:?kernel Image required}
camera_assets=${4:-}
camera_release=${5:-192}
[[ $# -le 5 && ( "$camera_release" == 192 || "$camera_release" == 193 || "$camera_release" == 194 || "$camera_release" == 195 || "$camera_release" == 196 || "$camera_release" == 197 || "$camera_release" == 198 || "$camera_release" == 199 ) ]]
camera_names=("camera-v$camera_release.cmd" "camera-v$camera_release.scr")
if [[ "$camera_release" -ge 194 ]]; then
  camera_names+=(imx214-1080p.a7cm imx214-reference.c SOURCE-LICENSE.txt)
fi
if [[ -n "$camera_assets" ]]; then
  for name in "${camera_names[@]}"; do test -f "$camera_assets/$name"; done
fi

sector_size=512
rootfs_first_lba=679936
rootfs_sectors=237568
work_dir=$(mktemp -d)
rootfs_image="$work_dir/openvela-rootfs.ext4"
embedded_kernel="$work_dir/embedded-Image"

cleanup()
{
  rm -f -- "$rootfs_image" "$embedded_kernel" \
    "$work_dir/embedded-camera-v$camera_release.cmd" "$work_dir/embedded-camera-v$camera_release.scr"
  for name in imx214-1080p.a7cm imx214-reference.c SOURCE-LICENSE.txt; do
    rm -f -- "$work_dir/embedded-$name"
  done
  rmdir "$work_dir" 2>/dev/null || true
}
trap cleanup EXIT

for tool in dd debugfs e2fsck sha256sum stat; do
  command -v "$tool" >/dev/null || {
    echo "Missing required tool: $tool" >&2
    exit 1
  }
done

test -f "$base_image"
test -f "$kernel"
test "$base_image" != "$output_image" || {
  echo "Output image must differ from source image" >&2
  exit 1
}
test ! -e "$output_image" || {
  echo "Refusing to overwrite existing output: $output_image" >&2
  exit 1
}

base_bytes=$(stat -c %s "$base_image")
minimum_bytes=$(((rootfs_first_lba + rootfs_sectors) * sector_size))
[ "$base_bytes" -ge "$minimum_bytes" ] || {
  echo "Base image is too small: $base_bytes" >&2
  exit 1
}

echo "Extracting openvela partition..."
dd if="$base_image" of="$rootfs_image" bs=1M \
  iflag=skip_bytes,count_bytes \
  skip="$((rootfs_first_lba * sector_size))" \
  count="$((rootfs_sectors * sector_size))" status=none
e2fsck -fn "$rootfs_image"

debugfs -w -R "rm /boot/openvela/a733/Image" \
  "$rootfs_image" >/dev/null 2>&1
debugfs -w -R "write $kernel /boot/openvela/a733/Image" \
  "$rootfs_image" >/dev/null 2>&1
debugfs -R "dump /boot/openvela/a733/Image $embedded_kernel" \
  "$rootfs_image" >/dev/null 2>&1
cmp -- "$kernel" "$embedded_kernel"

# Optional v192 RAM-only script assets. No boot environment/extlinux change.
# The caller additionally recomputes these against the exact final ELF/Image.
if [[ -n "$camera_assets" ]]; then
  for name in "${camera_names[@]}"; do
    debugfs -w -R "write $camera_assets/$name /boot/openvela/a733/$name" \
      "$rootfs_image" >/dev/null 2>&1
    debugfs -R "dump /boot/openvela/a733/$name $work_dir/embedded-$name" \
      "$rootfs_image" >/dev/null 2>&1
    cmp -- "$camera_assets/$name" "$work_dir/embedded-$name"
  done
fi
e2fsck -fn "$rootfs_image"

echo "Copying the known-good source image..."
cp --reflink=auto -- "$base_image" "$output_image"
dd if="$rootfs_image" of="$output_image" bs=1M \
  iflag=count_bytes oflag=seek_bytes \
  seek="$((rootfs_first_lba * sector_size))" \
  count="$((rootfs_sectors * sector_size))" \
  conv=notrunc status=none

echo "Kernel SHA256:"
sha256sum "$kernel"
echo "Final image SHA256:"
sha256sum "$output_image"
