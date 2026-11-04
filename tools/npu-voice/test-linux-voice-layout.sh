#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)/test-linux-voice.sh"
usage()
{
  printf '%s\n' \
    'Usage: sudo bash test-linux-voice-layout.sh --run --npu-idle-confirmed' \
    '       bash test-linux-voice-layout.sh --env-only' \
    'Exactly four fixed native model runs; captures actual prepare/command/buffer layout.' \
    'No keyword matrix, live capture, network, converter install, reset/reboot or RTOS claim.'
}
validation_pipeline()
{
  command -v python3 >/dev/null || return 2
  phase=layout-fixture-check
  python3 "$base/capture-voice-layout.py" check "$base" || return $?
  chmod u+x "$base/voice-layout-linux-aarch64"
  phase=native-layout-capture
  hardware_started=1
  set +e
  python3 -u "$base/capture-voice-layout.py" run "$base" "$work" --npu-idle-confirmed 2>&1 | tee "$work/layout-console.log"
  local states=("${PIPESTATUS[@]}")
  local status=${states[0]}
  ((status!=0)) || status=${states[1]}
  set -e
  ((status==0)) || return "$status"
  [[ -s "$work/layout-summary.json" ]] || return 2
  result=NATIVE_GOLDENS_AND_LAYOUT_CAPTURED
  phase=finished
}
if [[ ${BASH_SOURCE[0]} == "$0" ]]; then
  export PYTHONDONTWRITEBYTECODE=1
  scope_note='Native model outputs and prepared layout capture only; no A7TV export, ASR conversion or openvela hardware execution.'
  main "$@"
fi
