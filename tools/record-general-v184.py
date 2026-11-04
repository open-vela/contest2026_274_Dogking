#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Record verified host/target-object evidence, never invent board acceptance."""
import hashlib
import json
from pathlib import Path
import shutil

repo=Path(__file__).resolve().parents[1]
dest=repo/'docs/evidence/general-asr-v184'
dest.mkdir(exist_ok=True)
host=repo/'out/asr-backend-replay-v184-r2'
summary=json.loads((host/'summary.json').read_text())
assert summary['result']=='HOST_CAPTURE_REPLAY_PASS' and not summary['hardware_executed']
pet=repo/'board/a733-cubie-a7z/openvela-overlay/apps/system/aipet'
for name,expected in summary['source_sha256'].items():
    assert hashlib.sha256((pet/name).read_bytes()).hexdigest()==expected
sources={
    'host-capture-replay.json':host/'summary.json',
    'host-capture-replay.txt':host/'run.log',
    'target-object-stack.json':repo/'out/general-arm-v184-r1/summary.json',
    'base-sdk-restored.json':repo/'out/general-build-v184-r1/sdk-restored.json',
    'prepared-export-index.json':repo/'out/general-asr-export-v184-r2/packages/export-index.json'}
assert json.loads(sources['base-sdk-restored.json'].read_text())['result']=='SDK_RESTORED_BYTE_EXACT'
for target,source in sources.items():
    assert not (dest/target).exists()
    shutil.copyfile(source,dest/target)
print('Recorded host/target-object evidence; target NPU replay/full voice NOT qualified')
