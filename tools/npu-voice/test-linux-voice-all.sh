#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)/test-linux-voice.sh"

usage()
{
  printf '%s\n' \
    'Usage: sudo bash test-linux-voice-all.sh --run --npu-idle-confirmed [--bundle-dir DIRECTORY]' \
    '       bash test-linux-voice-all.sh --env-only [--bundle-dir DIRECTORY]' \
    'One batch: malformed input rejection, warm/reload, real CPU-mel/NPU-vocoder WAVs,' \
    'six KWS fixtures x two scales x two state policies, greedy + legacy keyword rules.' \
    'No cloud, automatic playback/reset/reboot, live capture, ASR-NBG conversion or RTOS acceptance.'
}

validation_pipeline()
{
  command -v python3 >/dev/null || { printf 'Missing python3; no NPU calls made.\n' >&2; return 2; }
  phase=batch-fixture-check
  python3 "$base/run-voice-batch.py" check "$base" || return $?
  chmod u+x "$base/voice-repeat-linux-aarch64"
  phase=available-asset-batch
  hardware_started=1
  set +e
  python3 -u "$base/run-voice-batch.py" run "$base" "$work" --npu-idle-confirmed 2>&1 | tee "$work/batch-console.log"
  local states=("${PIPESTATUS[@]}")
  local status=${states[0]}
  ((status!=0)) || status=${states[1]}
  set -e
  ((status==0)) || { printf 'STOP: preserve complete report; no automatic hardware retry.\n' >&2; return "$status"; }
  [[ -s "$work/batch-summary.json" ]] || return 2
  result=COLLECTED_AVAILABLE_ASSET_BATCH
  phase=finished
}

if [[ ${BASH_SOURCE[0]} == "$0" ]]; then
  export PYTHONDONTWRITEBYTECODE=1
  scope_note='Available KWS/vocoder asset batch collected only. See per-test results and BLOCKED list; NOT full ASR/RTOS/live-audio acceptance.'
  main "$@"
fi
