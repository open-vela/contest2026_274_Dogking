#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Keep all 19 voice gates and add actual product integration tests/ARM units."""
from pathlib import Path
import argparse,json,subprocess,time,hashlib
r=Path(__file__).resolve().parents[1];p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--base',type=Path,required=True);p.add_argument('--build',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
out=a.output.resolve();assert out.is_relative_to(r/'out');out.mkdir()
base=json.loads((a.base/'summary.json').read_text());assert base['result']=='V216_VOICE_HOST_ARM_PASSED_NOT_BOARD' and len(base['tests'])==19
tests=list(base['tests'])
for name,args in [('product-core',['bash','tools/test-product-v1.sh']),('product-service',['python3','tools/test-product-service-v1.py']),
                  ('mcu',['bash','tools/test-aipet-mcu.sh']),('product-arm',['python3','tools/check-a733-product-arm-v1.py','--build',str(a.build),'--output',str(out/'arm')])]:
    start=time.monotonic();log=out/(name+'.log')
    with log.open('x') as f:subprocess.run(args,cwd=r,check=True,timeout=900,stdout=f,stderr=subprocess.STDOUT)
    tests.append(dict(name=name,command=args,seconds=time.monotonic()-start,sha256=hashlib.sha256(log.read_bytes()).hexdigest()))
    print('PASS '+name,flush=True)
base.update(result='V217_VOICE_HOST_ARM_PASSED_NOT_BOARD',tests=tests,baseline_directory=str(a.base),
  baseline_summary_sha256=hashlib.sha256((a.base/'summary.json').read_bytes()).hexdigest(),
  product='desktop-pet-v1.0-candidate',cloud_api_executed=False,hardware_executed=False)
(out/'summary.json').write_text(json.dumps(base,indent=2)+'\n')
