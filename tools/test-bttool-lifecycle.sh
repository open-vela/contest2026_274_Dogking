#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
team=$(cd "$(dirname "$0")/.." && pwd)
official=${OPENVELA_WORKSPACE:-$team/../../quickly-openvela}
stage=$(mktemp -d /tmp/a733-lifecycle-test.XXXXXX)
trap 'rm -rf -- "$stage"' EXIT
for relative in \
  frameworks/connectivity/bluetooth/tools/bt_tools.c \
  frameworks/connectivity/bluetooth/tools/scan.c \
  frameworks/connectivity/bluetooth/service/src/adapter_state.c \
  frameworks/connectivity/bluetooth/service/stacks/zephyr/sal_adapter_le_interface.c; do
  mkdir -p "$stage/$(dirname "$relative")"
  cp "$official/$relative" "$stage/$relative"
done
patch --batch --reject-file=- -p1 -d "$stage" < "$team/patches/bluetooth-cli-init-recovery.patch"
patch --batch --reject-file=- -p1 -d "$stage" < "$team/patches/bluetooth-ble-lifecycle.patch"
extract()
{
  awk -v name="$2" 'index($0, name) && $0 !~ /;$/ && $0 ~ /^(static |bt_status_t )/ {copy=1} copy{print} copy && /^}/{exit}' "$1"
}
tool="$stage/frameworks/connectivity/bluetooth/tools/bt_tools.c"
state="$stage/frameworks/connectivity/bluetooth/service/src/adapter_state.c"
sal="$stage/frameworks/connectivity/bluetooth/service/stacks/zephyr/sal_adapter_le_interface.c"
for func in bttool_adapter_ready bt_tool_init bt_tool_uninit enable_cmd disable_cmd on_adapter_state_changed_cb bttool_ins_init bttool_ins_uninit; do
  extract "$tool" " $func(" >> "$stage/tool-extracted.h"
done
extract "$state" ' ble_on_process_event(' > "$stage/state-extracted.h"
extract "$sal" ' STACK_CALL(le_disable)' > "$stage/sal-extracted.h"
extract "$sal" ' bt_sal_le_disable(' >> "$stage/sal-extracted.h"
cp "$stage/frameworks/connectivity/bluetooth/tools/scan.c" "$stage/scan-source.h"
for header in advertiser_data.h bt_le_scan.h bt_tools.h bt_adapter.h; do
  printf '#include "scan-mocks.h"\n' > "$stage/$header"
done
cp "$team/tools/test-bttool-scan-mocks.h" "$stage/scan-mocks.h"
for test in scan lifecycle; do
  gcc -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -Wno-unused-parameter -Wno-missing-field-initializers \
    -pthread -fsanitize=address,undefined -I"$stage" "$team/tools/test-bttool-$test.c" -o "$stage/$test-test"
  "$stage/$test-test"
done
if [[ -n "${COMPARE_PATCHED_WORKSPACE:-}" ]]; then
  for relative in frameworks/connectivity/bluetooth/tools/bt_tools.c frameworks/connectivity/bluetooth/tools/scan.c \
    frameworks/connectivity/bluetooth/service/src/adapter_state.c frameworks/connectivity/bluetooth/service/stacks/zephyr/sal_adapter_le_interface.c; do
    cmp <(awk '{print}' "$stage/$relative") <(awk '{print}' "$COMPARE_PATCHED_WORKSPACE/$relative")
  done
  echo 'lifecycle sources match staged build: PASS'
fi
