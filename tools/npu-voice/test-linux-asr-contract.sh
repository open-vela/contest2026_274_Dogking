#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# New general-ASR model contract smoke ONLY; not a recognition acceptance.
set -euo pipefail
cd "$(dirname "$0")"
if [[ $# != 2 || "$2" != --npu-idle-confirmed ||
      ( "$1" != --inspect && "$1" != --run-zero-smoke ) ]]; then
  echo 'Usage: sudo bash test-linux-asr-contract.sh --inspect --npu-idle-confirmed'
  echo '   or: sudo bash test-linux-asr-contract.sh --run-zero-smoke --npu-idle-confirmed'
  echo 'Stop other NPU programs first. No reset/retry/format/ASR acceptance.'
  exit 2
fi
[[ $(uname -m) == aarch64 && -c /dev/vipcore ]]
command -v fuser >/dev/null || { echo 'fuser required for NPU ownership check';exit 2; }
idle_check() {
  local rc=0
  fuser /dev/vipcore > /dev/null 2>&1 || rc=$?
  [[ "$rc" == 1 ]] || { echo 'NPU busy or ownership unknown; no run/reset';return 2; }
  for sensor in /sys/class/thermal/thermal_zone*/temp; do
    [[ -r "$sensor" ]] || continue
    local temp;read -r temp <"$sensor"
    [[ "$temp" =~ ^[0-9]+$ && "$temp" -lt 75000 ]] || {
      echo 'Thermal status unsafe/unknown; cool/check manually';return 2;
    }
  done
}
python3 - <<'PY'
import hashlib,json,struct
from pathlib import Path
r=json.loads(Path('files-sha256.json').read_text())
for name,info in r.items():
 p=Path(name)
 assert not p.is_symlink() and p.is_file() and p.stat().st_size==info['bytes'],name
 assert hashlib.sha256(p.read_bytes()).hexdigest()==info['sha256'],name
for role in ('encoder','decoder','joiner'):
 with (Path('models')/(role+'.nb')).open('rb') as f:
  magic,version,cid=struct.unpack('<III',f.read(12))
 assert magic==0x4e4d5056 and version in (0x10020,0x20000) and cid==0x1000003b
print('Files/CID verified. Not a board inference or transcript result.')
PY
mkdir -p results
result=$(mktemp -d "$PWD/results/asr-contract-XXXXXXXX")
echo "REPORT DIRECTORY: $result"
trap 'echo "Stopped on error; keep logs. Do not automatically rerun or reset NPU."' ERR
for role in decoder joiner encoder; do
  idle_check
  ./voice-golden-linux-aarch64 "models/$role.nb" --inspect >"$result/$role-inspect.log" 2>&1
done
if [[ "$1" == --run-zero-smoke ]]; then
  for role in decoder joiner encoder; do
    idle_check
    ./voice-golden-linux-aarch64 "models/$role.nb" --zero "$result/$role-zero" \
      >"$result/$role-zero.log" 2>&1
  done
fi
python3 - "$result" "$1" <<'PY'
import json,sys
from pathlib import Path
p=Path(sys.argv[1]);run=sys.argv[2]=='--run-zero-smoke'
for role in ('decoder','joiner','encoder'):
 text=(p/(role+'-inspect.log')).read_text()
 assert 'reference-status=0' in text
 if run:
  text=(p/(role+'-zero.log')).read_text()
  assert 'reference-status=0' in text and 'run-status=0' in text
data={'contract_inspection':True,'synthetic_zero_smoke':run,
      'real_audio':False,'transcript_accepted':False,'openvela_passed':False,
      'full_voice_chain':False}
(p/'result.json').write_text(json.dumps(data,indent=2))
print(json.dumps(data,indent=2))
PY
report="results/$(basename "$result").tar.gz"
tar -czf "$report" -C results "$(basename "$result")"
echo "RETURN THIS FILE: $PWD/$report"
