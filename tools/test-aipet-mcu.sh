#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
official=${OPENVELA_WORKSPACE:-$repo/../../quickly-openvela}
reference=${STM32_REFERENCE:-$repo/out/stm32c8t6mini-reference-20261023-proxy}
[[ $(git -C "$reference" rev-parse HEAD) == e83a9cac8a0e4a775a38910bdaf08c68bfdffc8c ]]
pet="$repo/board/a733-cubie-a7z/openvela-overlay/apps/system/aipet"
knf="$pet/third_party/kaldi-native-fbank/csrc"
cjson="$official/apps/netutils/cjson/cJSON"
tmp=$(mktemp -d /tmp/aipet-mcu.XXXXXX)
trap 'rm -rf -- "$tmp"' EXIT
gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I"$reference/User" -I"$reference/Protocol" -c "$reference/Protocol/link_proto.c" -o "$tmp/stm32.o"
g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I"$pet" -I"$reference/Protocol" "$repo/tools/test-mcu-protocol.cxx" \
  "$pet/mcu_protocol.cxx" "$tmp/stm32.o" -o "$tmp/protocol"
"$tmp/protocol"
gcc -fsanitize=address,undefined -I"$cjson" -c "$cjson/cJSON.c" -o "$tmp/cjson.o"
gcc -Wall -Wextra -Werror -fsanitize=address,undefined -I"$repo/tools/mcu-host" -I"$pet" \
  -c "$pet/mcu_tool_bridge.c" -o "$tmp/bridge.o"
g++ -std=c++17 -Wall -Wextra -Werror -Wno-sign-compare -Wno-unused-parameter \
  -fsanitize=address,undefined -pthread -DAIPET_MCU_TEST_SPAWN -DAIPET_AUDIO_DIR="\"$tmp/state\"" \
  -DAIPET_AUDIO_RECORD="\"$tmp/record.wav\"" -I"$repo/tools/mcu-host" -I"$pet" \
  -I"$pet/../a733display" -I"$knf/include" -I"$cjson" \
  -I"$repo/board/a733-cubie-a7z/openvela-overlay/apps/include" -I"$official/packages/ai_agent/include" \
  "$repo/tools/test-aipet-mcu.cxx" "$pet/mcu_protocol.cxx" "$pet/mcu_link.cxx" \
  "$pet/mcu_agent_tool.cxx" "$pet/pet_routes.cxx" "$pet/pet_core.cxx" \
  "$pet/audio_app.cxx" "$pet/audio_resample.cxx" "$pet/voice_fbank.cxx" "$knf"/src/*.cc \
  "$pet/voice_local.cxx" "$pet/voice_play_stream.cxx" "$pet/audio_resample_stream.cxx" \
  "$pet/voice_phrase_pipe.cxx" "$pet/audio_capture_live.cxx" \
  "$pet/speech_output.cxx" "$pet/uart_tts.cxx" "$tmp/cjson.o" "$tmp/bridge.o" -Wl,--wrap=open -lutil -lm -o "$tmp/pipeline"
"$tmp/pipeline"
bash "$repo/tools/check-openvela-first.sh"
