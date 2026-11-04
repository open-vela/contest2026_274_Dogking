#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
cd "$(dirname "$0")/.."
# Separate candidate cache: never overwrite the audited v217 build/images.
export A733_PRODUCT_BUILD_DIR="${A733_CLOUD_BUILD_DIR:-$(cd ../../quickly-openvela && pwd)/cmake_out/cubie-a7z_nsh_v218_cloud_voice}"
bash tools/build-a733-product-v1.sh "$@"
