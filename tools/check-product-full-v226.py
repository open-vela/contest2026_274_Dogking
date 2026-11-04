#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
import subprocess,sys
root=Path(__file__).resolve().parents[1]
frozen=sys.argv[sys.argv.index('--firmware-commit')+1]
# Match this Windows worktree's Git CRLF normalization in WSL as well. This
# does not ignore code whitespace or change repository/global configuration.
assert subprocess.run(['git','-c','core.autocrlf=true','diff','--quiet',frozen,'--','board/a733-cubie-a7z/openvela-overlay'],cwd=root).returncode==0, 'Firmware source freeze changed; use the matching build/source commit'
p=Path(__file__).with_name('check-product-full-v225.py');s=p.read_text()
s=s.replace('V225_VOICE_CAMERA_HOST_ARM_FULL_LINK_PASSED_NOT_BOARD_TESTED','V226_VOICE_CAMERA_HOST_ARM_FULL_LINK_PASSED_NOT_BOARD_TESTED')
s=s.replace('desktop pet v2.0pre CANDIDATE (v225;','desktop pet v2.1pre CANDIDATE (v226;').replace('voice-service-v2.0pre(v225)','voice-service-v2.1pre(v226)')
exec(compile(s,str(p),'exec'))
for label in (b'cloud-recovery-v226:',b'cloud-wss-v226:',b'wifi-recovery-v226:',b'cloud-xfyun-v226:'):
    assert label in binary,label
report.update(recovery_attempts=3,local_hold_ms=120000,
 resident_cloud_mode='cloud-preferred-with-local-fallback',action_replay=False,played_audio_replay=False)
f.write_text(json.dumps(report,indent=2)+'\n')
