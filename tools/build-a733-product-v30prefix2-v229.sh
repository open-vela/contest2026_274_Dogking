#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
cd "$(dirname "$0")/.."
export A733_CLOUD_TRANSPORT_V229=1
export A733_V228_BUILD_DIR="${A733_V229_BUILD_DIR:-$(cd ../../quickly-openvela && pwd)/cmake_out/cubie-a7z_nsh_v229_cloud_transport}"
bash tools/build-a733-product-v30prefix-v228.sh "$@"
