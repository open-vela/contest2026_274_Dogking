#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Independent primary-header facts and archived-v195 hardware-model replay."""
from pathlib import Path
import os,re,subprocess,tempfile
repo=Path(__file__).resolve().parents[1]
chip=repo/'board/a733-cubie-a7z/openvela-overlay/vendor/allwinnertech/chips/a733'
header=repo.parent/'bsp-product-aiot-stable/drivers/vin/vin-mipi/combo_csi/combo_csi_reg_i.h'
text=header.read_text()
def define(name):
    return int(re.search(r'^#define\s+'+name+r'\s+(0x[0-9a-fA-F]+|[0-9]+)\s*$',text,re.M)[1],0)
assert define('CMB_PHY_DESKEW0_OFF')==0x14
assert define('CMB_PHY_DESKEW1_OFF')==0x18
assert define('CMB_PHY_DESKEW_LANECK1_SET')==20
assert define('CMB_PHYA_TRESCAL_RESULT')==16
assert define('CMB_PHYA_TRESCAL_FLAG')==3
vendor_c=(header.parent/'combo_csi_reg.c').read_text()
vendor_io=(header.parents[2]/'utility/vin_io.h').read_text()
# The BSP passes a BIT NUMBER, not RESULT_MASK, to this masked helper.
# Check the effective behavior instead of inventing a new RESULT override.
assert re.search(r'CMB_PHYA_TRESCAL_RESULT,\s*val\s*<<\s*CMB_PHYA_TRESCAL_RESULT',vendor_c)
assert '(set_bits & clr_bits)' in vendor_io
assert ((0xa<<define('CMB_PHYA_TRESCAL_RESULT'))&define('CMB_PHYA_TRESCAL_RESULT'))==0
assert ((0xa<<define('CMB_PHYA_TRESCAL_SET'))&(0x1f<<define('CMB_PHYA_TRESCAL_SET')))==0xa0
with tempfile.TemporaryDirectory(prefix='a733-v195-replay-') as tmp:
    source=Path(tmp)/'legacy.c'
    source.write_bytes(subprocess.check_output(['git','show',
      '92363e6574d9f3eb54a0fbf08488f0cd05a3d940:'+str((chip/'a733_mipi_rx.c').relative_to(repo))],cwd=repo))
    exe=Path(tmp)/'test'
    subprocess.run(['gcc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror',
        '-fno-pie','-no-pie','-fsanitize=address,undefined','-I'+str(chip),
        str(source),str(repo/'tools/test-a733-mipi-rx.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe),'legacy'],check=True,timeout=30,
                   env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1'})
print('PASS: BSP primary-header offsets and effective SET=0xA/no RESULT seed verified; legacy model reproduces the reported signature; fixed implementation tested separately')
