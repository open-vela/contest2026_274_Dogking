#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Retain ALL hardware/ABI/ISA/camera gates; allow only named product sources."""
from pathlib import Path
p=Path(__file__).with_name('check-a733-voice-v216.py');s=p.read_text()
extra=('apps/include/system/a733_camera_live.h','apps/system/aipet/CMakeLists.txt',
 'vendor/allwinnertech/chips/a733/Kconfig','apps/system/aipet/mcu_link.cxx','apps/system/aipet/mcu_protocol.cxx',
 *('apps/system/aipet/'+n for n in ('audio_product.inc','product_core.cxx','product_core.hxx','product_runtime.cxx','product_runtime.hxx','product_labels.hxx','product_cloud_config.c')))
assert s.count('extra=(')==1
s=s.replace('extra=(', 'extra=('+''.join(repr(n)+',' for n in extra),1)
s=s.replace('paths=302','paths=309').replace('V216_VOICE_CAMERA_HOST_ARM_FULL_LINK_PASSED_NOT_BOARD_TESTED','V217_VOICE_CAMERA_HOST_ARM_FULL_LINK_PASSED_NOT_BOARD_TESTED')
exec(compile(s,str(p),'exec'))
import subprocess,json,hashlib
snapshot=a.output/'product-snapshot.log'
with snapshot.open('x') as log:
    subprocess.run(['python3',str(r/'tools/test-product-snapshot-v1.py')],cwd=r,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=120)
result=json.loads((a.output/'summary.json').read_text())
assert 'CONFIG_A733_PRODUCT_V1=y' in (a.build/'.config').read_text()
for name in ('a733_camera_identify_start','a733_camera_identify_poll','a733_camera_identify_stop',
             'a733_camera_live_blocked','a733_display_pet_start','a733_display_pet_pause','a733_product_camera_begin'):
    entries=sym.get_symbol_by_name(name);assert entries and entries[0]['st_shndx']!='SHN_UNDEF',name
result.update(product_snapshot_mock_passed=True,product_snapshot_log_sha256=hashlib.sha256(snapshot.read_bytes()).hexdigest(),
              product_integration_enabled=True,cloud_api_executed=False)
(a.output/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
