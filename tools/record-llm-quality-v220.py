#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Keep the independent real-GGUF oracle and require the preview code path."""
from pathlib import Path
import sys
p=Path(__file__).with_name('record-llm-quality-v216.py')
assert 'llm-latency-v220:' in Path(sys.argv[1]).read_text()
s=p.read_text().replace('V216_REAL_GGUF_REFERENCE_PASSED_NOT_BOARD',
                        'V220_REAL_GGUF_REFERENCE_PASSED_NOT_BOARD')
exec(compile(s,str(p),'exec'))
report.update(preview_config='CONFIG_A733_PRODUCT_V12=1',
              same_math_cold_warm_batch=True,host_timing_not_board_timing=True)
for name in ('local_llm.cxx','product_runtime.cxx'):
    path='board/a733-cubie-a7z/openvela-overlay/apps/system/aipet/'+name
    current=(r/path).read_bytes()
    committed=subprocess.check_output(['git','show',a.firmware_commit+':'+path],cwd=r)
    assert current.replace(b'\r\n',b'\n')==committed.replace(b'\r\n',b'\n')
    report['source_sha256'][path]=sha(r/path)
(out/'summary.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
