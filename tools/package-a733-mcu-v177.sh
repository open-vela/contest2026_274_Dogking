#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
cd "$repo"
sdk=${OPENVELA_WORKSPACE:-$(cd ../../quickly-openvela && pwd)}
build=${A733_BUILD_DIR:-$sdk/cmake_out/cubie-a7z_nsh_v177_mcu_uart_candidate}
image=../openvela-a733-cubie-a7z-sd-v177-mcu-uart-candidate.img
cap=out/v177-mcu-uart-candidate-20261023
export PATH="$sdk/prebuilts/gcc/linux-x86_64/aarch64-none-elf/bin:$PATH"
python3 tools/check-a733-mcu-staging.py restored "$sdk" out/sdk-before-v177.json
python3 - <<'PY'
import hashlib,json
from pathlib import Path
r=Path.cwd();overlay=r/'board/a733-cubie-a7z/openvela-overlay'
for name in ('v177-mcu-staged.json','v177-existing-staged.json'):
    record=json.loads((r/'out'/name).read_text())
    for path,digest in record['files'].items():
        assert hashlib.sha256((overlay/path).read_bytes()).hexdigest()==digest,path
assert 'PASS: real application voice-route-UART-TTS pipeline' in (r/'out/v177-mcu-host-release.log').read_text()
assert 'PASS: late ACK and fault-before-ACK cannot revive motion' in (r/'out/v177-mcu-host-release.log').read_text()
assert 'openvela-boundary: passed' in (r/'out/v177-regression-final.log').read_text()
for row in json.loads((r/'out/v177-mcu-arm-objects-r3/summary.json').read_text()).values():
    assert hashlib.sha256((r/row['source']).read_bytes()).hexdigest()==row['source_sha256'],row['source']
assert 'bounded ordered routes/config/template tests passed' in (r/'out/v177-routes-release.log').read_text()
assert 'PASS: UART4 actual driver bodies' in (r/'out/v177-uart4-release.log').read_text()
assert 'PASS: real ARM64 GOT fixture rejected when orphaned' in (r/'out/v177-got-layout-test.log').read_text()
PY
echo 'b321a8d6fa53dac5053891f1df2bf3425e7280256a78405c74a0823336b42271  ../openvela-a733-cubie-a7z-sd-v175-ufs-auto-candidate.img' | sha256sum -c -
bash tools/check-a733-elf-layout.sh "$build/nuttx" "$build/nuttx.bin" >out/v177-layout.log 2>&1
aarch64-none-elf-nm -C --defined-only "$build/nuttx" >out/v177-symbols.txt
for symbol in 'aipet::mcu_worker_main()' 'aipet_mcu_tools_register' 'aipet::mcu_request('; do
  grep -F "$symbol" out/v177-symbols.txt
done
bash tools/package-a733-kernel-image.sh ../openvela-a733-cubie-a7z-sd-v175-ufs-auto-candidate.img \
  "$image" "$build/nuttx.bin" >out/v177-package.log 2>&1
kernel_sha=$(sha256sum "$build/nuttx.bin" | cut -d' ' -f1)
bash tools/verify-a733-image.sh "$image" "$kernel_sha" >out/v177-image-verified.log 2>&1
GIT_OPTIONAL_LOCKS=0 python3 tools/seal-a733-voice-candidate.py "$build" "$image" \
  out/a733-openvela-kws-v158-r2.tar.gz "$cap" out/a733-v177-mcu-uart-source-20261023.tar.gz \
  out/v177-existing-staged.json >out/v177-sealed.json
cp -a docs "$cap/docs"
cp out/v177-mcu-staged.json out/v177-sdk-restored.json out/v177-mcu-host-release.log \
  out/v177-regression-final.log out/v177-layout.log out/v177-image-verified.log \
  out/v177-routes-release.log out/v177-uart4-release.log out/v177-ufs-data-regression.log \
  out/v177-got-layout-test.log "$cap/"
cp -a out/v177-mcu-arm-objects-r3 "$cap/arm64-check"
echo 'v177 candidate packaged and verified; physical board/MCU/cloud speech NOT tested here'
sha256sum "$image" "$build/nuttx.bin"
