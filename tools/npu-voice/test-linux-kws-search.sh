#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)/test-linux-voice.sh"
usage()
{
  printf '%s\n' \
    'Usage: sudo bash test-linux-kws-search.sh --run --npu-idle-confirmed' \
    '       bash test-linux-kws-search.sh --env-only' \
    'Six frozen encoder cases; bounded C search; ONLY missing decoder/joiner branches run NPU.' \
    'No live capture, cloud, ASR, reset/reboot, firmware changes or end-to-end claim.'
}
validation_pipeline()
{
  command -v python3 >/dev/null || return 2
  phase=search-fixture-check
  python3 "$base/run-kws-search.py" check "$base" || return $?
  phase=search-native-branches
  hardware_started=1
  set +e
  python3 -u "$base/run-kws-search.py" run "$base" "$work" --npu-idle-confirmed 2>&1 | tee "$work/search-console.log"
  local states=("${PIPESTATUS[@]}")
  local status=${states[0]}
  ((status!=0)) || status=${states[1]}
  set -e
  ((status==0)) || return "$status"
  [[ -s "$work/search-summary.json" ]] || return 2
  result=COLLECTED_SEARCH_REGRESSION
  phase=finished
}
if [[ ${BASH_SOURCE[0]} == "$0" ]]; then
  export PYTHONDONTWRITEBYTECODE=1
  scope_note='Experimental C keyword search on frozen Linux NPU encoder outputs; native branch origins explicit. Not ASR/openvela/live-audio acceptance.'
  main "$@"
fi
