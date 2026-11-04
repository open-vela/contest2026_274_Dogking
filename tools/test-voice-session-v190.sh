#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
cd "$(dirname "$0")/.."
out=${1:?new out directory}
[[ "$out" =~ ^out/[a-zA-Z0-9_-]+(/[a-zA-Z0-9_-]+)*$ && ! -e "$out" ]];mkdir -p "$out"
pet=board/a733-cubie-a7z/openvela-overlay/apps/system/aipet
flags=(-std=c++17 -O2 -Wall -Wextra -Werror -fsanitize=address,undefined -pthread -I"$pet" -Iboard/a733-cubie-a7z/openvela-overlay/apps/include)
g++ "${flags[@]}" tools/test-voice-compute-v190.cxx -o "$out/compute"
"$out/compute" > "$out/compute.log" 2>&1
g++ "${flags[@]}" -DCONFIG_A733_CPU_ACOUSTIC=1 \
  -Iboard/a733-cubie-a7z/openvela-overlay/apps/include tools/test-voice-session-v190.cxx \
  "$pet/voice_general_provider.cxx" -o "$out/session"
"$out/session" > "$out/session.log" 2>&1
cat "$out/compute.log" "$out/session.log"
