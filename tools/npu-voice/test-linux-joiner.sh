#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Stage 2: bounded synthetic input-sensitivity checks, never speech acceptance.
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)/test-linux-voice.sh"

usage()
{
  printf '%s\n' \
    'Usage: sudo bash test-linux-joiner.sh --run --npu-idle-confirmed [--bundle-dir DIRECTORY]' \
    '       bash test-linux-joiner.sh --env-only [--bundle-dir DIRECTORY]' \
    'Requires python3; uses only the already-tested KWS joiner.' \
    'Six runs: zero, encoder-only, decoder-only, both, repeat encoder-only, repeat zero.' \
    'No microphone, real speech, state feedback, TTS or openvela acceptance.'
}

validation_pipeline()
{
  scope_note='Synthetic joiner input sensitivity/repeatability only. NOT KWS/ASR/TTS accuracy or openvela acceptance.'
  phase=fixture-preparation
  command -v python3 >/dev/null || { printf 'Missing python3; no NPU calls made.\n' >&2; return 2; }
  python3 "$base/joiner-fixtures.py" prepare "$work" || return $?
  inspect_model joiner-info models/kws/joiner_float_a733.nb 2 1 || return $?
  python3 "$base/joiner-fixtures.py" contract "$work/joiner-info.log" || return $?
  local tag case_name
  for tag in zero-a encoder-a decoder-a both-a encoder-b zero-b; do
    case "$tag" in
      zero-*) case_name=zero ;;
      encoder-*) case_name=encoder ;;
      decoder-*) case_name=decoder ;;
      both-*) case_name=both ;;
    esac
    # Re-check immutable fixtures and every reported input/output CRC per run.
    python3 "$base/joiner-fixtures.py" fixtures "$work" || return $?
    run_stage "$tag" "$base/models/kws/joiner_float_a733.nb" \
      "$work/fixtures/$case_name" "$work/$tag" || return $?
    python3 "$base/joiner-fixtures.py" output "$work" "$tag" || return $?
  done
  phase=compare
  python3 "$base/joiner-fixtures.py" compare "$work" > "$work/dynamic-summary.json" || return $?
  cat "$work/dynamic-summary.json"
  result=PASS_SYNTHETIC_JOINER_ONLY
  phase=finished
}

if [[ ${BASH_SOURCE[0]} == "$0" ]]; then
  scope_note='Synthetic joiner checks only. NOT KWS/ASR/TTS accuracy or openvela acceptance.'
  main "$@"
fi
