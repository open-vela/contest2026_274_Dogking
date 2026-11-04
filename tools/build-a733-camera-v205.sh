#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
cd "$(dirname "$0")/.."
[[ $# == 2 || ( $# == 3 && "$3" == --incremental ) ]]
export OPENVELA_WORKSPACE=${OPENVELA_WORKSPACE:-$(cd ../../quickly-openvela && pwd)}
export A733_GENERAL_BUILD_DIR="$OPENVELA_WORKSPACE/cmake_out/cubie-a7z_nsh_v205_camera_perf"
export A733_ORT_STATIC_PREFIX=$(realpath "$2") A733_GENERAL_VOICE_EXPERIMENTAL=1
export A733_BOOT_DIAGNOSTICS=1 A733_CAMERA_EXPERIMENTAL=1 A733_CAMERA_CONTROL_EXPERIMENTAL=1
export A733_CAMERA_LIVE_EXPERIMENTAL=1 A733_CAMERA_PERF_EXPERIMENTAL=1
test -f "$A733_ORT_STATIC_PREFIX/manifest.json"
if [[ "${3:-}" == --incremental ]]; then
  grep -qx CONFIG_A733_CAMERA_PERF_TEST=y "$A733_GENERAL_BUILD_DIR/.config"
  grep -qx CONFIG_FS_TMPFS=y "$A733_GENERAL_BUILD_DIR/.config"
  bash tools/build-a733-general-v184.sh "$1" --incremental
else
  test ! -e "$A733_GENERAL_BUILD_DIR" || { echo 'NEW v205 cache required; no implicit deletion';exit 1; }
  bash tools/build-a733-general-v184.sh "$1"
fi
grep -qx CONFIG_A733_CAMERA_PERF_TEST=y "$A733_GENERAL_BUILD_DIR/.config"
grep -qx CONFIG_A733_CAMERA_LIVE_TEST=y "$A733_GENERAL_BUILD_DIR/.config"
grep -qx CONFIG_FS_TMPFS=y "$A733_GENERAL_BUILD_DIR/.config"
