#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
pet="$repo/board/a733-cubie-a7z/openvela-overlay/apps/system/aipet"
knf="$pet/third_party/kaldi-native-fbank/csrc"
tmp=$(mktemp -d /tmp/aipet-fbank.XXXXXX)
trap 'rm -rf -- "$tmp"' EXIT
sources=("$pet/voice_fbank.cxx" "$knf"/src/*.cc)
for mode in sanitizers no-exceptions rtos-logger; do
  flags=(-fsanitize=address,undefined)
  [[ "$mode" != no-exceptions ]] || flags=(-fno-exceptions -fno-rtti)
  [[ "$mode" != rtos-logger ]] || flags=(-fno-exceptions -fno-rtti -DKNF_NO_IOSTREAM=1 -fsanitize=address,undefined)
  g++ -std=c++17 -O1 -g -Wall -Wextra -Werror -Wno-sign-compare -Wno-unused-parameter "${flags[@]}" -pthread \
    -I"$pet" -I"$knf/include" "$repo/tools/test-aipet-fbank.cxx" "${sources[@]}" -lm -o "$tmp/$mode"
  "$tmp/$mode"
done
