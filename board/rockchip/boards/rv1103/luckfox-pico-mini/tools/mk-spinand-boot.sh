#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
openvela_root="$(realpath "$script_dir/../../../../../..")"
sdk_root="${RV1103_SDK:-/home/devcontainers/rv1103-openvela-sdk/luckfox-pico-main}"
build_dir="${RV1103_OPENVELA_BUILD:-$openvela_root/cmake_out/luckfox-pico-mini_nsh_build}"
output_dir="${RV1103_OUTPUT_DIR:-$build_dir/rv1103-spinand}"

nuttx_bin="$build_dir/nuttx.bin"
dtb="$sdk_root/sysdrv/source/objs_kernel/arch/arm/boot/dts/rv1103g-luckfox-pico-mini.dtb"
resource="$sdk_root/sysdrv/source/objs_kernel/resource.img"
mkimage="$sdk_root/sysdrv/tools/pc/uboot_tools/mkimage"
vendor_image_dir="$sdk_root/output/image"
output="$output_dir/boot.img"
partition_size=$((4 * 1024 * 1024))

for input in "$nuttx_bin" "$dtb" "$resource" "$mkimage"; do
  if [[ ! -f "$input" ]]; then
    echo "Missing required input: $input" >&2
    exit 1
  fi
done

work_dir="$(mktemp -d)"
trap 'rm -rf -- "$work_dir"' EXIT

cp "$nuttx_bin" "$work_dir/openvela.bin"
cp "$dtb" "$work_dir/board.dtb"
cp "$resource" "$work_dir/resource.img"
cp "$script_dir/openvela-spinand.its" "$work_dir/openvela-spinand.its"

mkdir -p "$output_dir"
(
  cd "$work_dir"
  "$mkimage" -f openvela-spinand.its -E -p 0x800 "$output"
)

cp "$vendor_image_dir/download.bin" "$output_dir/download.bin"
cp "$vendor_image_dir/env.img" "$output_dir/env.img"
cp "$vendor_image_dir/idblock.img" "$output_dir/idblock.img"
cp "$vendor_image_dir/uboot.img" "$output_dir/uboot.img"
cp "$vendor_image_dir/boot.img" "$output_dir/boot-linux-backup.img"

image_size="$(stat -c %s "$output")"
if ((image_size > partition_size)); then
  echo "boot.img is larger than the 4 MiB SPI NAND boot partition" >&2
  exit 1
fi

"$mkimage" -l "$output"
sha256sum "$output"
sha256sum "$output_dir/download.bin" "$output_dir/env.img"
sha256sum "$output_dir/idblock.img" "$output_dir/uboot.img"
sha256sum "$output_dir/boot-linux-backup.img"
(
  cd "$output_dir"
  sha256sum boot.img download.bin env.img idblock.img uboot.img \
    boot-linux-backup.img >SHA256SUMS
)
printf 'boot_partition_bytes=%d\n' "$partition_size"
printf 'boot_image_bytes=%d\n' "$image_size"
printf 'boot_image=%s\n' "$output"
