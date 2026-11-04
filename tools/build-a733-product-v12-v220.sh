#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
cd "$(dirname "$0")/.."
export A733_PRODUCT_V12=1
export A733_PRODUCT_BUILD_DIR="${A733_V220_BUILD_DIR:-$(cd ../../quickly-openvela && pwd)/cmake_out/cubie-a7z_nsh_v220_product_v12_preview}"
if [[ -f "$A733_PRODUCT_BUILD_DIR/.config" ]] &&
   ! grep -qx CONFIG_A733_PRODUCT_V12=y "$A733_PRODUCT_BUILD_DIR/.config"; then
  echo 'STOP: this is not a v1.2 preview cache; preserve the stable cache and choose a NEW directory' >&2
  exit 1
fi
if [[ "${3:-}" == --incremental ]]; then
  grep -qx CONFIG_A733_PRODUCT_V12=y "$A733_PRODUCT_BUILD_DIR/.config"
fi
bash tools/build-a733-product-v1.sh "$@"
grep -qx CONFIG_A733_PRODUCT_V12=y "$A733_PRODUCT_BUILD_DIR/.config"
