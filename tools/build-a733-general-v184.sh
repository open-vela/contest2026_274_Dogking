#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Candidate full-link/ABI build only. No image packaging or board writes.
set -euo pipefail
cd "$(dirname "$0")/.."
run=${1:-out/general-build-v184-r1}
[[ "$run" =~ ^out/[a-zA-Z0-9_-]+$ && ! -e "$run" ]]
mkdir "$run"
export OPENVELA_WORKSPACE=${OPENVELA_WORKSPACE:-$(cd ../../quickly-openvela && pwd)}
export A733_BUILD_DIR="${A733_GENERAL_BUILD_DIR:-$OPENVELA_WORKSPACE/cmake_out/cubie-a7z_nsh_v184_general_voice}"
export A733_TENSOR_EXPERIMENTAL=1 A733_GENERAL_ASR_EXPERIMENTAL=1
export A733_CPU_ACOUSTIC_EXPERIMENTAL=1 A733_UFS_EXPERIMENTAL=1
export A733_UFS_HANDOFF_EXPERIMENTAL=1 A733_UFS_QUIESCE_EXPERIMENTAL=1
export A733_UFS_DMA_NOP_EXPERIMENTAL=1 A733_UFS_READCHECK_EXPERIMENTAL=1
export A733_UFS_BLOCK_RO_EXPERIMENTAL=1 A733_UFS_SCRATCH_EXPERIMENTAL=1
export A733_UFS_DATA_EXPERIMENTAL=1 JOBS=${JOBS:-6} A733_FORCE_LOCAL_LLM_REBUILD=1
python3 tools/check-a733-mcu-staging.py snapshot "$OPENVELA_WORKSPACE" "$run/sdk-before.json"
bash tools/build-a733-wapi.sh "${2:-}" >"$run/build.log" 2>&1 &
builder=$!
trap 'wait "$builder" || true' EXIT
set +e
wait "$builder"
status=$?
set -e
printf '%s\n' "$status" >"$run/wrapper-status.txt"
trap - EXIT
python3 tools/check-a733-mcu-staging.py restored "$OPENVELA_WORKSPACE" "$run/sdk-before.json" >"$run/sdk-restored.json"
[[ "$status" == 0 ]]
"$OPENVELA_WORKSPACE/prebuilts/gcc/linux-x86_64/aarch64-none-elf/bin/aarch64-none-elf-nm" \
  -n -C "$A733_BUILD_DIR/nuttx" >"$run/symbols.txt"
grep -E 'g_npu_arena|_ebss|_end' "$run/symbols.txt"
for symbol in 'aipet::voice_general_asr_open(' 'aipet::voice_general_live('; do
  grep -F "$symbol" "$run/symbols.txt"
done
echo 'PASS: v184 candidate linked with larger DMA + matching libc++; NOT board acceptance'
