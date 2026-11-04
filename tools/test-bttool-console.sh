#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
team=$(cd "$(dirname "$0")/.." && pwd)
official=${OPENVELA_WORKSPACE:-$team/../../quickly-openvela}
test_dir=$(mktemp -d /tmp/a733-bttool-test.XXXXXX)
trap 'rm -rf -- "$test_dir"' EXIT
for relative in \
  frameworks/connectivity/bluetooth/tools/bt_tools.c \
  frameworks/connectivity/bluetooth/tools/scan.c \
  frameworks/connectivity/bluetooth/service/src/adapter_state.c \
  frameworks/connectivity/bluetooth/service/stacks/zephyr/sal_adapter_le_interface.c; do
  mkdir -p "$test_dir/$(dirname "$relative")"
  cp "$official/$relative" "$test_dir/$relative"
done
patch --batch --reject-file=- -p1 -d "$test_dir" \
  < "$team/patches/bluetooth-cli-init-recovery.patch"
patch --batch --reject-file=- -p1 -d "$test_dir" \
  < "$team/patches/bluetooth-ble-lifecycle.patch"
awk '/^static ssize_t bttool_read_command/{copy=1} copy{print} copy && /^}/{exit}' \
  "$test_dir/frameworks/connectivity/bluetooth/tools/bt_tools.c" \
  > "$test_dir/bttool-console-extracted.h"
gcc -D__NuttX__ -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I"$test_dir" "$team/tools/test-bttool-console.c" -o "$test_dir/console-test"
"$test_dir/console-test"
awk '/^static void bttool_execute_command_cb/{copy=1} copy{print} copy && /^}/{exit}' \
  "$test_dir/frameworks/connectivity/bluetooth/tools/bt_tools.c" \
  > "$test_dir/bttool-parser-extracted.h"
gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I"$test_dir" "$team/tools/test-bttool-parser.c" -o "$test_dir/parser-test"
"$test_dir/parser-test"
if [[ -n "${COMPARE_PATCHED_WORKSPACE:-}" ]]; then
  for relative in \
    frameworks/connectivity/bluetooth/tools/bt_tools.c \
    frameworks/connectivity/bluetooth/tools/scan.c \
    frameworks/connectivity/bluetooth/service/src/adapter_state.c \
    frameworks/connectivity/bluetooth/service/stacks/zephyr/sal_adapter_le_interface.c; do
    # Normalize only the final missing LF; all line content remains exact.
    cmp <(awk '{print}' "$test_dir/$relative") \
        <(awk '{print}' "$COMPARE_PATCHED_WORKSPACE/$relative")
  done
  echo "live CLI/SAL/state sources match patch (terminal LF normalized): PASS"
fi
