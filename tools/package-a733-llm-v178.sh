#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
cd "$(dirname "$0")/.."
sdk=${OPENVELA_WORKSPACE:-$(cd ../../quickly-openvela && pwd)}
build=${A733_BUILD_DIR:-$sdk/cmake_out/cubie-a7z_nsh_v178_llm_candidate}
image=../openvela-a733-cubie-a7z-sd-v178-llm-candidate.img
cap=out/v178-llm-candidate-20261025
export PATH="$sdk/prebuilts/gcc/linux-x86_64/aarch64-none-elf/bin:$PATH"
python3 tools/check-a733-mcu-staging.py restored "$sdk" out/sdk-before-v178.json
python3 - <<'PY'
import hashlib,json,re
from pathlib import Path
r=Path.cwd();overlay=r/'board/a733-cubie-a7z/openvela-overlay'
for name in ('v178-staged.json','v178-mcu-unchanged-staged.json'):
    record=json.loads((r/'out'/name).read_text())
    for path,digest in record['files'].items():
        assert hashlib.sha256((overlay/path).read_bytes()).hexdigest()==digest,path
# User explicitly skipped MCU changes: compare byte-for-byte to this turn's
# immutable pre-development checkpoint, not just to the staged new sources.
before=json.loads((r/'out/checkpoints/a733-pre-v178-offline-20261025/manifest.json').read_text())
for name,info in before['files'].items():
    if '/mcu_' in name or name.endswith('/a733_uart4.c'):
        assert hashlib.sha256((r/name).read_bytes()).hexdigest()==info['sha256'],name
text=(r/'out/v178-minicpm5-directory-final.log').read_text(errors='replace')
rows=re.findall(r'LOGITS prompt=.*? step=\d+ maxabs=([\d.e+-]+) rmse=([\d.e+-]+) top1=(\d+)/(\d+)',text)
assert len(rows)==12 and all(float(a)<=1e-5 and float(b)<=1e-6 and c==d for a,b,c,d in rows)
assert 'PASS: real MiniCPM5 tokenization' in text
assert 'REAL_UTF8_API_REPLY=1+1=2' in text
qwen=(r/'out/v178-qwen-compat.log').read_text(errors='replace')
assert 'PASS: real Qwen2 backward-compatibility' in qwen
assert 'tensor-directory cache-hit' in text and 'prefill-cache: reused=25' in text
assert 'PASS: v178 model routing' in (r/'out/v178-llm-regression.log').read_text(errors='replace')
assert 'openvela-boundary: passed' in (r/'out/v178-audio-regression.log').read_text(errors='replace')
assert json.loads((r/'out/asr-export-v178-20261025/artifacts-verified.json').read_text())['result']=='EXPORTED_CID_CHECKED_NOT_BOARD_TESTED'
for item in json.loads((r/'out/v178-arm-preflight-r6/summary.json').read_text()).values():
    assert hashlib.sha256((r/item['source']).read_bytes()).hexdigest()==item['sha256']
PY
echo '3a14cf7bf03336160bb7e5ccd1c1da484d0c6a5902c9ba1ea2c69564a0f95ba1  ../openvela-a733-cubie-a7z-sd-v177-mcu-uart-candidate.img' | sha256sum -c -
bash tools/check-a733-elf-layout.sh "$build/nuttx" "$build/nuttx.bin" >out/v178-layout.log 2>&1
aarch64-none-elf-nm -C --defined-only "$build/nuttx" >out/v178-symbols.txt
for symbol in 'aipet::local_route_stream(' 'aipetllm_swiglu' 'aipetllm_softmax_exp' 'aipet::mcu_worker_main()'; do
  grep -F "$symbol" out/v178-symbols.txt
done
bash tools/package-a733-kernel-image.sh ../openvela-a733-cubie-a7z-sd-v177-mcu-uart-candidate.img \
  "$image" "$build/nuttx.bin" >out/v178-package.log 2>&1
kernel_sha=$(sha256sum "$build/nuttx.bin" | cut -d' ' -f1)
bash tools/verify-a733-image.sh "$image" "$kernel_sha" >out/v178-image-verified.log 2>&1
GIT_OPTIONAL_LOCKS=0 python3 tools/seal-a733-voice-candidate.py "$build" "$image" \
  out/a733-linux-asr-contract-v178-20261025-r2.tar.gz "$cap" \
  out/a733-v178-llm-source-20261025.tar.gz out/v178-staged.json >out/v178-sealed.json
cp -a docs "$cap/docs"
cp out/v178-sdk-restored.json out/v178-llm-regression.log out/v178-audio-regression.log \
  out/v178-minicpm5-directory-final.log out/v178-layout.log out/v178-image-verified.log \
  out/v178-mcu-unchanged-staged.json "$cap/"
cp out/asr-export-v178-20261025/artifacts-verified.json "$cap/asr-compiler-artifacts.json"
cp out/v178-qwen-compat.log "$cap/"
cp -a out/v178-arm-preflight-r6 "$cap/arm64-preflight"
printf 'v178 candidate sealed; NO board flashing or end-to-end speech claim\n'
sha256sum "$image" "$build/nuttx.bin"
