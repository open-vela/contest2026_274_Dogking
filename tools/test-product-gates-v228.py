#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""41 inherited gates plus actual strict-cloud worker failure/recovery cases."""
from pathlib import Path
import runpy,subprocess,json,hashlib,time
p=Path(__file__).with_name('test-product-gates-v227.py')
state=runpy.run_path(str(p),run_name='__main__');r=state['r'];out=state['out']
report=json.loads((out/'summary.json').read_text());assert len(report['tests'])==41
cmd=['python3','tools/test-cloud-only-v228.py'];log=out/'cloud-only-v228.log';start=time.monotonic()
with log.open('x') as f:subprocess.run(cmd,cwd=r,check=True,timeout=240,stdout=f,stderr=subprocess.STDOUT)
report['tests'].append(dict(name='cloud-only-v228',command=cmd,seconds=time.monotonic()-start,sha256=hashlib.sha256(log.read_bytes()).hexdigest()))
report.update(result='V228_VOICE_HOST_ARM_PASSED_NOT_BOARD',software_gates=42,
 product='desktop-pet-v3.0pre-fix-v228-candidate',resident_cloud_mode='cloud-only-no-local-fallback',
 explicit_cloud_policy='cloud-only-no-local-fallback',auto_fallback_opt_in=True,
 board_tested=False,hardware_executed=False,cloud_account_calls=False)
(out/'summary.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS v228:42 software gates; strict CLOUD never loads local. NOT board acceptance.',flush=True)
