#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/.." && pwd -P)
sdk=${OPENVELA_WORKSPACE:-$repo/../../quickly-openvela}
tmp=$(mktemp -d /tmp/a733-crc.XXXXXX)
trap 'rm -rf -- "$tmp"' EXIT
for mode in fast slow; do
  flags=();if [[ "$mode" == slow ]];then flags=(-DCONFIG_LIBC_CRC32_SLOW=1);fi
  gcc -std=c11 -O2 -Wall -Wextra -Werror -fsanitize=address,undefined "${flags[@]}" \
    -I"$repo/tools/tensor-mock" "$repo/tools/test-a733-crc.c" "$sdk/nuttx/libs/libc/misc/lib_crc32.c" -o "$tmp/$mode"
  "$tmp/$mode"
done
