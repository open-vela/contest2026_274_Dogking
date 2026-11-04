#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
cd "$(dirname "$0")/.."
pet=board/a733-cubie-a7z/openvela-overlay/apps/system/aipet
out=${1:-out/voice-stream-v181-r1}
[[ "$out" =~ ^out/[a-zA-Z0-9_-]+(/[a-zA-Z0-9_-]+)*$ && ! -e "$out" ]];mkdir -p "$out"
flags=(-std=c++17 -O2 -Wall -Wextra -Werror -I"$pet")
g++ "${flags[@]}" -fsanitize=address,undefined tools/test-audio-stream-resample.cxx \
  "$pet/audio_resample.cxx" "$pet/audio_resample_stream.cxx" -o "$out/test-resample"
"$out/test-resample"
g++ "${flags[@]}" -fsanitize=address,undefined -pthread \
  -Iboard/a733-cubie-a7z/openvela-overlay/apps/include tools/test-voice-play-stream.cxx \
  "$pet/voice_play_stream.cxx" "$pet/audio_resample_stream.cxx" "$pet/voice_local.cxx" \
  "$pet/pet_core.cxx" -Wl,--wrap=pthread_join -o "$out/test-play-stream"
"$out/test-play-stream"
g++ "${flags[@]}" -fsanitize=address,undefined -pthread tools/test-voice-phrase-pipe.cxx \
  "$pet/voice_phrase_pipe.cxx" -o "$out/test-phrase"
"$out/test-phrase"
g++ "${flags[@]}" -fsanitize=address,undefined tools/test-tts-frontend.cxx \
  "$pet/voice_tts_frontend.cxx" -lm -o "$out/test-tts"
"$out/test-tts" ../references/matcha-icefall-zh-baker/tokens.txt ../references/matcha-icefall-zh-baker/lexicon.txt
g++ "${flags[@]}" tools/npu-voice/tts-frontend-host.cxx \
  "$pet/voice_tts_frontend.cxx" -lm -o "$out/tts-host"
gcc -std=c11 -O2 -Wall -Wextra -Werror -I"$pet" -c "$pet/voice_asr_decode.c" -o "$out/decode.o"
knf="$pet/third_party/kaldi-native-fbank/csrc"
gcc -std=c11 -O2 -fsanitize=address,undefined -I"$pet" -c "$pet/voice_asr_decode.c" -o "$out/decode-sanitized.o"
g++ "${flags[@]}" -fsanitize=address,undefined -Wno-sign-compare -Wno-unused-parameter -I"$knf/include" \
  tools/test-asr-stream.cxx "$pet/voice_asr_stream.cxx" "$pet/voice_fbank.cxx" "$knf"/src/*.cc \
  "$out/decode-sanitized.o" -lm -o "$out/test-asr-stream"
"$out/test-asr-stream"
g++ "${flags[@]}" -Wno-sign-compare -Wno-unused-parameter -I"$knf/include" \
  tools/npu-voice/asr-stream-host.cxx "$pet/voice_asr_stream.cxx" "$pet/voice_fbank.cxx" \
  "$knf"/src/*.cc "$out/decode.o" -lm -o "$out/asr-stream-host"
echo 'PASS: streaming resampler compiled/tested; ASR stream oracle bridge built, not target provider'
