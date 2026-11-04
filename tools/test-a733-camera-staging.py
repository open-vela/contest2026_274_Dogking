#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Every source is staged/restored or an explicitly named identical SDK base."""
from pathlib import Path
import re
repo=Path(__file__).resolve().parents[1]
prefix='vendor/allwinnertech/chips/a733/'
overlay=repo/'board/a733-cubie-a7z/openvela-overlay'
cmake=(overlay/prefix/'CMakeLists.txt').read_text()
required=set(re.findall(r'\ba733_[\w]+\.[cS]\b',cmake))
required.update(('a733_mipi_rx.h','a733_imx214_cold.h','a733_imx214_twi3.h','a733_csi_capture.h'))
source=(repo/'tools/build-a733-wapi.sh').read_text()
sdk=repo.parent.parent/'quickly-openvela'
# Existing installed BSP base, not silently skipped arbitrary new files.
resident={'a733_lowputc.S','a733_sdmmc.c','a733_serial.c','a733_trng.c',
          'a733_tsadc.c','a733_watchdog.c'}
def check(text):
    match=re.search(r'^files=\(\n(.*?)^\)',text,re.M|re.S)
    assert match,'Explicit backup/staging inventory missing'
    listed=set(match.group(1).split())
    missing=[name for name in sorted(required-resident) if prefix+name not in listed]
    assert not missing,('CMake source omitted from staging/restore',missing)
    for name in required:assert (overlay/prefix/name).is_file(),name
    for name in resident:
        assert (overlay/prefix/name).read_bytes()==(sdk/prefix/name).read_bytes(),('SDK baseline drift',name)
check(source)
for name in ('a733_mipi_rx.c','a733_mipi_rx.h','a733_csi_capture.c','a733_csi_capture.h'):
    try:check(source.replace('  '+prefix+name+'\n',''))
    except AssertionError:pass
    else:raise AssertionError('Missing receiver escaped the staging guard')
print(f'PASS: {len(required)} A733 source/header files covered, 6 installed BSP bases byte-equal, 4 missing-file regressions rejected; NO SDK WRITES')
