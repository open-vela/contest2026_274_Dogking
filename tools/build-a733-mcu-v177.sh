#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
cd "$repo"
export OPENVELA_WORKSPACE=${OPENVELA_WORKSPACE:-$(cd ../../quickly-openvela && pwd)}
export A733_BUILD_DIR=${A733_BUILD_DIR:-$OPENVELA_WORKSPACE/cmake_out/cubie-a7z_nsh_v177_mcu_uart_candidate}
export A733_TENSOR_EXPERIMENTAL=1 A733_UFS_EXPERIMENTAL=1
export A733_UFS_HANDOFF_EXPERIMENTAL=1 A733_UFS_QUIESCE_EXPERIMENTAL=1
export A733_UFS_DMA_NOP_EXPERIMENTAL=1 A733_UFS_READCHECK_EXPERIMENTAL=1
export A733_UFS_BLOCK_RO_EXPERIMENTAL=1 A733_UFS_SCRATCH_EXPERIMENTAL=1
export A733_UFS_DATA_EXPERIMENTAL=1 JOBS=${JOBS:-16}
mkdir -p out
if [[ "${1:-}" != --incremental ]]; then
  [[ ! -e "$A733_BUILD_DIR/nuttx" && ! -e out/sdk-before-v177.json ]]
  python3 tools/check-a733-mcu-staging.py snapshot "$OPENVELA_WORKSPACE" out/sdk-before-v177.json
fi
bash tools/build-a733-wapi.sh "${1:-}" >out/build-v177-mcu.log 2>&1 &
builder=$!
trap 'wait "$builder" || true' EXIT
matched=0
for ((i=0;i<60;i++)); do
  if python3 tools/check-a733-mcu-staging.py staged "$OPENVELA_WORKSPACE" >out/v177-mcu-staged.tmp 2>out/v177-mcu-staging-error.log; then
    mv out/v177-mcu-staged.tmp out/v177-mcu-staged.json
    python3 tools/check-a733-tensor-staged.py "$OPENVELA_WORKSPACE" --ufs >out/v177-existing-staged.json
    matched=1;break
  fi
  sleep 2
done
[[ "$matched" == 1 ]]
wait "$builder"
trap - EXIT
python3 tools/check-a733-mcu-staging.py restored "$OPENVELA_WORKSPACE" out/sdk-before-v177.json >out/v177-sdk-restored.json
printf 'v177 build linked; SDK restored; hardware acceptance still required\n'
