#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""v179 gate: previous NPU/LLM gate plus every actual aipet source/header."""
import hashlib
import json
import subprocess
import sys
from pathlib import Path
repo = Path(__file__).resolve().parents[1]
sdk = Path(sys.argv[1])
base = repo / 'board/a733-cubie-a7z/openvela-overlay'
record = json.loads(subprocess.check_output([sys.executable, str(repo / 'tools/check-a733-llm-staged.py'), str(sdk)]))
for src in sorted((base / 'apps/system/aipet').rglob('*')):
    if not src.is_file(): continue
    name = str(src.relative_to(base))
    data = src.read_bytes()
    if data != (sdk / name).read_bytes(): raise ValueError('Staged source mismatch: ' + name)
    record['files'][name] = hashlib.sha256(data).hexdigest()
print(json.dumps(record, indent=2))
