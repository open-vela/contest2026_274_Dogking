#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
cd "$(dirname "$0")/.."
run=${1:-out/live-build-v182-r1}
[[ "$run" =~ ^out/[a-zA-Z0-9_-]+$ && ! -e "$run" ]]
mkdir "$run"
export OPENVELA_WORKSPACE=${OPENVELA_WORKSPACE:-$(cd ../../quickly-openvela && pwd)}
export A733_BUILD_DIR="$OPENVELA_WORKSPACE/cmake_out/cubie-a7z_nsh_v182_live_capture"
export A733_TENSOR_EXPERIMENTAL=1 A733_UFS_EXPERIMENTAL=1
export A733_UFS_HANDOFF_EXPERIMENTAL=1 A733_UFS_QUIESCE_EXPERIMENTAL=1
export A733_UFS_DMA_NOP_EXPERIMENTAL=1 A733_UFS_READCHECK_EXPERIMENTAL=1
export A733_UFS_BLOCK_RO_EXPERIMENTAL=1 A733_UFS_SCRATCH_EXPERIMENTAL=1
export A733_UFS_DATA_EXPERIMENTAL=1 JOBS=${JOBS:-12} A733_FORCE_LOCAL_LLM_REBUILD=1
python3 tools/check-a733-mcu-staging.py snapshot "$OPENVELA_WORKSPACE" "$run/sdk-before.json"
bash tools/build-a733-wapi.sh "${2:-}" >"$run/build.log" 2>&1 &
builder=$!
trap 'wait "$builder" || true' EXIT
matched=0
for ((i=0;i<90;i++)); do
  if python3 tools/check-a733-voice-staged.py "$OPENVELA_WORKSPACE" >"$run/staged.tmp" 2>"$run/staging-error.log"; then
    mv "$run/staged.tmp" "$run/staged.json";matched=1;break
  fi
  sleep 2
done
[[ "$matched" == 1 ]]
wait "$builder"
trap - EXIT
python3 tools/check-a733-mcu-staging.py restored "$OPENVELA_WORKSPACE" "$run/sdk-before.json" >"$run/sdk-restored.json"
"$OPENVELA_WORKSPACE/prebuilts/gcc/linux-x86_64/aarch64-none-elf/bin/aarch64-none-elf-nm" -C "$A733_BUILD_DIR/nuttx" >"$run/symbols.txt"
for symbol in aipetllm_prefill_control 'aipet::audio_capture_live(' 'aipet::voice_play_stream('; do
  grep -F "$symbol" "$run/symbols.txt"
done
echo 'v182 source/build passed; no board acceptance or general target ASR/TTS provider claim.'
