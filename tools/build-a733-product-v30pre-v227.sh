#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
cd "$(dirname "$0")/.."
export A733_CLOUD_LATENCY_V227=1
export A733_V226_BUILD_DIR="${A733_V227_BUILD_DIR:-$(cd ../../quickly-openvela && pwd)/cmake_out/cubie-a7z_nsh_v227_product_v30pre}"
bash tools/build-a733-product-v21pre-v226.sh "$@"
