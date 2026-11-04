#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Record software/image qualification separately from microphone acceptance."""
import hashlib,json,shutil,sys
from pathlib import Path
repo=Path(__file__).resolve().parents[1]
cap=repo/'out/v182-live-candidate-20261025'
run=repo/'out/live-build-v182-r1'
evidence=repo/'docs/evidence/live-voice-v182'
record=json.loads((cap/'manifest.json').read_text())
assert record['result']=='V182_IMAGE_PRODUCED_NOT_BOARD_TESTED'
assert record['sdk_restored']['result']=='SDK_RESTORED_BYTE_EXACT'
assert not record['full_offline_chain_complete'] and not record['board_executed']
def sha(path):
 h=hashlib.sha256()
 with path.open('rb') as f:
  while data:=f.read(1048576):h.update(data)
 return h.hexdigest()
ov=repo/'board/a733-cubie-a7z/openvela-overlay'
for name,value in record['staged']['files'].items():assert sha(ov/name)==value,name
arm=json.loads((repo/'out/live-arm-v182-r1/summary.json').read_text())
for name,info in arm.items():assert sha(repo/name)==info['sha256'] and info['max_static_frame']<=4096
logs={'out/v182-live-host-r1.log':'PASS: live 48->16 FIR byte-exact',
 'out/v182-audio-r3.log':'speech output contracts passed',
 'out/v182-mcu-r2.log':'NOT board acceptance',
 'out/v182-routes-r2.log':'bounded ordered routes/config/template tests passed',
 'out/v182-i2s-stream.log':'PASS: actual I2S stream TX function',
 'out/v182-i2s-live.log':'PASS: actual RX loop',
 'out/v182-i2s-matrix.log':'PASS: 8 configs',
 'out/live-build-v182-r1/image-verified.log':'Embedded kernel SHA256:',
 'out/live-build-v182-r1/final-layout.log':'footprint passed'}
for name,text in logs.items():assert text in (repo/name).read_text(),name
assert record['image']['sha256']==sha(Path(record['image']['path']))
assert record['artifacts']['Image']['sha256']==sha(cap/'Image')
evidence.mkdir()
for name in logs:shutil.copyfile(repo/name,evidence/Path(name).name)
record.update(arm64=arm,logs={name:sha(repo/name) for name in logs},
              previous_release='a733-v181-voice-latency-20261025-r2',
              application_only_changes=True,required_next='physical FIFO/scheduling/echo/endpoint validation; native general ASR evidence still missing')
(evidence/'summary.json').write_text(json.dumps(record,indent=2),encoding='utf8')
print(json.dumps({'image':record['image'],'kernel':record['artifacts']['Image'],'result':record['result']},indent=2))
