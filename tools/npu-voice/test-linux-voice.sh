#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Linux A733 reference checks only. No installation, network, reset or reboot.
# Models/libraries in the private local kit are not redistributed in Git.

usage()
{
  printf '%s\n' \
    'Usage: bash test-linux-voice.sh --env-only [--bundle-dir DIRECTORY]' \
    '       sudo bash test-linux-voice.sh --run --npu-idle-confirmed [--bundle-dir DIRECTORY]' \
    'Stop all other NPU applications before --run. Never run this in NSH.' \
    'Checks 4 model interfaces, then runs only the small KWS joiner twice with zero input.' \
    'This is NOT microphone/ASR/TTS accuracy or openvela acceptance.'
}

verify_assets()
{
  local path expected
  while read -r expected path; do
    [[ -f "$base/$path" && ! -L "$base/$path" ]] || {
      printf 'Missing/nonregular/symlink asset: %s\n' "$path" >&2; return 2;
    }
    printf '%s  %s\n' "$expected" "$path"
  done < <(asset_lock) > "$work/expected.sha256"
  # Check from the bundle root; preserve all original files on mismatch.
  (cd "$base" && sha256sum --check "$work/expected.sha256") \
    > "$work/asset-verification.log" 2>&1 || {
      cat "$work/asset-verification.log" >&2; return 2;
    }
}

asset_lock()
{
  printf '%s\n' \
    'f283a60e59e41fccff5adfc53865fd38e21d6ab501e8f63b185dec00d81789f0 voice-golden-linux-aarch64' \
    '82f049b0ed0065dd4d443e37eeb1edfcbef24457c9c24e36170d64d5b748ca66 lib/libNBGlinker.so' \
    '3ed5357b26bd6c4fb68fbdc0b21d637227dda711d1fe67b9e28accdc48bb11f2 lib/libVIPhal.so' \
    'c89747b37baca00ad8bf70126b3bd6b7b36def0875a6bdb33d95bcb8f65707c0 models/kws/encoder_float_a733.nb' \
    '034f5266ccf5b62cc507593c0c4fec708073365282fc76b15521ee424f3035cc models/kws/decoder_float_a733.nb' \
    'f51cb0ceca61a44ee6bc8618d3ebbac788e7c3744a210c5c24cac9c6ffa4bf5e models/kws/joiner_float_a733.nb' \
    '00121f4ccb6d50833ccddc92d14da717e66e2f044c05316924cbf215ecf3403a models/vocoder/vocoder_int16_a733.nb'
}

environment_log()
{
  {
    date -u -Is
    uname -a
    [[ ! -f /etc/os-release ]] || cat /etc/os-release
    ls -l /dev/vipcore || true
    command -v free >/dev/null && free -h || true
    command -v df >/dev/null && df -h "$base" || true
    command -v ip >/dev/null && ip -4 addr || true
    command -v ps >/dev/null && ps -eo pid,comm || true
    local temp
    for temp in /sys/class/thermal/thermal_zone*/temp; do
      [[ -r "$temp" ]] || continue
      printf '%s=' "$temp"; cat "$temp" || true
    done
  } > "$work/environment.log" 2>&1
}

preflight()
{
  [[ $(uname -s) == Linux && $(uname -m) == aarch64 ]] || {
    printf 'Requires physical A733 Linux/aarch64, not x86/WSL/NSH.\n' >&2; return 2;
  }
  [[ -c /dev/vipcore ]] || {
    printf '/dev/vipcore missing. No driver installation/reset attempted.\n' >&2; return 2;
  }
  [[ -r /dev/vipcore && -w /dev/vipcore ]] || {
    printf 'Device access denied; run via sudo. Device permissions are never changed.\n' >&2; return 2;
  }
  local command
  for command in sha256sum timeout ldd grep cmp tee flock; do
    command -v "$command" >/dev/null || { printf 'Missing command: %s\n' "$command" >&2; return 2; }
  done
  # Lock this kit, without creating/truncating an unknown lock file. This is
  # not a global NPU lock; manual idle confirmation is still required.
  exec 9< "$base"
  flock -n 9 || { printf 'Another test holds this kit lock.\n' >&2; return 2; }
  if command -v fuser >/dev/null; then
    local rc=0
    fuser /dev/vipcore > "$work/device-users.log" 2>&1 || rc=$?
    if ((rc==0)); then
      printf 'Another process has /dev/vipcore open; stop it manually first.\n' >&2; return 2;
    elif ((rc!=1)); then
      printf 'Cannot inspect device users; inspect device-users.log.\n' >&2; return 2;
    fi
  else
    printf 'fuser unavailable; relying on your explicit NPU-idle confirmation.\n' \
      > "$work/device-users.log"
  fi
  verify_assets || return $?
  # Only the hash-verified kit binary gets an execute bit; no system chmod.
  chmod u+x "$base/voice-golden-linux-aarch64"
  if ! LD_LIBRARY_PATH="$base/lib" ldd "$base/voice-golden-linux-aarch64" \
       > "$work/ldd.log" 2>&1; then
    cat "$work/ldd.log" >&2; return 2
  fi
  if grep -q 'not found' "$work/ldd.log"; then
    cat "$work/ldd.log" >&2; return 2
  fi
}

run_stage()
{
  local tag="$1"; shift
  phase="$tag"
  printf '\n=== %s ===\n' "$tag"
  hardware_started=1
  set +e
  LD_LIBRARY_PATH="$base/lib" timeout --signal=TERM --kill-after=5s 60s \
    "$base/voice-golden-linux-aarch64" "$@" 2>&1 | tee "$work/$tag.log"
  local states=("${PIPESTATUS[@]}")
  local rc=${states[0]}
  ((rc!=0)) || rc=${states[1]}
  printf 'process-exit=%s tee-exit=%s\n' "${states[0]}" "${states[1]}" >> "$work/$tag.log"
  set -e
  if ((rc!=0)); then
    printf 'STOP: %s failed (exit=%s); no further NPU calls.\n' "$tag" "$rc" >&2
    if ((rc==124 || rc==137)); then
      printf 'Deadline exceeded: preserve logs and cold reboot before NPU reuse. Killing userspace is NOT a hardware reset.\n' >&2
    fi
    return "$rc"
  fi
  grep -q '^reference-status=0;' "$work/$tag.log" || {
    printf 'STOP: successful cleanup marker missing.\n' >&2; return 1;
  }
}

check_counts()
{
  grep -Fxq "inputs=$2 outputs=$3" "$work/$1.log" || {
    printf 'STOP: unexpected input/output counts in %s.\n' "$1" >&2; return 1;
  }
}

inspect_model()
{
  run_stage "$1" "$base/$2" --inspect || return $?
  check_counts "$1" "$3" "$4"
}

zero_joiner()
{
  local tag="$1"
  run_stage "$tag" "$base/models/kws/joiner_float_a733.nb" --zero "$work/$tag" || return $?
  check_counts "$tag" 2 1 || return $?
  grep -q '^run-status=0 wall-us=' "$work/$tag.log" || return 1
  grep -Eq '^output\[0\] bytes=[0-9]+ raw-crc32=[0-9a-f]+ changed=[1-9][0-9]* status=0$' \
    "$work/$tag.log" || { printf 'STOP: output unchanged/incomplete.\n' >&2; return 1; }
  [[ -s "$work/$tag/output0.bin" && ! -L "$work/$tag/output0.bin" ]]
}

finish()
{
  local rc=$?
  trap - EXIT INT TERM
  set +e
  {
    printf 'result=%s\nphase=%s\nexit=%s\nhardware_started=%s\n' "$result" "$phase" "$rc" "$hardware_started"
    printf '%s\n' "${scope_note:-Only interface/zero-input repeatability checks. NOT KWS/ASR/TTS accuracy or openvela acceptance.}"
    if ((rc!=0 && hardware_started)); then
      printf 'Do not retry hardware automatically. If timed out/hung, preserve this archive and cold reboot.\n'
    fi
  } > "$work/SUMMARY.txt"
  dmesg > "$work/dmesg.log" 2>&1
  local archive
  archive=$(mktemp "$base/results/voice-report-XXXXXXXX.tar.gz")
  if [[ -n "$archive" ]] && tar -C "$work" -czf "$archive" .; then
    if ((EUID==0)) && [[ ${SUDO_UID:-} =~ ^[0-9]+$ && ${SUDO_GID:-} =~ ^[0-9]+$ ]]; then
      chown "$SUDO_UID:$SUDO_GID" "$archive" || true
    fi
    printf '\nREPORT: %s\n' "$archive"
  else
    printf 'Archive failed; original logs retained: %s\n' "$work" >&2
    ((rc!=0)) || rc=1
  fi
  printf 'LOGS: %s\nRESULT: %s (exit=%s)\n' "$work" "$result" "$rc"
  exit "$rc"
}

validation_pipeline()
{
  inspect_model encoder-info models/kws/encoder_float_a733.nb 39 39 || return $?
  inspect_model decoder-info models/kws/decoder_float_a733.nb 1 1 || return $?
  inspect_model joiner-info models/kws/joiner_float_a733.nb 2 1 || return $?
  inspect_model vocoder-info models/vocoder/vocoder_int16_a733.nb 1 1 || return $?
  zero_joiner joiner-zero-a || return $?
  zero_joiner joiner-zero-b || return $?
  phase=compare
  sha256sum "$work/joiner-zero-a/output0.bin" "$work/joiner-zero-b/output0.bin" > "$work/output-hashes.txt"
  cmp "$work/joiner-zero-a/output0.bin" "$work/joiner-zero-b/output0.bin" || return $?
  printf 'Two zero-input outputs are byte-identical. Accuracy/dynamic-input proof still pending.\n'
  result=PASS_INTERFACE_AND_ZERO_REPEAT_ONLY
  phase=finished
}

main()
{
  local mode='' confirmed=0 root
  root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
  while (($#)); do
    case "$1" in
      --env-only|--run) [[ -z "$mode" ]] || { usage >&2; return 2; }; mode="$1"; shift ;;
      --npu-idle-confirmed) confirmed=1; shift ;;
      --bundle-dir) (($#>=2)) || { usage >&2; return 2; }; root="$2"; shift 2 ;;
      --help|-h) usage; return 0 ;;
      *) usage >&2; return 2 ;;
    esac
  done
  [[ -n "$mode" ]] || { usage >&2; return 2; }
  [[ "$mode" != --run || "$confirmed" == 1 ]] || {
    printf 'Refusing hardware access without --npu-idle-confirmed. Stop other NPU apps first.\n' >&2; return 2;
  }
  set -Eeuo pipefail
  umask 077
  export LC_ALL=C
  base=$(cd -- "$root" && pwd -P)
  [[ ! -L "$base/results" ]] || { printf 'Refuse symlink results directory.\n' >&2; return 2; }
  mkdir -p "$base/results"
  work=$(mktemp -d "$base/results/run-$(date -u +%Y%m%dT%H%M%SZ)-XXXXXXXX")
  phase=environment result=FAILED hardware_started=0
  trap finish EXIT
  trap 'exit 130' INT
  trap 'exit 143' TERM
  environment_log
  printf 'Logs: %s\n' "$work"
  if [[ "$mode" == --env-only ]]; then
    result=ENV_ONLY_NO_NPU_CALLS
    return 0
  fi
  phase=preflight
  preflight || return $?
  validation_pipeline || return $?
}

if [[ ${BASH_SOURCE[0]} == "$0" ]]; then
  main "$@"
fi
