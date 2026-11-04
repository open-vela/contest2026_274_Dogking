#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""New private Linux ASR contract kit; no libraries, flashing or HW calls."""
import hashlib,json,shutil,tarfile,sys,re
from pathlib import Path
repo=Path(__file__).resolve().parents[2]
source=repo/'out/asr-export-v178-20261025'
name=sys.argv[1] if len(sys.argv)>1 else 'a733-linux-asr-contract-v178-20261025'
assert re.fullmatch(r'a733-linux-asr-contract-v178-[A-Za-z0-9-]+',name)
dest=repo/'out'/name
report=json.loads((source/'artifacts-verified.json').read_text())
assert report['result']=='EXPORTED_CID_CHECKED_NOT_BOARD_TESTED'
dest.mkdir();(dest/'models').mkdir()
for item in report['models']:
    nbg=item['nbg'][0];src=source/nbg['path']
    assert hashlib.sha256(src.read_bytes()).hexdigest()==nbg['sha256']
    shutil.copyfile(src,dest/'models'/(item['role']+'.nb'))
    shutil.copyfile(source/(item['role']+'-nbinfo.txt'),dest/(item['role']+'-nbinfo.txt'))
shutil.copyfile(source/'artifacts-verified.json',dest/'compiler-artifacts.json')
for src in (repo/'out/npu-voice/voice-golden-linux-aarch64',
            repo/'tools/npu-voice/voice-golden.c',
            repo/'tools/npu-voice/test-linux-asr-contract.sh'):
    shutil.copyfile(src,dest/src.name)
(dest/'voice-golden-linux-aarch64').chmod(0o755)
(dest/'README.txt').write_text('''GENERAL ASR CONTRACT CANDIDATE, NOT KWS, NOT WORKING SPEECH RECOGNITION
Run on Radxa Linux A733 with its installed vendor VIPLite runtime only.
Never flash/overwrite the current openvela card for this kit automatically.
Keep all other NPU programs stopped; do not use two test shells concurrently.

sudo bash test-linux-asr-contract.sh --inspect --npu-idle-confirmed
sudo bash test-linux-asr-contract.sh --run-zero-smoke --npu-idle-confirmed

Inspect queries native tensor padding/types and working-memory requirements.
Zero smoke executes synthetic zeros exactly once/model, not real speech.
No retries/resets on failure; preserve the returned tar.gz and all logs.
Do not treat successful zero runs as transcript or end-to-end acceptance.

NEXT: real fbank input, recurrent states, full native output capture and
greedy decoding against the 6254-class ASR vocabulary, then an openvela port.
Existing 263-class KWS assets MUST NOT replace these ASR models/tokens.
General offline TTS additionally needs Matcha CPU acoustic/text integration;
the already-tested vocoder alone cannot synthesize arbitrary reply text.
''')
files={str(p.relative_to(dest)):{'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()}
       for p in dest.rglob('*') if p.is_file()}
with (dest/'files-sha256.json').open('x') as f:json.dump(files,f,indent=2)
archive=dest.with_suffix('.tar.gz')
with tarfile.open(archive,'x:gz') as t:t.add(dest,arcname=dest.name)
print(json.dumps({'archive':str(archive),'bytes':archive.stat().st_size,
                  'sha256':hashlib.sha256(archive.read_bytes()).hexdigest(),
                  'hardware_executed':False},indent=2))
