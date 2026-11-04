#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/.." && pwd -P)
pet="$repo/board/a733-cubie-a7z/openvela-overlay/apps/system/aipet"
tmp=$(mktemp -d /tmp/aipet-kws.XXXXXX)
trap 'rm -rf -- "$tmp"' EXIT
gcc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I"$pet" "$repo/tools/test-aipet-kws.c" "$pet/voice_kws.c" -lm -o "$tmp/test"
"$tmp/test"
# Same C source can be used from a C++ application without ABI name mangling.
g++ -std=c++17 -Wall -Wextra -Werror -I"$pet" -x c++ -fsyntax-only "$pet/voice_kws.h"
