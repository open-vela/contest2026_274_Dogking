#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

image=${1:?image required}
expected_kernel_hash=${2:-}
sector_size=512
rootfs_first_lba=679936
rootfs_sectors=237568
work_dir=$(mktemp -d)
rootfs_image="$work_dir/openvela-rootfs.ext4"
embedded_kernel="$work_dir/Image"

cleanup()
{
  rm -f -- "$rootfs_image" "$embedded_kernel"
  rmdir "$work_dir" 2>/dev/null || true
}
trap cleanup EXIT

test -f "$image"
dd if="$image" of="$rootfs_image" bs=1M \
  iflag=skip_bytes,count_bytes \
  skip="$((rootfs_first_lba * sector_size))" \
  count="$((rootfs_sectors * sector_size))" status=none
e2fsck -fn "$rootfs_image"
debugfs -R "dump /boot/openvela/a733/Image $embedded_kernel" \
  "$rootfs_image" >/dev/null 2>&1

actual_kernel_hash=$(sha256sum "$embedded_kernel" | cut -d ' ' -f1)
echo "Embedded kernel bytes: $(stat -c %s "$embedded_kernel")"
echo "Embedded kernel SHA256: $actual_kernel_hash"
if [ -n "$expected_kernel_hash" ]; then
  test "$actual_kernel_hash" = "$expected_kernel_hash"
fi

/usr/sbin/sgdisk -v "$image"
echo "Image bytes: $(stat -c %s "$image")"
sha256sum "$image"
