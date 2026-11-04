#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
cd "$(dirname "$0")/.."
export A733_V223_BUILD_DIR="${A733_V224_BUILD_DIR:-$(cd ../../quickly-openvela && pwd)/cmake_out/cubie-a7z_nsh_v224_product_v13fix2}"
bash tools/build-a733-product-v13fix-v223.sh "$@"
