#!/usr/bin/env bash
set -euo pipefail

# Create a separate A733 SD image by replacing only the openvela kernel in a
# known-good 512 MiB image.  The source image is never modified.
#
# Usage:
#   package-a733-local-llm-image.sh BASE.img OUTPUT.img KERNEL.Image

base_image=${1:?base image required}
output_image=${2:?output image required}
kernel=${3:?kernel Image required}
rootfs_offset=${ROOTFS_OFFSET:-348127232}
rootfs_size=${ROOTFS_SIZE:-121634816}
mount_dir=$(mktemp -d)
loop_dev=""

cleanup()
{
  mountpoint -q "$mount_dir" && umount "$mount_dir" || true
  [ -z "$loop_dev" ] || losetup -d "$loop_dev" 2>/dev/null || true
  rmdir "$mount_dir" 2>/dev/null || true
}
trap cleanup EXIT

test -f "$base_image"
test -f "$kernel"
test "$(stat -c %s "$base_image")" -ge "$rootfs_offset"
test "$(stat -c %s "$kernel")" -gt 0

if [[ "$base_image" != "$output_image" ]]; then
  cp --reflink=auto -- "$base_image" "$output_image"
fi

loop_dev=$(losetup --find --show --offset "$rootfs_offset" \
  --sizelimit "$rootfs_size" "$output_image")
mount "$loop_dev" "$mount_dir"
install -m 0644 "$kernel" "$mount_dir/boot/openvela/a733/Image"
sync

echo "Embedded kernel:"
sha256sum "$kernel" "$mount_dir/boot/openvela/a733/Image"
umount "$mount_dir"
e2fsck -fn "$loop_dev"
losetup -d "$loop_dev"
loop_dev=""

echo "Image:"
sha256sum "$output_image"
