#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
official=${OPENVELA_WORKSPACE:-$repo/../../quickly-openvela}
overlay="$repo/board/a733-cubie-a7z/openvela-overlay"
pet="$overlay/apps/system/aipet"
knf="$pet/third_party/kaldi-native-fbank/csrc"
mb="$official/apps/crypto/mbedtls/mbedtls"
tmp=$(mktemp -d /tmp/aipet-audio-test.XXXXXX)
trap 'rm -rf -- "$tmp"' EXIT
g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -pthread \
  -DAIPET_AUDIO_DIR="\"$tmp/state\"" -DAIPET_AUDIO_RECORD="\"$tmp/record.wav\"" \
  -I"$pet" -I"$knf/include" -I"$overlay/apps/include" -I"$official/packages/ai_agent/include" \
  -Wno-sign-compare -Wno-unused-parameter \
  "$repo/tools/test-aipet-audio.cxx" "$pet/audio_app.cxx" "$pet/audio_resample.cxx" "$pet/pet_core.cxx" \
  "$pet/voice_local.cxx" "$pet/voice_play_stream.cxx" "$pet/audio_resample_stream.cxx" "$pet/audio_capture_live.cxx" \
  "$pet/voice_fbank.cxx" "$knf"/src/*.cc \
  "$pet/speech_output.cxx" "$pet/uart_tts.cxx" -Wl,--wrap=open -lm -o "$tmp/audio"
"$tmp/audio"
g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I"$pet" "$repo/tools/test-aipet-resample.cxx" "$pet/audio_resample.cxx" -lm -o "$tmp/resample"
"$tmp/resample"
gcc -D_DEFAULT_SOURCE -Wall -Wextra -Werror -fsanitize=address,undefined \
  -DMBEDTLS_CONFIG_FILE='"aipet-test-mbedtls.h"' -I"$repo/tools" -I"$mb/include" \
  -I"$official/apps/netutils/cjson/cJSON" -I"$overlay/packages/ai_agent/src/voice" \
  "$repo/tools/test-aipet-tts-complete.c" "$official/apps/netutils/cjson/cJSON/cJSON.c" \
  "$mb/library/base64.c" "$mb/library/constant_time.c" -lm -o "$tmp/tts"
"$tmp/tts"
g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -pthread \
  "$repo/tools/test-aipet-speech.cxx" "$pet/speech_output.cxx" "$pet/uart_tts.cxx" -o "$tmp/speech"
"$tmp/speech"
bash "$repo/tools/check-openvela-first.sh"
