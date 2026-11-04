#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Validate generated script wire format and mocked command control flow."""
import importlib.util
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import zlib

repo=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('handoff',repo/'tools/build-a733-camera-handoff-v192.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
args=(0x40202000,0x40200000,0x700000,0x5800000,0x01234567)
source,record=m.image_script(*args)
assert record==0x40082000
data=m.legacy_script(source)
fields=struct.unpack('>7I4B32s',data[:64])
assert fields[0]==0x27051956 and fields[2]==0 and fields[3]==len(data)-64
assert fields[7:11]==(5,2,6,0)
assert fields[1]==zlib.crc32(data[:4]+bytes(4)+data[8:64])
assert fields[6]==zlib.crc32(data[64:])
assert struct.unpack('>II',data[64:72])==(len(source.encode('ascii')),0)
assert data[72:]==source.encode('ascii')
assert data==m.legacy_script(source)  # reproducible timestamp/header
for badindex,value in ((0,0x40202001),(0,0x40100000),(0,0x40900000),
                       (1,0x40080000),(2,32),(3,0x690000),(3,0x9000000),
                       (4,0),(4,0xffffffff)):
    bad=list(args);bad[badindex]=value
    try:m.image_script(*bad)
    except ValueError:pass
    else:raise AssertionError(('Accepted unqualified layout',bad))
for forbidden in ('saveenv','setenv','i2c write','i2c mw','sunxi_axp','mmc write',
                  'i2c probe','i2c reset','mw.l 020','mw.l 025'):
    assert forbidden not in source
with tempfile.TemporaryDirectory(prefix='camera-handoff-v192-') as tmp:
    cmd=Path(tmp)/'script.cmd';cmd.write_text(source)
    cases=['none','endian','load','size','crc-command','crc','dev','bad-id',
           'bad-version','fdt','commit']+['read'+str(i) for i in range(6)]
    for fail in cases:
        run=subprocess.run(['bash',str(repo/'tools/test-a733-camera-handoff-v192.sh'),str(cmd)],
            capture_output=True,text=True,env={**os.environ,'FAIL':fail},timeout=10)
        success=fail in ('none','endian')
        assert ('MOCK_BOOTI_REACHED' in run.stdout)==success,(fail,run.stdout,run.stderr)
        assert (run.returncode==0)==success,(fail,run.returncode,run.stderr)
        assert not run.stderr,(fail,run.stderr)  # Logs must not become shell commands.
print('PASS: 17 generated Hush command-model paths, 9 rejected layouts, deterministic legacy script header/CRCs; NOT actual U-Boot execution')
source193,record193=m.image_script(*args,release=193)
assert record193==record and source193==source.replace('v192','v193')
data193=m.legacy_script(source193,193)
assert data193[32:64].rstrip(b'\0')==b'A733 camera handoff v193'
assert data193[72:]==source193.encode('ascii')
assert data193==m.legacy_script(source193,193)
with tempfile.TemporaryDirectory(prefix='camera-handoff-v193-') as tmp:
    cmd=Path(tmp)/'script.cmd';cmd.write_text(source193)
    for fail in cases:
        run=subprocess.run(['bash',str(repo/'tools/test-a733-camera-handoff-v192.sh'),str(cmd)],
            capture_output=True,text=True,env={**os.environ,'FAIL':fail},timeout=10)
        success=fail in ('none','endian')
        assert ('MOCK_BOOTI_REACHED' in run.stdout)==success and (run.returncode==0)==success
        assert not run.stderr,(fail,run.stderr)
for func,params in ((m.image_script,(*args,200)),(m.legacy_script,(source,200))):
    try:func(*params)
    except ValueError:pass
    else:raise AssertionError('Unsupported release accepted')
print('PASS: v193 equivalent 17 script command-model paths, fixed release/name and two unsupported-release rejections; NOT actual U-Boot')
