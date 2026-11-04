#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/.." && pwd -P)
tmp=$(mktemp -d /tmp/aipet-npuref.XXXXXX)
trap 'rm -rf -- "$tmp"' EXIT
sdk=${OPENVELA_WORKSPACE:-$repo/../../quickly-openvela}
for src in "$sdk/nuttx/libs/libc/misc/lib_crc32.c" "$repo/tools/tensor-crc-mock.c"; do
  gcc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
    -I"$repo/tools/tensor-mock" -I"$repo/board/a733-cubie-a7z/openvela-overlay/apps/include" -c "$src" -o "$tmp/$(basename "$src").o"
done
gcc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
  -c "$repo/board/a733-cubie-a7z/openvela-overlay/apps/system/aipet/voice_kws.c" -o "$tmp/kws.o"
g++ -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -pthread \
  -I"$repo/board/a733-cubie-a7z/openvela-overlay/apps/include" \
  "$repo/tools/test-aipet-npuref.cxx" "$tmp/kws.o" "$tmp/lib_crc32.c.o" "$tmp/tensor-crc-mock.c.o" -o "$tmp/test"
"$tmp/test" "$tmp"
