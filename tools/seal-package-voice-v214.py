#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Seal verified v214 and create a NEW regular image/evidence; no devices/Git writes."""
from pathlib import Path
import argparse,hashlib,json,re,shutil,subprocess,sys
r=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--build',type=Path,required=True)
p.add_argument('--firmware-commit',required=True)
a=p.parse_args();assert re.fullmatch('[0-9a-f]{40}',a.firmware_commit)
build=a.build.resolve(strict=True)
seal=r/'out/v214-release-20261030';package=r/'out/voice-package-v214-r1'
image=r.parent/'openvela-a733-cubie-a7z-sd-v214-voice-resident-candidate.img'
voice=r/'out/voice-regression-v214-r6/summary.json';full=r/'out/voice-full-v214-r1/summary.json'
assert not any(f.exists() for f in (seal,package,image,Path(str(image)+'.sha256')))
v=json.loads(voice.read_text());f=json.loads(full.read_text())
assert v['result']=='V214_VOICE_HOST_ARM_PASSED_NOT_BOARD' and len(v['tests'])==17
assert f['result']=='V214_VOICE_CAMERA_HOST_ARM_FULL_LINK_PASSED_NOT_BOARD_TESTED'
assert f['firmware_commit']==a.firmware_commit and not f['hardware_verified']
def sha(path):
    h=hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda:stream.read(8*1024*1024),b''):h.update(chunk)
    return h.hexdigest()
seal.mkdir()
for name,digest in f['build_artifact_sha256'].items():
    assert sha(build/name)==digest;shutil.copyfile(build/name,seal/name)
for name in ('imx214-1080p.a7cm','imx214-reference.c','SOURCE-LICENSE.txt'):
    shutil.copyfile(r/'out/v213-release-20261030'/name,seal/name)
subprocess.run([sys.executable,str(r/'tools/package-a733-voice-v214.py'),str(seal),str(image),str(package),
    '--firmware-commit',a.firmware_commit,'--voice-gates',str(voice),'--full-gates',str(full)],check=True,cwd=r)
for name in ('camera-v214.cmd','camera-v214.scr'):
    shutil.copyfile(package/'assets'/name,seal/name)
shutil.copyfile(package/'assets/manifest.json',seal/'camera-v214-manifest.json')
evidence=r/'docs/evidence/voice-v214';evidence.mkdir()
for source,name in ((voice,'host-arm-summary.json'),(full,'full-link-summary.json'),
    (package/'summary.json','package-summary.json'),
    (r/'out/voice-build-v214-r2/sdk-restored.json','sdk-restored.json'),
    (r/'out/voice-regression-v214-r6/resident.log','resident-host.log'),
    (r/'out/voice-regression-v214-r6/monitor-driver.log','monitor-driver-host.log')):
    shutil.copyfile(source,evidence/name)
pkg=json.loads((package/'summary.json').read_text())
summary=dict(result='V214_SOFTWARE_IMAGE_VERIFIED_NOT_BOARD_ACCEPTED',firmware_commit=a.firmware_commit,
    image=image.name,bytes=image.stat().st_size,sha256=pkg['sha256'],kernel_sha256=sha(seal/'nuttx.bin'),
    kernel_bytes=(seal/'nuttx.bin').stat().st_size,regressions=17,arm_units=17,
    microphone_hardware_measured=False,physical_audio_onset_measured=False,
    latency_3_to_5_seconds_accepted=False,full_duplex=False,boot_recording=False,
    old_v213_preserved=True,physical_disks_written=False,ufs_written=False,
    sealed_directory=str(seal.relative_to(r)),
    recovery_directory='out/checkpoints/a733-v214-voice-resident-recovery-20261030',
    references={p.name:sha(p) for p in evidence.iterdir()})
(evidence/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
print(json.dumps(summary,indent=2))
