#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
team=$(cd "$(dirname "$0")/.." && pwd)
official=${OPENVELA_WORKSPACE:-$team/../../quickly-openvela}
stage=$(mktemp -d /tmp/a733-home-test.XXXXXX)
trap 'rm -rf -- "$stage"' EXIT
gcc -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror \
  -fsanitize=address,undefined -I"$team/app/a733home" \
  "$team/app/a733home/home_protocol.c" "$team/app/a733home/test_home_protocol.c" -o "$stage/protocol"
"$stage/protocol"
gcc -std=c11 -D_POSIX_C_SOURCE=200809L -DHOME_STEP_TIMEOUT_MS=50 \
  -Dclock_gettime=home_test_clock_gettime -Dnanosleep=home_test_nanosleep \
  -Wall -Wextra -Werror -pthread -fsanitize=address,undefined \
  -I"$team/tools/home-client-mocks" \
  -I"$official/frameworks/connectivity/bluetooth/framework/include" \
  "$team/tools/test-home-client.c" "$team/app/a733home/home_protocol.c" -o "$stage/client"
"$stage/client" > "$stage/client.log" 2>&1 || { cat "$stage/client.log"; exit 1; }
tail -n 1 "$stage/client.log"
bash "$team/tools/check-openvela-first.sh"
if [[ -n "${COMPARE_PATCHED_WORKSPACE:-}" ]]; then
  for name in a733home_main.c home_protocol.h home_protocol.c CMakeLists.txt Kconfig Make.defs Makefile; do
    cmp "$team/app/a733home/$name" "$COMPARE_PATCHED_WORKSPACE/apps/system/a733home/$name"
  done
  echo 'home-client sources match staged build: PASS'
fi
