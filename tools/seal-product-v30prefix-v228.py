#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Seal a NEW v228 image, retain v227 and all preceding recovery artifacts."""
from pathlib import Path
p=Path(__file__).with_name('seal-product-v30pre-v227.py');s=p.read_text()
s=s.replace('v227','v228').replace('V227','V228')
s=s.replace('v30pre','v30prefix').replace('v3.0pre','v3.0pre-fix')
s=s.replace('==41','==42').replace('software_gates=41','software_gates=42')
s=s.replace("'cloud-policy-prefetch-v228'","'cloud-policy-prefetch-v227','cloud-only-v228'")
needle='exec(compile(s,str(p),\'exec\'))'
assert s.count(needle)==1
s=s.replace(needle,"s=s.replace('voice-service-v3.0pre(v228)', 'voice-service-v3.0pre-fix(v228)')\n"+needle)
needle=" 'openvela-a733-cubie-a7z-sd-v2.0pre-v225-candidate.img'"
assert needle in s
s=s.replace(needle," 'openvela-a733-cubie-a7z-sd-v3.0pre-v227-candidate.img':'e4348914e72bcf282f42755ee3ca5e0d48985cfb62eceeab165d98827673ce64',\n"+needle)
exec(compile(s,str(p),'exec'))
result.update(explicit_cloud_policy='cloud-only-no-local-fallback',auto_fallback_opt_in=True,
 inherited_v227_network_TTS_user_reported_success=True,v228_board_tested=False)
(evidence/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
