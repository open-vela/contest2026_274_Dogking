#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
import re,subprocess,tempfile
r=Path(__file__).resolve().parents[1]
model='board/a733-cubie-a7z/openvela-overlay/apps/system/aipetllm/aipetllm_modelcheck.c'
source=(r/model).read_text();old=subprocess.check_output(['git','show','a733-v210-voice-source-20261030:'+model],cwd=r,text=True)
for pattern in (r'  for \(row = worker->first;.*?\n  return NULL;\n\}',
                r'  for \(unsigned b = 0; b < batch; b\+\+\)\n    quantize_row_q8_K\(.*?;'):
    assert re.search(pattern,source,re.S)[0]==re.search(pattern,old,re.S)[0]
ggml=r/'board/a733-cubie-a7z/openvela-overlay/apps/system/aipetllm/third_party/ggml'
with tempfile.TemporaryDirectory(prefix='q8-v211-') as tmp:
    exe=Path(tmp)/'test'
    subprocess.run(['gcc','-std=c11','-D_GNU_SOURCE','-O1','-g','-ffunction-sections','-fdata-sections',
      '-fsanitize=address,undefined','-I'+str(ggml/'include'),'-I'+str(ggml/'src'),
      str(ggml/'src/ggml-quants.c'),str(r/'tools/test-q8-workspace-v211.c'),
      '-Wl,--gc-sections','-lm','-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
