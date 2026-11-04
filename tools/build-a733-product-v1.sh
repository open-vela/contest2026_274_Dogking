#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
cd "$(dirname "$0")/.."
export A733_PRODUCT_V1=1
export A733_V216_BUILD_DIR="${A733_PRODUCT_BUILD_DIR:-$(cd ../../quickly-openvela && pwd)/cmake_out/cubie-a7z_nsh_v217_product}"
if [[ "${3:-}" == --incremental ]]; then
  grep -qx CONFIG_A733_PRODUCT_V1=y "$A733_V216_BUILD_DIR/.config" || {
    echo 'STOP: legacy cache does not enable product v1; use a NEW build directory (no incremental flag)' >&2
    exit 1
  }
fi
bash tools/build-a733-voice-v216.sh "$@"
grep -qx CONFIG_A733_PRODUCT_V1=y "$A733_V216_BUILD_DIR/.config"
