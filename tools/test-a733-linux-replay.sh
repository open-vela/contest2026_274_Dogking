#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/.." && pwd -P)
tmp=$(mktemp -d /tmp/a733-replay-test.XXXXXX)
trap 'rm -rf -- "$tmp"' EXIT
inc="$repo/board/a733-cubie-a7z/openvela-overlay"
gcc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I"$inc/nuttx/include" -I"$inc/vendor/allwinnertech/chips/a733/include" \
  "$repo/tools/test-a733-linux-replay.c" "$inc/vendor/allwinnertech/chips/a733/a733_tensor_plan.c" \
  -Wl,--wrap=ioctl -Wl,--wrap=open -Wl,--wrap=close -o "$tmp/test"
"$tmp/test" "$tmp"
