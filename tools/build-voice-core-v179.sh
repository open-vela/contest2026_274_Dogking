#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
cd "$(dirname "$0")/.."
pet=board/a733-cubie-a7z/openvela-overlay/apps/system/aipet
out=out/voice-core-v179
mkdir -p "$out"
flags=(-g -Wall -Wextra -Werror -I"$pet")
gcc -std=c11 "${flags[@]}" -fsanitize=address,undefined \
  tools/test-asr-decode.c "$pet/voice_asr_decode.c" -lm -o "$out/test-asr"
"$out/test-asr"
g++ -std=c++17 "${flags[@]}" -fsanitize=address,undefined \
  tools/test-tts-frontend.cxx "$pet/voice_tts_frontend.cxx" -lm -o "$out/test-tts"
model=../references/matcha-icefall-zh-baker
"$out/test-tts" "$model/tokens.txt" "$model/lexicon.txt"
g++ -std=c++17 "${flags[@]}" -fsanitize=address,undefined -pthread \
  tools/test-voice-local.cxx "$pet/voice_local.cxx" "$pet/pet_core.cxx" \
  -Wl,--wrap=clock_gettime -o "$out/test-local"
"$out/test-local"
gcc -std=c11 -O2 "${flags[@]}" tools/npu-voice/asr-decoder-host.c \
  "$pet/voice_asr_decode.c" -lm -o "$out/asr-host"
g++ -std=c++17 -O2 "${flags[@]}" tools/npu-voice/tts-frontend-host.cxx \
  "$pet/voice_tts_frontend.cxx" -lm -o "$out/tts-host"
knf="$pet/third_party/kaldi-native-fbank/csrc"
g++ -std=c++17 -O2 "${flags[@]}" -Wno-sign-compare -Wno-unused-parameter \
  -I"$knf/include" tools/npu-voice/export-fbank.cxx "$pet/voice_fbank.cxx" \
  "$knf"/src/*.cc -lm -o "$out/export-fbank"
echo 'Host ASR/TTS components built. No board, NPU or full speech-chain acceptance.'
