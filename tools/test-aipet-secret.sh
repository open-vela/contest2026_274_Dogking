#!/usr/bin/env bash
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
pet="$repo/board/a733-cubie-a7z/openvela-overlay/apps/system/aipet"
mb="$repo/../../quickly-openvela/apps/crypto/mbedtls/mbedtls"
tmp=$(mktemp -d)
trap 'rm -f "$tmp/test" "$tmp/device.key"; rmdir "$tmp"' EXIT
gcc -D_DEFAULT_SOURCE -Wall -Wextra -Werror -fsanitize=address,undefined -pthread \
  -DMBEDTLS_CONFIG_FILE='"aipet-test-mbedtls.h"' \
  -DAIPET_KEY_FILE="\"$tmp/device.key\"" \
  -I"$repo/tools" -I"$pet" -I"$mb/include" \
  "$repo/tools/test-aipet-secret.c" "$pet/agent_secret.c" \
  "$mb/library/"{aes,gcm,cipher,cipher_wrap,platform_util,constant_time}.c -o "$tmp/test"
"$tmp/test"
