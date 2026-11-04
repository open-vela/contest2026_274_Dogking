#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Inherited full hardware/ABI gates, plus bounded cloud transport profile."""
from pathlib import Path
p=Path(__file__).with_name('check-product-full-v227.py');s=p.read_text()
s=s.replace('paths=319','paths=320').replace('SDK_restored_paths=319','SDK_restored_paths=320')
s=s.replace('V227_VOICE_CAMERA_HOST_ARM_FULL_LINK_PASSED_NOT_BOARD_TESTED','V229_VOICE_CAMERA_HOST_ARM_FULL_LINK_PASSED_NOT_BOARD_TESTED')
s=s.replace('desktop pet v3.0pre CANDIDATE (v227;','desktop pet v3.0pre-fix2 CANDIDATE (v229;').replace('voice-service-v3.0pre(v227)','voice-service-v3.0pre-fix2(v229)')
exec(compile(s,str(p),'exec'))
assert 'CONFIG_NETDB_MAX_IPv4ADDR=4' in (a.build/'.config').read_text()
for label in (b'cloud-only-v228:',b'waiting-cloud-MIC-OFF',b'product-policy-v228:',b'cloud-transport-v229:',b'owned-WSS=',b'wire-rx='):
    assert label in binary,label
report.update(explicit_cloud_policy='cloud-only-no-local-fallback',auto_fallback_opt_in=True,DNS_IPv4_candidates=4,bounded_protocol_shutdown_ms=500)
f.write_text(json.dumps(report,indent=2)+'\n')
