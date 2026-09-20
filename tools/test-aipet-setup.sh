#!/usr/bin/env bash
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
official="$repo/../../quickly-openvela"
pet="$repo/board/a733-cubie-a7z/openvela-overlay/apps/system/aipet"
mb="$official/apps/crypto/mbedtls/mbedtls"
tmp=$(mktemp -d)
trap 'rm -f "$tmp/test" "$tmp/device.key" "$tmp/config/config.json"; rmdir "$tmp/config" 2>/dev/null || true; rmdir "$tmp"' EXIT
gcc -D_DEFAULT_SOURCE -Wall -Wextra -Werror -pthread \
  -DMBEDTLS_CONFIG_FILE='"aipet-test-mbedtls.h"' \
  -DAIPET_SETUP_DIR="\"$tmp\"" -DAIPET_KEY_FILE="\"$tmp/device.key\"" \
  -I"$repo/tools" -I"$pet" -I"$mb/include" -I"$official/apps/include" \
  -I"$official/apps/netutils/cjson/cJSON" \
  "$repo/tools/test-aipet-setup.c" "$pet/agent_setup.c" "$pet/agent_secret.c" \
  "$official/apps/netutils/cjson/cJSON/cJSON.c" \
  "$mb/library/"{aes,gcm,cipher,cipher_wrap,platform_util,constant_time}.c -lm -o "$tmp/test"
python3 "$repo/tools/test-aipet-setup.py" "$tmp/test" "$tmp"
