#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Write NEW image files only. Never a block device, source image or UFS.
set -euo pipefail
cd "$(dirname "$0")/.."
run=${1:-out/live-build-v182-r1}
cap=${2:-out/v182-live-candidate-20261025}
base=../openvela-a733-cubie-a7z-sd-v178-llm-candidate.img
image=../openvela-a733-cubie-a7z-sd-v182-live-voice-candidate.img
sdk=${OPENVELA_WORKSPACE:-$(cd ../../quickly-openvela && pwd)}
build="$sdk/cmake_out/cubie-a7z_nsh_v182_live_capture"
export PATH="$sdk/prebuilts/gcc/linux-x86_64/aarch64-none-elf/bin:$PATH"
[[ ! -e "$cap" && ! -e "$image" ]]
python3 tools/check-a733-mcu-staging.py restored "$sdk" "$run/sdk-before.json"
python3 - "$run" <<'PY'
import hashlib,json,sys
from pathlib import Path
root=Path.cwd();ov=root/'board/a733-cubie-a7z/openvela-overlay'
staged=json.loads((Path(sys.argv[1])/'staged.json').read_text())
assert staged['result']=='STAGED_SOURCES_MATCH'
for path,h in staged['files'].items():assert hashlib.sha256((ov/path).read_bytes()).hexdigest()==h,path
arm=json.loads((root/'out/live-arm-v182-r1/summary.json').read_text())
for path,info in arm.items():assert hashlib.sha256((root/path).read_bytes()).hexdigest()==info['sha256'] and info['max_static_frame']<=4096
for file,expected in [('v182-live-host-r1.log','PASS: live 48->16 FIR byte-exact'),
                      ('v182-audio-r3.log','speech output contracts passed'),
                      ('v182-mcu-r2.log','NOT board acceptance'),
                      ('v182-routes-r2.log','bounded ordered routes/config/template tests passed')]:
 text=(root/'out'/file).read_text();assert expected in text and 'ERROR: AddressSanitizer' not in text,file
PY
echo 'e9352cccd0ce73d895e0741a4808195d707d34937097a339bfc2d87b8fb835d3  ../openvela-a733-cubie-a7z-sd-v178-llm-candidate.img' | sha256sum -c -
bash tools/check-a733-elf-layout.sh "$build/nuttx" "$build/nuttx.bin" >"$run/final-layout.log" 2>&1
mkdir "$cap"
cp "$build/nuttx.bin" "$cap/Image"
cp "$build/nuttx" "$cap/nuttx.elf"
cp "$build/.config" "$cap/build.config"
cp tools/board-tests/v182-voice-test.nsh "$cap/"
bash tools/package-a733-kernel-image.sh "$base" "$image" "$cap/Image" >"$run/package.log" 2>&1
hash=$(sha256sum "$cap/Image" | cut -d' ' -f1)
bash tools/verify-a733-image.sh "$image" "$hash" >"$run/image-verified.log" 2>&1
python3 - "$run" "$cap" "$image" <<'PY'
import hashlib,json,sys
from pathlib import Path
def sha(p):
 h=hashlib.sha256()
 with p.open('rb') as f:
  while b:=f.read(1048576):h.update(b)
 return h.hexdigest()
run,cap,image=map(Path,sys.argv[1:]);assert image.stat().st_size==2147483648
record={'result':'V182_IMAGE_PRODUCED_NOT_BOARD_TESTED','image':{'path':str(image.resolve()),'bytes':image.stat().st_size,'sha256':sha(image)},
 'artifacts':{p.name:{'bytes':p.stat().st_size,'sha256':sha(p)} for p in cap.iterdir() if p.is_file()},
 'staged':json.loads((run/'staged.json').read_text()),'sdk_restored':json.loads((run/'sdk-restored.json').read_text()),
 'new_driver_pinmux_or_npu_mapping':False,'target_general_asr_provider':False,'target_matcha_acoustic_executor':False,
 'board_executed':False,'full_offline_chain_complete':False,'first_audio_3_to_4_seconds_accepted':False}
(cap/'manifest.json').write_text(json.dumps(record,indent=2))
print(json.dumps({'result':record['result'],'image':record['image'],'kernel':record['artifacts']['Image']},indent=2))
PY
