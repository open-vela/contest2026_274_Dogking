#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Fail-closed host regression bundle. No physical board, disk or NPU calls."""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys,time
r=Path(__file__).resolve().parents[1];p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
tests=[('pixels',['test-a733-perf-pixels-v205.py']),('display',['test-a733-live-display-v204.py']),
 ('legacy-app',['test-a733-live-v204.py']),('optimized-app',['test-a733-live-v204.py','--perf']),
 ('stream',['test-a733-camera-stream-v205.py']),('resident',['test-a733-npu-resident-v205.py']),
 ('tone-compat',['test-a733-tone-compat-v204.py']),('vfs',['test-a733-live-vfs-v204.py']),
 ('camera-v203',['test-a733-camera-v203.py']),('csi-old',['test-a733-csi-capture.py']),
 ('registers',['test-a733-csi-registers-v199.py']),('old-log',['test-a733-live-log-v204.py']),
 ('perf-log',['test-a733-perf-log-v205.py'])]
tests.append(('stream-fix',['test-a733-stream-fix-v206.py']))
tests.append(('swout1',['test-a733-swout1-v200.py']))
results=[]
for name,args in tests:
    start=time.monotonic();path=a.output/(name+'.log')
    with path.open('x') as log:
        subprocess.run([sys.executable,str(r/'tools'/args[0]),*args[1:]],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=240)
    results.append(dict(name=name,arguments=args,seconds=time.monotonic()-start,sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
    print('PASS '+name,flush=True)
(a.output/'summary.json').write_text(json.dumps(dict(result='V206_HOST_REGRESSION_PASSED',tests=results,
    hardware_verified=False,performance_scope='host timing is NOT A733 performance'),indent=2)+'\n')
