#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/.." && pwd -P)
overlay="$repo/board/a733-cubie-a7z/openvela-overlay"
chip="$overlay/vendor/allwinnertech/chips/a733"
tmp=$(mktemp -d /tmp/a733-tensor.XXXXXX)
trap 'rm -rf -- "$tmp"' EXIT
sdk=${OPENVELA_WORKSPACE:-$repo/../../quickly-openvela}
gcc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I"$repo/tools/tensor-mock" -c "$sdk/nuttx/libs/libc/misc/lib_crc32.c" -o "$tmp/crc.o"
gcc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I"$chip/include" -I"$overlay/nuttx/include" "$repo/tools/test-a733-tensor.c" \
  "$chip/a733_tensor_plan.c" -o "$tmp/test"
"$tmp/test"
gcc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I"$chip" -I"$chip/include" -I"$overlay/nuttx/include" \
  "$repo/tools/test-a733-tensor-adapter.c" "$chip/a733_tensor_plan.c" -o "$tmp/adapter"
"$tmp/adapter"
gcc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I"$repo/tools/tensor-mock" -I"$chip" -I"$overlay/apps/include" -I"$overlay/nuttx/include" \
  "$repo/tools/test-a733-tensor-client.c" "$tmp/crc.o" -o "$tmp/client"
"$tmp/client"

# Include the real reserved-page allocator, not only an unlimited calloc mock.
python3 "$repo/tools/test-a733-runtime-pool.py"
