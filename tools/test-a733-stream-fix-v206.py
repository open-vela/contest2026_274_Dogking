#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Replay archived v205 vs current rearm, and pin the vendor status meanings."""
from pathlib import Path
import hashlib,re,subprocess,tempfile
repo=Path(__file__).resolve().parents[1]
chip=repo/'board/a733-cubie-a7z/openvela-overlay/vendor/allwinnertech/chips/a733'
header=(repo.parent/'bsp-product-aiot-stable/drivers/vin/vin-video/dma140/dma140_reg_i.h').read_text()
for name,bit in [('BUF_ADDR_UNDERFLOW_INT_PD',17),('LINE_MISS_INT_PD',21)]:
    assert re.search(r'^#define\s+'+name+r'\s+'+str(bit)+r'\s*$',header,re.M)
assert 0x00220083&0x002f8044==0x00220000
commit='38323b5c81a19b1963059e2dbeca50180179dd53'
relative=(chip/'a733_csi_capture.c').relative_to(repo).as_posix()
with tempfile.TemporaryDirectory(prefix='rearm206-') as raw:
    tmp=Path(raw);old=tmp/'v205.c'
    old.write_bytes(subprocess.check_output(['git','show',commit+':'+relative],cwd=repo))
    print('Archived v205 CSI SHA256:',hashlib.sha256(old.read_bytes()).hexdigest(),flush=True)
    for label,source,args in [('v205',old,['legacy']),('v206',chip/'a733_csi_capture.c',[])]:
        exe=tmp/label
        subprocess.run(['gcc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fno-pie','-no-pie',
          '-fsanitize=address,undefined','-I'+str(chip),str(source),str(chip/'a733_csi_frame_plan.c'),
          str(repo/'tools/test-a733-csi-rearm-v206.c'),'-o',str(exe)],check=True)
        subprocess.run([str(exe),*args],check=True,timeout=30)
print('PASS: software regression only; physical address-latch diagnosis still needs v206 board test')
