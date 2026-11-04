#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
team=$(cd "$(dirname "$0")/.." && pwd)
official=$(cd "${OPENVELA_WORKSPACE:-$team/../../quickly-openvela}" && pwd)
build="$official/cmake_out/cubie-a7z_nsh_v147_ics43434_counter"
base="$team/../openvela-a733-cubie-a7z-sd-i2s-v146-ics43434-candidate.img"
output=${1:-$team/../openvela-a733-cubie-a7z-sd-i2s-v147-ics43434-counter-candidate.img}
evidence="$team/docs/evidence/i2s-v147"
export PATH="$official/prebuilts/gcc/linux-x86_64/aarch64-none-elf/bin:/usr/bin:/bin:$PATH"
test -s "$build/nuttx.bin"
test -s "$build/nuttx"
grep -q '^Official source restoration: PASS' "$evidence/official-restore.log"
grep -q 'a733-layout: page alignment/segments/stack/Image footprint passed' "$evidence/build.log"
for flag in CONFIG_ARM64_MMU_ASSERT=y CONFIG_SMP_NCPUS=8 CONFIG_SYSTEM_A733HOME=y CONFIG_BLUETOOTH_GATT_CLIENT=y CONFIG_LIBUV_THREADPOOL_SIZE=1 CONFIG_A733_I2S0_AUDIO=y; do
  grep -qx "$flag" "$build/.config"
done
grep -qx '# CONFIG_BLUETOOTH_BREDR_SUPPORT is not set' "$build/.config"
expected=fece72b9666b3d3b99c6ec3831e26ae6962fa33404aa13e6db55217903b2eb1f
actual=$(sha256sum "$base" | cut -d ' ' -f1)
[[ "$actual" == "$expected" ]] || { echo 'v146 recovery image hash mismatch' >&2; exit 1; }
bash "$team/tools/check-a733-elf-layout.sh" "$build/nuttx" "$build/nuttx.bin" \
  | tee "$evidence/layout.log"
kernel_hash=$(sha256sum "$build/nuttx.bin" | cut -d ' ' -f1)
bash "$team/tools/package-a733-kernel-image.sh" "$base" "$output" "$build/nuttx.bin" \
  2>&1 | tee "$evidence/package.log"
bash "$team/tools/verify-a733-image.sh" "$output" "$kernel_hash" \
  2>&1 | tee "$evidence/verify.log"
cp "$build/.config" "$evidence/build.config"
echo 'v147 frame-counter candidate packaged; real recording/clock/acoustic acceptance pending.'
