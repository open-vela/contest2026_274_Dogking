#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Application-only v221 revision of the v220 preview profile, NEW cache.
set -euo pipefail
cd "$(dirname "$0")/.."
export A733_V220_BUILD_DIR="${A733_V221_BUILD_DIR:-$(cd ../../quickly-openvela && pwd)/cmake_out/cubie-a7z_nsh_v221_product_v12fix_preview}"
bash tools/build-a733-product-v12-v220.sh "$@"
