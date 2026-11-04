#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
team=$(cd "$(dirname "$0")/.." && pwd)
official=$(cd "${OPENVELA_WORKSPACE:-$team/../../quickly-openvela}" && pwd)
build="$official/cmake_out/cubie-a7z_nsh_v144_ble_reopen"
base=${A733_BASE_IMAGE:-$team/../openvela-a733-cubie-a7z-sd-ble-v143-lifecycle-candidate.img}
output=${1:-$team/../openvela-a733-cubie-a7z-sd-ble-v144-reopen-candidate.img}
evidence="$team/docs/evidence"
export PATH="$official/prebuilts/gcc/linux-x86_64/aarch64-none-elf/bin:/usr/bin:/bin:$PATH"
test -s "$build/nuttx.bin"
test -s "$build/nuttx"
grep -q '^Official source restoration: PASS' "$evidence/20261014-v144-official-restore.log"
grep -q 'a733-layout: page alignment/segments/stack/Image footprint passed' "$evidence/20261014-v144-build.log"
for flag in CONFIG_ARM64_MMU_ASSERT=y CONFIG_SMP_NCPUS=8 CONFIG_BLUETOOTH_GATT_CLIENT=y CONFIG_LIBUV_THREADPOOL_SIZE=1; do
  grep -qx "$flag" "$build/.config"
done
grep -qx '# CONFIG_BLUETOOTH_BREDR_SUPPORT is not set' "$build/.config"
if [[ -z "${A733_BASE_IMAGE:-}" ]]; then
  expected=fa3b6592e35a367de29cd1b4fe3b7a6233d3ebdaeea106e6b88bbbd037bb0bf5
  actual=$(sha256sum "$base" | cut -d ' ' -f1)
  [[ "$actual" == "$expected" ]] || { echo 'v143 recovery image hash mismatch' >&2; exit 1; }
fi
bash "$team/tools/check-a733-elf-layout.sh" "$build/nuttx" "$build/nuttx.bin" \
  | tee "$evidence/20261014-v144-layout.log"
kernel_hash=$(sha256sum "$build/nuttx.bin" | cut -d ' ' -f1)
bash "$team/tools/package-a733-kernel-image.sh" "$base" "$output" "$build/nuttx.bin" \
  2>&1 | tee "$evidence/20261014-v144-package.log"
bash "$team/tools/verify-a733-image.sh" "$output" "$kernel_hash" \
  2>&1 | tee "$evidence/20261014-v144-verify.log"
cp "$build/.config" "$evidence/20261014-v144-build.config"
echo 'Candidate packaged/verified; BLE reopen stress and Wi-Fi coexistence require board testing.'
