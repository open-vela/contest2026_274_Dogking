#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""New v229 candidate only; retain v228 and every stable/recovery image."""
from pathlib import Path
p=Path(__file__).with_name('seal-product-v30pre-v227.py');s=p.read_text()
s=s.replace('v227','v229').replace('V227','V229').replace('v30pre','v30prefix2').replace('v3.0pre','v3.0pre-fix2')
s=s.replace('==41','==44').replace('software_gates=41','software_gates=44').replace('paths=319','paths=320').replace('SDK_restored_paths=319','SDK_restored_paths=320')
s=s.replace("'cloud-policy-prefetch-v229'","'cloud-policy-prefetch-v227','cloud-only-v228','dns-close-v229','actual-rounds-v229'")
needle="exec(compile(s,str(p),'exec'))";assert s.count(needle)==1
s=s.replace(needle,"s=s.replace('voice-service-v3.0pre(v229)', 'voice-service-v3.0pre-fix2(v229)')\n"+needle)
needle=" 'openvela-a733-cubie-a7z-sd-v2.0pre-v225-candidate.img'";assert needle in s
s=s.replace(needle," 'openvela-a733-cubie-a7z-sd-v3.0pre-v227-candidate.img':'e4348914e72bcf282f42755ee3ca5e0d48985cfb62eceeab165d98827673ce64',\n 'openvela-a733-cubie-a7z-sd-v3.0pre-fix-v228-candidate.img':'fce43893a2cfe0b40160dbf83017fa17b77c0bd179dc4f0793c183399332e09c',\n"+needle)
exec(compile(s,str(p),'exec'))
shutil.copyfile(a.voice_report/'actual-rounds-v229/summary.json',evidence/'actual-rounds-v229-summary.json')
result.update(explicit_cloud_policy='cloud-only-no-local-fallback',auto_fallback_opt_in=True,DNS_IPv4_candidates=4,
              bounded_protocol_shutdown_ms=500,board_tested=False,board_root_cause_proven=False)
(evidence/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
