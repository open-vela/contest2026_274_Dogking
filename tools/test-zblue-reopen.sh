#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
team=$(cd "$(dirname "$0")/.." && pwd)
official=${OPENVELA_WORKSPACE:-$team/../../quickly-openvela}
stage=$(mktemp -d /tmp/a733-zblue-reopen-test.XXXXXX)
trap 'rm -rf -- "$stage"' EXIT
paths=(external/zblue/zblue/subsys/bluetooth/host/hci_core.c
       external/zblue/zblue/subsys/bluetooth/host/scan.c
       frameworks/connectivity/bluetooth/service/stacks/zephyr/hci_h4.c
       frameworks/connectivity/bluetooth/service/stacks/zephyr/sal_le_scan_interface.c)
for path in "${paths[@]}"; do
  mkdir -p "$stage/$(dirname "$path")"
  cp "$official/$path" "$stage/$path"
done
patch --batch --reject-file=- -p1 -d "$stage" < "$team/patches/bluetooth-zblue-reopen.patch"
extract()
{
  awk -v name="$2" 'index($0,name) && $0 !~ /;$/ && $0 ~ /^(static |void |bt_status_t )/ {copy=1} copy{print} copy && /^}/{exit}' "$1"
}
extract "$stage/${paths[0]}" ' bt_dev_quiesce(' > "$stage/quiesce.h"
extract "$stage/${paths[1]}" ' bt_scan_reset(' > "$stage/scan-reset.h"
extract "$stage/${paths[2]}" ' h4_send_data(' > "$stage/h4-write.h"
extract "$stage/${paths[2]}" ' h4_send(' > "$stage/h4-send.h"
extract "$stage/${paths[3]}" ' STACK_CALL(start_scan)' > "$stage/sal-start.h"
extract "$stage/${paths[3]}" ' bt_sal_le_start_scan(' > "$stage/sal-enqueue.h"
gcc -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -pthread \
  -fsanitize=address,undefined -I"$stage" "$team/tools/test-zblue-reopen.c" -o "$stage/test"
"$stage/test"
if [[ -n "${COMPARE_PATCHED_WORKSPACE:-}" ]]; then
  for path in "${paths[@]}"; do
    cmp <(awk '{print}' "$stage/$path") <(awk '{print}' "$COMPARE_PATCHED_WORKSPACE/$path")
  done
  echo 'Zblue reopen sources match staged build: PASS'
fi
