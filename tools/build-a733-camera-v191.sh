#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# First fully linked camera qualification candidate; NEVER a capture claim.
set -euo pipefail
cd "$(dirname "$0")/.."
[[ $# == 2 || ( $# == 3 && "$3" == --incremental ) ]]
prefix=$(realpath "$2"); [[ -f "$prefix/manifest.json" ]]
export OPENVELA_WORKSPACE=${OPENVELA_WORKSPACE:-$(cd ../../quickly-openvela && pwd)}
export A733_ORT_STATIC_PREFIX="$prefix" A733_GENERAL_VOICE_EXPERIMENTAL=1
export A733_BOOT_DIAGNOSTICS=1 A733_CAMERA_EXPERIMENTAL=1
export A733_GENERAL_BUILD_DIR="$OPENVELA_WORKSPACE/cmake_out/cubie-a7z_nsh_v191_camera"
if [[ "${3:-}" == --incremental ]]; then
  [[ -f "$A733_GENERAL_BUILD_DIR/CMakeCache.txt" ]]
  for option in CONFIG_A733_BOOT_DIAGNOSTICS=y CONFIG_A733_IMX214_CHECKPOINT=y \
    CONFIG_SYSTEM_A733CAMERA=y CONFIG_BOARD_INITTHREAD_STACKSIZE=262144; do
    grep -qx "$option" "$A733_GENERAL_BUILD_DIR/.config"
  done
  bash tools/build-a733-general-v184.sh "$1" --incremental
else
  [[ ! -e "$A733_GENERAL_BUILD_DIR" ]] || { echo 'Refusing implicit reuse/erase of v191.' >&2;exit 1; }
  bash tools/build-a733-general-v184.sh "$1"
fi
