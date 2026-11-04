#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Same complete ABI/ISA/camera/SDK-restoration checks, new application labels."""
from pathlib import Path
p=Path(__file__).with_name('check-product-full-v227.py');s=p.read_text()
s=s.replace('V227_VOICE_CAMERA_HOST_ARM_FULL_LINK_PASSED_NOT_BOARD_TESTED','V228_VOICE_CAMERA_HOST_ARM_FULL_LINK_PASSED_NOT_BOARD_TESTED')
s=s.replace('desktop pet v3.0pre CANDIDATE (v227;','desktop pet v3.0pre-fix CANDIDATE (v228;')
s=s.replace('voice-service-v3.0pre(v227)','voice-service-v3.0pre-fix(v228)')
exec(compile(s,str(p),'exec'))
for label in (b'cloud-only-v228:',b'waiting-cloud-MIC-OFF',b'product-policy-v228:'):
    assert label in binary,label
report.update(explicit_cloud_policy='cloud-only-no-local-fallback',auto_fallback_opt_in=True)
f.write_text(json.dumps(report,indent=2)+'\n')
