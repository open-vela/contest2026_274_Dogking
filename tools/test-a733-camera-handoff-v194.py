#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import sys
repo=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('handoff',repo/'tools/build-a733-camera-handoff-v192.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
release=int(sys.argv[1]) if len(sys.argv)>1 else 194
assert release in (194,195,196,197,198,199)
args=(0x40202000,0x40200000,0x700000,0x5800000,0x01234567,release)
source,_=m.image_script(*args,0x40204000)
assert 'imx214-1080p.a7cm 294' in source and 'mode' not in source.split('booti ')[-1]
for bad in (None,0x40204001,0x40202000,0x40201fc0,0x40900000):
    try:m.image_script(*args,bad)
    except ValueError:pass
    else:raise AssertionError(('Mode record accepted',bad))
cases=['none','endian','load','size','crc-command','crc','dev','bad-id',
       'bad-version','fdt','commit']+['read'+str(i) for i in range(6)]
cases+=['mode-signature','mode-abi','mode-read','mode-size','mode-bytes','mode-ready']
with tempfile.TemporaryDirectory(prefix='handoff-v194-') as tmp:
    cmd=Path(tmp)/'test.cmd';cmd.write_text(source)
    for fail in cases:
        run=subprocess.run(['bash',str(repo/'tools/test-a733-camera-handoff-v192.sh'),str(cmd)],
            capture_output=True,text=True,timeout=10,env={**os.environ,'FAIL':fail,'RELEASE':str(release)})
        good=fail in ('none','endian')
        assert (run.returncode==0)==good and ('MOCK_BOOTI_REACHED' in run.stdout)==good,(fail,run.stdout,run.stderr)
        assert not run.stderr,(fail,run.stderr)
print(f'PASS: 23 v{release} exact-image/external-mode/PMIC script paths, 5 invalid mode layouts rejected; COMMAND MODEL ONLY')
