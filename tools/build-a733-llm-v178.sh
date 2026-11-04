#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
cd "$(dirname "$0")/.."
export OPENVELA_WORKSPACE=${OPENVELA_WORKSPACE:-$(cd ../../quickly-openvela && pwd)}
export A733_BUILD_DIR=${A733_BUILD_DIR:-$OPENVELA_WORKSPACE/cmake_out/cubie-a7z_nsh_v178_llm_candidate}
export A733_TENSOR_EXPERIMENTAL=1 A733_UFS_EXPERIMENTAL=1
export A733_UFS_HANDOFF_EXPERIMENTAL=1 A733_UFS_QUIESCE_EXPERIMENTAL=1
export A733_UFS_DMA_NOP_EXPERIMENTAL=1 A733_UFS_READCHECK_EXPERIMENTAL=1
export A733_UFS_BLOCK_RO_EXPERIMENTAL=1 A733_UFS_SCRATCH_EXPERIMENTAL=1
export A733_UFS_DATA_EXPERIMENTAL=1 JOBS=${JOBS:-12}
export A733_FORCE_LOCAL_LLM_REBUILD=1
mkdir -p out
if [[ "${1:-}" != --incremental ]]; then
  [[ ! -e "$A733_BUILD_DIR/nuttx" && ! -e out/sdk-before-v178.json ]]
  python3 tools/check-a733-mcu-staging.py snapshot "$OPENVELA_WORKSPACE" out/sdk-before-v178.json
fi
bash tools/build-a733-wapi.sh "${1:-}" >out/build-v178-llm.log 2>&1 &
builder=$!
trap 'wait "$builder" || true' EXIT
matched=0
for ((i=0;i<90;i++)); do
  if python3 tools/check-a733-llm-staged.py "$OPENVELA_WORKSPACE" >out/v178-staged.tmp 2>out/v178-staging-error.log; then
    mv out/v178-staged.tmp out/v178-staged.json
    python3 tools/check-a733-mcu-staging.py staged "$OPENVELA_WORKSPACE" >out/v178-mcu-unchanged-staged.json
    matched=1;break
  fi
  sleep 2
done
[[ "$matched" == 1 ]]
wait "$builder"
trap - EXIT
python3 tools/check-a733-mcu-staging.py restored "$OPENVELA_WORKSPACE" out/sdk-before-v178.json >out/v178-sdk-restored.json
printf 'v178 linked; SDK restored. Candidate only; board acceptance required.\n'
