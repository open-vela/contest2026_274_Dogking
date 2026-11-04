#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Save actual CPU/compile/link-gate evidence without model/library bytes."""
import hashlib
import json
from pathlib import Path
import shutil
repo=Path(__file__).resolve().parents[1]
dest=repo/'docs/evidence/general-asr-v184/acoustic';dest.mkdir()
host=repo/'out/acoustic-capi-host-v184-r2'
summary=json.loads((host/'summary.json').read_text())
pet=repo/'board/a733-cubie-a7z/openvela-overlay/apps/system/aipet'
for name,digest in summary['sources'].items():
    assert hashlib.sha256((pet/name).read_bytes()).hexdigest()==digest
assert (repo/'out/general-build-v184-r2/wrapper-status.txt').read_text().strip()=='0'
files={
 'host-cpu-execution.json':host/'summary.json','host-cpu-execution.txt':host/'run.log',
 'original-onnx-comparison.json':repo/'out/acoustic-capi-compare-v184-r2/summary.json',
 'target-object-stack.json':repo/'out/acoustic-arm-v184-r1/summary.json',
 'sdk-restored-r2.json':repo/'out/general-build-v184-r2/sdk-restored.json',
 'static-runtime-build.json':repo/'out/ort-build-v184-newabi-r6-result.json',
 'static-runtime-manifest.json':repo/'out/ort-static-v184-r1/manifest.json',
 'link-probe-failure.json':repo/'out/ort-link-probe-v184-r1/summary.json',
 'link-probe-failure.txt':repo/'out/ort-link-probe-v184-r1/link.log'}
for name,source in files.items():shutil.copyfile(source,dest/name)
print('Recorded CPU host/object/static evidence + explicit FAILED full-link gate; no hardware acceptance')
