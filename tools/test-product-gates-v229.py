#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Inherited 42 gates plus DNS callback and same-process actual TLS rounds."""
from pathlib import Path
import runpy,subprocess,json,hashlib,time
state=runpy.run_path(str(Path(__file__).with_name('test-product-gates-v228.py')),run_name='__main__');r=state['r'];out=state['out']
report=json.loads((out/'summary.json').read_text());assert len(report['tests'])==42
for name,cmd in [('dns-close-v229',['python3','tools/test-dns-close-v229.py']),('actual-rounds-v229',['python3','tools/test-cloud-rounds-v229.py','--output',str(out/'actual-rounds-v229')])]:
    log=out/(name+'.log');start=time.monotonic()
    with log.open('x') as f:subprocess.run(cmd,cwd=r,check=True,timeout=240,stdout=f,stderr=subprocess.STDOUT)
    report['tests'].append(dict(name=name,command=cmd,seconds=time.monotonic()-start,sha256=hashlib.sha256(log.read_bytes()).hexdigest()))
report.update(result='V229_VOICE_HOST_ARM_PASSED_NOT_BOARD',software_gates=44,product='desktop-pet-v3.0pre-fix2-v229-candidate',
              multi_address_TCP_TLS=True,bounded_protocol_shutdown_ms=500,DNS_truncated_UDP_closed=True,board_tested=False)
(out/'summary.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS v229:44 software gates. NOT board/cloud account acceptance.',flush=True)
