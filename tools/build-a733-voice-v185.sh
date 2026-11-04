#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Explicit full candidate. Preserves passed images and restores official SDK.
set -euo pipefail
cd "$(dirname "$0")/.."
[[ $# == 2 ]]
prefix=$(realpath "$2")
[[ -f "$prefix/manifest.json" ]]
export OPENVELA_WORKSPACE=${OPENVELA_WORKSPACE:-$(cd ../../quickly-openvela && pwd)}
export A733_ORT_STATIC_PREFIX="$prefix" A733_GENERAL_VOICE_EXPERIMENTAL=1
export A733_GENERAL_BUILD_DIR="$OPENVELA_WORKSPACE/cmake_out/cubie-a7z_nsh_v185_general_voice"
mode=
if [[ -f "$A733_GENERAL_BUILD_DIR/CMakeCache.txt" ]]; then mode=--incremental; fi
bash tools/build-a733-general-v184.sh "$1" "$mode"
