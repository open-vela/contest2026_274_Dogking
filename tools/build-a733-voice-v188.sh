#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Fresh boot-hardening/diagnostic candidate. Never reuses the v185 build dir.
set -euo pipefail
cd "$(dirname "$0")/.."
[[ $# == 2 ]]
prefix=$(realpath "$2")
[[ -f "$prefix/manifest.json" ]]
export OPENVELA_WORKSPACE=${OPENVELA_WORKSPACE:-$(cd ../../quickly-openvela && pwd)}
export A733_ORT_STATIC_PREFIX="$prefix" A733_GENERAL_VOICE_EXPERIMENTAL=1
export A733_BOOT_DIAGNOSTICS=1
export A733_GENERAL_BUILD_DIR="$OPENVELA_WORKSPACE/cmake_out/cubie-a7z_nsh_v188_boot"
[[ ! -e "$A733_GENERAL_BUILD_DIR" ]] || {
  echo 'Refusing to erase/reuse v188 build; choose a fresh wrapper/build directory.' >&2
  exit 1
}
bash tools/build-a733-general-v184.sh "$1"
