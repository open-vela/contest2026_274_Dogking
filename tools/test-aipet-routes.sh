#!/usr/bin/env bash
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
pet="$repo/board/a733-cubie-a7z/openvela-overlay/apps/system/aipet"
if [[ -d "$repo/../apps/netutils/cjson/cJSON" ]]; then
  cjson="$repo/../apps/netutils/cjson/cJSON"
else
  cjson="$repo/../../quickly-openvela/apps/netutils/cjson/cJSON"
fi
tmp=$(mktemp -d)
trap 'rm -f "$tmp/routes" "$tmp/core" "$tmp/cjson.o"; rmdir "$tmp"' EXIT
gcc -fsanitize=address,undefined -I"$cjson" -c "$cjson/cJSON.c" -o "$tmp/cjson.o"
g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I"$pet" -I"$cjson" "$repo/tools/test-aipet-routes.cxx" \
  "$pet/pet_routes.cxx" "$pet/pet_core.cxx" "$pet/speech_output.cxx" \
  "$pet/uart_tts.cxx" "$tmp/cjson.o" -o "$tmp/routes"
"$tmp/routes" "$repo/tools/routes-test-valid.json" "$repo/tools/routes-test-invalid.json"
g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
  "$repo/tools/test-aipet-core.cxx" "$pet/pet_core.cxx" -o "$tmp/core"
"$tmp/core"
