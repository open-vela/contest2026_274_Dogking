#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/.." && pwd -P)
pet="$repo/board/a733-cubie-a7z/openvela-overlay/apps/system/aipet"
knf="$pet/third_party/kaldi-native-fbank/csrc"
tmp=$(mktemp -d /tmp/frontend-audit.XXXXXX)
trap 'rm -rf -- "$tmp"' EXIT
mkdir "$tmp/data"
g++ -std=c++17 -Os -g -Wall -Wextra -Werror -Wno-sign-compare -Wno-unused-parameter \
  -fsanitize=address,undefined -I"$knf/include" \
  "$repo/tools/test-frontend-audit.cxx" "$pet/voice_fbank.cxx" "$knf"/src/*.cc \
  -Wl,--wrap=open,--wrap=unlink,--wrap=write,--wrap=fsync,--wrap=close -lm -o "$tmp/test"
"$tmp/test" "$1" "$2" "$tmp/data"
