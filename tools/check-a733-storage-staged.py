#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Hash all bulk application inputs while the official SDK is staged."""
from pathlib import Path
import hashlib,json,sys
repo=Path(__file__).resolve().parent.parent
sdk=Path(sys.argv[1]);files={}
for f in sorted((repo/'app/a733storage').iterdir()):
    if not f.is_file():continue
    name='apps/system/a733storage/'+f.name
    if f.read_bytes()!=(sdk/name).read_bytes():raise ValueError('Staged mismatch: '+name)
    files[name]=hashlib.sha256(f.read_bytes()).hexdigest()
print(json.dumps(dict(result='STAGED_STORAGE_MATCH',files=files),indent=2))
