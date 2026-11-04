#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Retain old images; seal a NEW v226 regular image after all 40 gates."""
from pathlib import Path
import sys
resume='--resume-verified-package' in sys.argv
if resume:sys.argv.remove('--resume-verified-package')
p=Path(__file__).with_name('seal-product-v20pre-v225.py');s=p.read_text()
s=s.replace('v225','v226').replace('V225','V226').replace('v20pre','v21pre').replace('v2.0pre','v2.1pre')
s=s.replace("len(voice['tests'])==36","len(voice['tests'])==40").replace('software_gates=36','software_gates=40')
for label in ('mcu-desktop','product-setting','product-boot'):
 s=s.replace("b'"+label+"-v226:'","b'"+label+"-v225:'")
for name in ('command-route','boot-policy','boot-worker'):
 s=s.replace("'v226-"+name+"'","'v225-"+name+"'")
# Resume is evidence-only: revalidate the completed package and every hash,
# never recreate/overwrite the image or silently accept conflicting evidence.
needle='assert not any(f.exists() for f in (seal,report,evidence,image,Path(str(image)+\'.sha256\')))'
assert needle in s
s=s.replace(needle,"if resume:\n    assert all(f.exists() for f in (seal,report,evidence,image,Path(str(image)+'.sha256')))\nelse:\n    "+needle)
s=s.replace('seal.mkdir()','seal.mkdir(exist_ok=resume)').replace('evidence.mkdir()','evidence.mkdir(exist_ok=resume)')
start=s.index('subprocess.run([sys.executable,str(r/\'tools/package-')
end=s.index('],cwd=r,check=True)',start)+len('],cwd=r,check=True)')
command=s[start:end]
s=s[:start]+"if not resume:\n    "+command+"\nelse:\n    checked=read(report/'summary.json')\n    assert checked['firmware_commit']==commit\n    assert checked['kernel_sha256']==sha(seal/'nuttx.bin')\n    assert checked['voice_gates_sha256']==sha(a.voice_report/'summary.json')\n    assert checked['full_gates_sha256']==sha(a.full_report/'summary.json')\n"+s[end:]
s=s.replace('shutil.copyfile(', 'copy_preserving(')
needle='read=lambda f:json.loads(f.read_text())'
assert needle in s
s=s.replace(needle,"""def copy_preserving(source,destination):
    if destination.exists():
        assert resume and sha(source)==sha(destination),str(destination)
    else:
        shutil.copyfile(source,destination)
"""+needle)
needle='for name,digest in old_images.items():'
at=s.index(needle)
s=s[:at]+"old_images['openvela-a733-cubie-a7z-sd-v2.0pre-v225-candidate.img']='cd4cd64097720b7da15abef1ee7f1e8291c258888a8683590a8c537998ddc51c'\n"+s[at:]
# Save new real recovery/WSS evidence along with every inherited gate.
needle="for name in ('summary.json','run.log'):"
at=s.index(needle)
s=s[:at]+"""for name in ('cloud-recovery-v226','wifi-recovery-v226','actual-wss-v226','cloud-providers-v226'):
    copy_preserving(a.voice_report/(name+'.log'),evidence/(name+'.log'))
for name in ('summary.json',):
    copy_preserving(a.voice_report/'actual-wss'/name,evidence/('actual-wss-'+name))
"""+s[at:]
exec(compile(s,str(p),'exec'))
result.update(recovery_attempts=3,local_hold_ms=120000,resident_cloud_mode='cloud-preferred-with-local-fallback',
              action_replay=False,played_audio_replay=False,resumed_evidence_only=resume)
(evidence/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
print('V226 seal complete; image re-created='+str(not resume),flush=True)
