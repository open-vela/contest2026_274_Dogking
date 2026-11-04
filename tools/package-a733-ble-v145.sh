#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
team=$(cd "$(dirname "$0")/.." && pwd)
official=$(cd "${OPENVELA_WORKSPACE:-$team/../../quickly-openvela}" && pwd)
build="$official/cmake_out/cubie-a7z_nsh_v145_gatt_client"
base=${A733_BASE_IMAGE:-$team/../openvela-a733-cubie-a7z-sd-ble-v144-reopen-candidate.img}
output=${1:-$team/../openvela-a733-cubie-a7z-sd-ble-v145-gatt-client-candidate.img}
evidence="$team/docs/evidence"
export PATH="$official/prebuilts/gcc/linux-x86_64/aarch64-none-elf/bin:/usr/bin:/bin:$PATH"
test -s "$build/nuttx.bin"
test -s "$build/nuttx"
grep -q '^Official source restoration: PASS' "$evidence/20261015-v145-official-restore.log"
grep -q 'a733-layout: page alignment/segments/stack/Image footprint passed' "$evidence/20261015-v145-build.log"
for flag in CONFIG_ARM64_MMU_ASSERT=y CONFIG_SMP_NCPUS=8 CONFIG_SYSTEM_A733HOME=y CONFIG_BLUETOOTH_GATT_CLIENT=y CONFIG_LIBUV_THREADPOOL_SIZE=1; do
  grep -qx "$flag" "$build/.config"
done
grep -qx '# CONFIG_BLUETOOTH_BREDR_SUPPORT is not set' "$build/.config"
if [[ -z "${A733_BASE_IMAGE:-}" ]]; then
  expected=829a1e972129c35a3347575ee5dac219a060cdddec6206fbd410954c44b66716
  actual=$(sha256sum "$base" | cut -d ' ' -f1)
  [[ "$actual" == "$expected" ]] || { echo 'v144 recovery image hash mismatch' >&2; exit 1; }
fi
bash "$team/tools/check-a733-elf-layout.sh" "$build/nuttx" "$build/nuttx.bin" \
  | tee "$evidence/20261015-v145-layout.log"
kernel_hash=$(sha256sum "$build/nuttx.bin" | cut -d ' ' -f1)
bash "$team/tools/package-a733-kernel-image.sh" "$base" "$output" "$build/nuttx.bin" \
  2>&1 | tee "$evidence/20261015-v145-package.log"
bash "$team/tools/verify-a733-image.sh" "$output" "$kernel_hash" \
  2>&1 | tee "$evidence/20261015-v145-verify.log"
cp "$build/.config" "$evidence/20261015-v145-build.config"
echo 'GATT application candidate packaged; no v145 board/peer/GATT success claimed.'
