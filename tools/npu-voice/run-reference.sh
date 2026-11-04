#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Explicit Linux hardware reference; never called by boot/build/Agent.
set -euo pipefail
if [[ $(uname -s) != Linux || $(uname -m) != aarch64 ]]; then
  echo 'Requires the physical A733 official Linux environment, not x86/WSL.' >&2
  exit 2
fi
if [[ ! -c /dev/vipcore ]]; then
  echo 'Official Linux /dev/vipcore missing; no hardware reset attempted.' >&2
  exit 2
fi
if (($# < 2 || $# > 3)); then
  echo 'Usage: run-reference.sh MODEL.nb --inspect' >&2
  echo '       run-reference.sh MODEL.nb INPUT_DIR|--zero NEW_OUTPUT_DIR' >&2
  exit 2
fi
here=$(cd "$(dirname "$0")" && pwd)
binary=${A733_VOICE_GOLDEN:-$here/../../out/npu-voice-bullseye/voice-golden-linux-aarch64}
[[ -x "$binary" ]] || { echo "Missing executable: $binary" >&2; exit 2; }
command -v timeout >/dev/null
echo 'Stop other NPU applications first. Golden output contains model/tensor data.'
set +e
timeout --signal=TERM --kill-after=5s 60s "$binary" "$@"
result=$?
set -e
if ((result == 124 || result == 137)); then
  echo 'Reference deadline exceeded. Preserve logs/dmesg and cold reboot before reuse.' >&2
  echo 'Do not infer that killing userspace has reset the NPU.' >&2
fi
exit "$result"
