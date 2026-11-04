#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
cd "$(dirname "$0")/.."
# Preserve the v217 audited image/cache and the separate v218 cloud cache.
export A733_PRODUCT_BUILD_DIR="${A733_V219_BUILD_DIR:-$(cd ../../quickly-openvela && pwd)/cmake_out/cubie-a7z_nsh_v219_product_v11}"
bash tools/build-a733-product-v1.sh "$@"
