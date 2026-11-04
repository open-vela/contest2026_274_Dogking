#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Host manifest/CRC/bounds check only. No NPU device or Linux replay enabled.
set -euo pipefail
cd "$(dirname "$0")/.."
package=${1:?new prepared package directory}
out=${2:?new validation output directory}
[[ "$out" =~ ^out/[a-zA-Z0-9_-]+$ && ! -e "$out" ]]
mkdir "$out"
chip=board/a733-cubie-a7z/openvela-overlay/vendor/allwinnertech/chips/a733
gcc -std=c11 -O2 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -DCONFIG_A733_NPU_GENERAL_ASR=1 -I"$chip/include" \
  -Iboard/a733-cubie-a7z/openvela-overlay/nuttx/include \
  tools/a733-tensor-check.c "$chip/a733_tensor_plan.c" -o "$out/tensor-check"
for role in encoder decoder joiner; do
  "$out/tensor-check" "$package/asr-$role.a7tv" > "$out/$role.log" 2>&1
  cat "$out/$role.log"
done
echo 'PASS: all native ASR packages validated by actual RTOS plan code (host only)'
