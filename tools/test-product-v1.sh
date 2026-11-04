#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
cd "$(dirname "$0")/.."
pet=board/a733-cubie-a7z/openvela-overlay/apps/system/aipet
headers=board/a733-cubie-a7z/openvela-overlay/apps/include
tmp=$(mktemp -d /tmp/a733-product-v1.XXXXXX)
trap 'rm -rf -- "$tmp"' EXIT
g++ -std=c++17 -g -O1 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I"$pet" -I"$headers" tools/test-product-v1.cxx "$pet/product_core.cxx" \
  "$pet/pet_core.cxx" "$pet/mcu_protocol.cxx" -o "$tmp/product"
"$tmp/product"
python3 tools/test-product-runtime-v1.py
