#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""New v216 image, all private-model roundtrips, explicit updated cloud persona."""
from pathlib import Path
p=Path(__file__).with_name('package-a733-voice-v211.py');s=p.read_text()
s=s.replace('v211','v216').replace('V211_','V216_').replace('release=211','release=216')
s=s.replace('def voice_checks(image,report):','def voice_checks(image,report,firmware_commit):')
s=s.replace('fixtures,fat_end=voice_checks(image,report)','fixtures,fat_end=voice_checks(image,report,a.firmware_commit)')
needle="        dest='::/npu/general-asr/checks'"
assert s.count(needle)==1
s=s.replace(needle,"""        soul=repo/'assets/aipet-online/SOUL.md'
        assert subprocess.check_output(['git','show',firmware_commit+':assets/aipet-online/SOUL.md'],cwd=repo)==soul.read_bytes()
        run(['mcopy','-o','-i',fat,soul,'::/ai_agent/config/SOUL.md'])
        check=tmp/'persona';run(['mcopy','-i',fat,'::/ai_agent/config/SOUL.md',check])
        assert old.digest(check)==old.digest(soul)
"""+needle)
s=s.replace('fat_changed_only_for_fixtures=True','fat_changed_only_for_fixtures_and_cloud_persona=True')
exec(compile(s,str(p),'exec'))
