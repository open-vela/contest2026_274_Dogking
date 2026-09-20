#!/usr/bin/env bash
set -euo pipefail

# Build a large, directly flashable Cubie A7Z SD image without root access.
# The known-good partitions 1-4 are preserved byte-for-byte, except that the
# openvela Image inside partition 3 is replaced.  Partition 5 is a new FAT32
# volume mounted by openvela at /data/models.
#
# Usage:
#   package-a733-local-llm-large-image.sh BASE.img OUTPUT.img KERNEL.Image [SIZE_GIB]

base_image=${1:?base image required}
output_image=${2:?output image required}
kernel=${3:?kernel Image required}
size_gib=${4:-3}

sector_size=512
rootfs_first_lba=679936
rootfs_sectors=237568
models_first_lba=1048576
minimum_models_sectors=2097152

work_dir=$(mktemp -d)
rootfs_image="$work_dir/openvela-rootfs.ext4"
models_image="$work_dir/openvela-models.fat"

cleanup()
{
  rm -f -- "$rootfs_image" "$models_image"
  rmdir "$work_dir" 2>/dev/null || true
}
trap cleanup EXIT

for tool in dd debugfs e2fsck truncate stat sha256sum; do
  command -v "$tool" >/dev/null || {
    echo "Missing required tool: $tool" >&2
    exit 1
  }
done

sgdisk=${SGDISK:-/usr/sbin/sgdisk}
mkfs_fat=${MKFS_FAT:-/usr/sbin/mkfs.fat}
fsck_fat=${FSCK_FAT:-/usr/sbin/fsck.fat}

test -x "$sgdisk" || { echo "Missing $sgdisk" >&2; exit 1; }
test -x "$mkfs_fat" || { echo "Missing $mkfs_fat" >&2; exit 1; }
test -x "$fsck_fat" || { echo "Missing $fsck_fat" >&2; exit 1; }
test -f "$base_image"
test -f "$kernel"
test "$base_image" != "$output_image" || {
  echo "Output image must differ from the source image" >&2
  exit 1
}
[[ "$size_gib" =~ ^[0-9]+$ ]] && [ "$size_gib" -ge 2 ] || {
  echo "SIZE_GIB must be an integer of at least 2" >&2
  exit 1
}

expected_base_bytes=$((512 * 1024 * 1024))
actual_base_bytes=$(stat -c %s "$base_image")
[ "$actual_base_bytes" -eq "$expected_base_bytes" ] || {
  echo "Unexpected base image size: $actual_base_bytes (expected $expected_base_bytes)" >&2
  exit 1
}

echo "Extracting openvela root filesystem..."
dd if="$base_image" of="$rootfs_image" bs="$sector_size" \
  skip="$rootfs_first_lba" count="$rootfs_sectors" status=none

debugfs -w -R "rm /boot/openvela/a733/Image" "$rootfs_image" >/dev/null 2>&1
debugfs -w -R "write $kernel /boot/openvela/a733/Image" \
  "$rootfs_image" >/dev/null 2>&1
e2fsck -fn "$rootfs_image"

echo "Copying the known-good base image..."
cp --reflink=auto -- "$base_image" "$output_image"
dd if="$rootfs_image" of="$output_image" bs="$sector_size" \
  seek="$rootfs_first_lba" count="$rootfs_sectors" \
  conv=notrunc status=none

truncate -s "${size_gib}G" "$output_image"
"$sgdisk" -e "$output_image" >/dev/null
"$sgdisk" -n "5:${models_first_lba}:0" -t 5:0700 \
  -c 5:models "$output_image" >/dev/null

total_sectors=$(( $(stat -c %s "$output_image") / sector_size ))
last_usable_lba=$((total_sectors - 34))
models_sectors=$((last_usable_lba - models_first_lba + 1))
[ "$models_sectors" -ge "$minimum_models_sectors" ] || {
  echo "Models partition is too small: $models_sectors sectors" >&2
  exit 1
}

truncate -s $((models_sectors * sector_size)) "$models_image"
"$mkfs_fat" -F 32 -S "$sector_size" -n VELA_MODELS "$models_image"
"$fsck_fat" -vn "$models_image"
dd if="$models_image" of="$output_image" bs="$sector_size" \
  seek="$models_first_lba" count="$models_sectors" \
  conv=notrunc,sparse status=none

echo "Validating GPT..."
"$sgdisk" -v "$output_image"

echo "Kernel SHA256:"
sha256sum "$kernel"
echo "Final image SHA256:"
sha256sum "$output_image"
echo "Models volume: /dev/a7z-models -> /data/models"
echo "Models capacity: $((models_sectors * sector_size)) bytes"
