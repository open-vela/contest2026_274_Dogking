#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/.." && pwd -P)
pet="$repo/board/a733-cubie-a7z/openvela-overlay/apps/system/aipet"
knf="$pet/third_party/kaldi-native-fbank/csrc"
tmp=$(mktemp -d /tmp/kws-wave.XXXXXX)
trap 'rm -rf -- "$tmp"' EXIT
g++ -std=c++17 -O1 -g -Wall -Wextra -Werror -Wno-sign-compare -Wno-unused-parameter -fsanitize=address,undefined \
  -I"$repo/board/a733-cubie-a7z/openvela-overlay/apps/include" -I"$knf/include" \
  "$repo/tools/test-kws-wave.cxx" "$pet/voice_fbank.cxx" "$knf"/src/*.cc -lm -o "$tmp/test"
"$tmp/test" "$1" "$2"
