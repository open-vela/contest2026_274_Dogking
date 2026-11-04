#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Extend existing staged-source gate with every changed local LLM source."""
import hashlib,json,subprocess,sys
from pathlib import Path
repo=Path(__file__).resolve().parents[1];sdk=Path(sys.argv[1])
overlay=repo/'board/a733-cubie-a7z/openvela-overlay'
record=json.loads(subprocess.check_output([sys.executable,str(repo/'tools/check-a733-tensor-staged.py'),str(sdk),'--ufs']))
names=['apps/system/aipetllm/'+n for n in (
 'CMakeLists.txt','Kconfig','aipetllm_modelcheck.c','aipetllm_bpe.cxx',
 'aipetllm_activation.c','aipetllm_unicode.inc','aipetllm_api.cxx',
 'aipetllm_api.h','aipetllm_sequence.h','aipetllm_prefill.inc')]
names+=['apps/system/aipet/'+n for n in ('pet_routes.cxx','pet_routes.hxx','local_llm.cxx')]
for name in names:
    data=(overlay/name).read_bytes()
    if data!=(sdk/name).read_bytes():raise ValueError('Staging mismatch: '+name)
    record['files'][name]=hashlib.sha256(data).hexdigest()
print(json.dumps(record,indent=2))
