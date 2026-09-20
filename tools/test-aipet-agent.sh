#!/usr/bin/env bash
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
official=$(cd "$repo/../../quickly-openvela" && pwd)
tmp=$(mktemp -d)
trap 'rm -f "$tmp/test" "$tmp/main.o"; rmdir "$tmp"' EXIT
pet="$repo/board/a733-cubie-a7z/openvela-overlay/apps/system/aipet"
gcc -D_DEFAULT_SOURCE -DOK=0 -DERROR=-1 -Wall -Wextra -Werror \
  -fsanitize=address,undefined -pthread \
  -I"$official/packages/ai_agent/src" -I"$official/packages/ai_agent/include" \
  -I"$pet" "$repo/tools/test-aipet-agent.c" "$pet/agent_bridge.c" \
  "$official/packages/ai_agent/src/core/message_bus_tap.c" -o "$tmp/test"
"$tmp/test"
g++ -std=c++17 -Wall -Wextra -Werror -I"$pet" \
  -c "$pet/aipet_main.cxx" -o "$tmp/main.o"
echo "official-Agent pet boundary tests passed (not a cloud/hardware test)"
