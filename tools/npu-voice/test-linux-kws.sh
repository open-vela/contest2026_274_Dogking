#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)/test-linux-voice.sh"

usage()
{
  printf '%s\n' \
    'Usage: sudo bash test-linux-kws.sh --run --npu-idle-confirmed [--bundle-dir DIRECTORY]' \
    '       bash test-linux-kws.sh --env-only [--bundle-dir DIRECTORY]' \
    'Real reference WAV/silence: CPU-precomputed fbank -> stateful KWS encoder -> decoder -> joiner.' \
    'Greedy phoneme diagnostics, NOT official keyword beam, Chinese ASR, live capture or openvela acceptance.' \
    'State scalar policy: feature-offset, matching reference default. Every state/output is retained.'
}

validation_pipeline()
{
  command -v python3 >/dev/null || { printf 'Missing python3; no NPU calls made.\n' >&2; return 2; }
  phase=stream-fixture-check
  python3 "$base/stream-kws.py" check "$base" || return $?
  phase=real-kws-diagnostic
  hardware_started=1
  set +e
  python3 -u "$base/stream-kws.py" run "$base" "$work" \
    --lens-policy feature-offset --npu-idle-confirmed 2>&1 | tee "$work/stream-console.log"
  local states=("${PIPESTATUS[@]}")
  local status=${states[0]}
  ((status!=0)) || status=${states[1]}
  set -e
  ((status==0)) || { printf 'STOP; preserve logs, no automatic retries.\n' >&2; return "$status"; }
  [[ -s "$work/stream-summary.json" ]] || return 2
  cat "$work/stream-summary.json"
  result=COLLECTED_REAL_KWS_GREEDY_DIAGNOSTIC
  phase=finished
}

if [[ ${BASH_SOURCE[0]} == "$0" ]]; then
  export PYTHONDONTWRITEBYTECODE=1
  scope_note='Real-fixture KWS state/greedy diagnostic only. NOT keyword accuracy, Chinese ASR, live capture, TTS or openvela acceptance.'
  main "$@"
fi
