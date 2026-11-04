#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Exhaustive allowlist + failures: model, not actual U-Boot/hardware."""
import hashlib
import importlib.util
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import zlib

repo=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('v200',repo/'tools/build-a733-swout1-v200.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
base,record=m.legacy_module().image_script(0x40202000,0x40200000,0x700000,0x5800000,0x1234567,199,0x40204000)
source=m.guarded_source(base,record)
assert source.count('sunxi_axp pmu write')==1
assert 'sunxi_axp pmu write 11 1f' in source and 'sleep 1' in source
for forbidden in ('saveenv','setenv','i2c write','i2c mw','mmc write','ufs write','pmu write 12','pmu write 2b'):
    assert forbidden not in source
assert not re.search(r'400820(?:1[6-9a-f]|[2-3][0-9a-f])\b',source), 'Reserved bytes touched'
image=m.legacy_image(source);fields=struct.unpack('>7I4B32s',image[:64])
assert fields[0]==0x27051956 and fields[3]==len(image)-64 and fields[7:11]==(5,2,6,0)
assert fields[1]==zlib.crc32(image[:4]+bytes(4)+image[8:64])
assert fields[6]==zlib.crc32(image[64:]) and image[72:]==source.encode('ascii')
assert image==m.legacy_image(source)
primary=repo.parent/'u-boot-2018-product-aiot-stable/drivers/sunxi_power/pmu_axp8191.c'
header=repo.parent/'u-boot-2018-product-aiot-stable/include/sunxi_power/pmu_axp8191.h'
text=primary.read_text();head=header.read_text()
assert re.search(r'"dc1sw1".*?AXP8191_DCDC_POWER_ON_OFF_CTL2,\s*3\s*}',text,re.S)
assert re.search(r'#define\s+AXP8191_DCDC_POWER_ON_OFF_CTL2\s+\(0x11\)',head)
assert re.search(r'#define\s+AXP8191_DC1OUT_VOL\s+\(0x12\)',head)
assert '"dcdc1", 1000, 3800, AXP8191_DC1OUT_VOL, 0x1f, 100' in text
cases=0
with tempfile.TemporaryDirectory(prefix='a733-swout1-model-') as temp:
    script=Path(temp)/'test.cmd';script.write_text(source)
    def run(ctrl='17',voltage='17',fail='none',success=False,writes=None):
        global cases
        env={**os.environ,'RELEASE':'200','CTRL':ctrl,'VOLTAGE':voltage,'FAIL':fail}
        result=subprocess.run(['bash',str(repo/'tools/test-a733-swout1-v200.sh'),str(script)],
                              env=env,capture_output=True,text=True,timeout=5)
        assert ('MOCK_BOOTI_REACHED' in result.stdout)==success,(ctrl,voltage,fail,result.stdout,result.stderr)
        assert (result.returncode==0)==success,(ctrl,voltage,fail,result.returncode,result.stdout)
        assert not result.stderr,(fail,result.stderr)
        match=re.search(r'MODEL writes=(\d+).*ready=(\d+) ctrl=([0-9a-f]+)',result.stdout)
        assert match,(fail,result.stdout)
        if writes is not None:assert int(match[1])==writes,(ctrl,voltage,fail,result.stdout)
        if not success and fail!='diagnostic':assert match[2]=='0',(fail,result.stdout)
        cases+=1
    for ctrl in range(256):
        good=ctrl in (0x17,0x1f)
        run(f'{ctrl:02x}',success=good,writes=1 if ctrl==0x17 else 0)
    for voltage in range(256):
        run(voltage=f'{voltage:02x}',success=voltage==0x17,writes=1 if voltage==0x17 else 0)
    early=['load','size','crc-command','crc','dev','bad-id','bad-version','fdt','commit',
           'mode-signature','mode-abi','mode-read','mode-size','mode-bytes','mode-ready',
           'dcdc-enable','power-read-before']+['read'+str(i) for i in range(6)]
    for fail in early:
        run(fail=fail,writes=None if fail=='commit' else 0)
    for fail in ['power-write-before','power-write-after','settle','post-dev','power-read-after',
                 'control-drift','voltage-read-after','voltage-drift','diagnostic']:
        run(fail=fail,writes=1)
    for fail in ['settle','post-dev','power-read-after','control-drift','voltage-read-after','voltage-drift','diagnostic']:
        run(ctrl='1f',fail=fail,writes=0)
print(f'PASS: {cases} SWOUT1 command-model cases; all 256 control/voltage states; one permitted bit3 write or idempotent no-write; CRC/identity/voltage/readback/failure gates; NO HARDWARE CLAIM')
print('PRIMARY',primary.name,hashlib.sha256(primary.read_bytes()).hexdigest(),header.name,hashlib.sha256(header.read_bytes()).hexdigest())
