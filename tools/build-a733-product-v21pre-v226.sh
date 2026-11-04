#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
cd "$(dirname "$0")/.."
export A733_V225_BUILD_DIR="${A733_V226_BUILD_DIR:-$(cd ../../quickly-openvela && pwd)/cmake_out/cubie-a7z_nsh_v226_product_v21pre}"
bash tools/build-a733-product-v20pre-v225.sh "$@"
