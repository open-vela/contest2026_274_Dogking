#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
team=$(cd "$(dirname "$0")/.." && pwd)
official=$(cd "${OPENVELA_WORKSPACE:-$team/../../quickly-openvela}" && pwd)
build="$official/cmake_out/cubie-a7z_nsh_v143_ble_lifecycle"
base=${A733_BASE_IMAGE:-$team/../openvela-a733-cubie-a7z-sd-ble-v142-cli-hostfix-candidate.img}
output=${1:-$team/../openvela-a733-cubie-a7z-sd-ble-v143-lifecycle-candidate.img}
evidence="$team/docs/evidence"
export PATH="$official/prebuilts/gcc/linux-x86_64/aarch64-none-elf/bin:/usr/bin:/bin:$PATH"
test -s "$build/nuttx.bin"
test -s "$build/nuttx"
grep -q '^Official source restoration: PASS' "$evidence/20261014-v143-official-restore.log"
grep -q 'a733-layout: page alignment/segments/stack/Image footprint passed' "$evidence/20261014-v143-build.log"
for flag in CONFIG_ARM64_MMU_ASSERT=y CONFIG_SMP_NCPUS=8 CONFIG_BLUETOOTH_GATT_CLIENT=y CONFIG_LIBUV_THREADPOOL_SIZE=1; do
  grep -qx "$flag" "$build/.config"
done
grep -qx '# CONFIG_BLUETOOTH_BREDR_SUPPORT is not set' "$build/.config"
if [[ -z "${A733_BASE_IMAGE:-}" ]]; then
  expected=6ba47e14951119d25a29c745aa135110b6daf7e9a7c2fe17f69bcd034528a5bb
  actual=$(sha256sum "$base" | cut -d ' ' -f1)
  [[ "$actual" == "$expected" ]] || { echo 'v142 recovery image hash mismatch' >&2; exit 1; }
fi
bash "$team/tools/check-a733-elf-layout.sh" "$build/nuttx" "$build/nuttx.bin" \
  | tee "$evidence/20261014-v143-layout.log"
kernel_hash=$(sha256sum "$build/nuttx.bin" | cut -d ' ' -f1)
bash "$team/tools/package-a733-kernel-image.sh" "$base" "$output" "$build/nuttx.bin" \
  2>&1 | tee "$evidence/20261014-v143-package.log"
bash "$team/tools/verify-a733-image.sh" "$output" "$kernel_hash" \
  2>&1 | tee "$evidence/20261014-v143-verify.log"
echo 'Candidate packaged/verified; real board boot and BLE lifecycle still require testing.'
