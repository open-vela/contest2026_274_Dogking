#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Seal v221 only after full link, 28 gates, SDK restoration and image readback."""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess,sys
r=Path(__file__).resolve().parents[1];p=argparse.ArgumentParser(description=__doc__)
for name in ('build','build_report','voice_report','full_report'):
    p.add_argument('--'+name.replace('_','-'),type=Path,required=True)
p.add_argument('--firmware-commit',required=True);a=p.parse_args()
for name in ('build','build_report','voice_report','full_report'):
    setattr(a,name,getattr(a,name).resolve(strict=True))
commit=subprocess.check_output(['git','rev-parse',a.firmware_commit],cwd=r,text=True).strip()
def sha(f):
    h=hashlib.sha256()
    with f.open('rb') as stream:
        for b in iter(lambda:stream.read(8*1024*1024),b''):h.update(b)
    return h.hexdigest()
read=lambda f:json.loads(f.read_text())
assert (a.build_report/'wrapper-status.txt').read_text().strip()=='0'
assert read(a.build_report/'sdk-restored.json')==dict(result='SDK_RESTORED_BYTE_EXACT',paths=316)
voice=read(a.voice_report/'summary.json');full=read(a.full_report/'summary.json')
assert voice['result']=='V221_VOICE_HOST_ARM_PASSED_NOT_BOARD' and len(voice['tests'])==28
assert full['result']=='V221_VOICE_CAMERA_HOST_ARM_FULL_LINK_PASSED_NOT_BOARD_TESTED' and full['firmware_commit']==commit
seal=r/'out/v221-product-v12fix-release-r1';report=r/'out/product-v12fix-v221-package-r1'
evidence=r/'docs/evidence/product-v12fix-v221-image'
image=r.parent/'openvela-a733-cubie-a7z-sd-v1.2fix-v221-preview.img'
assert not any(f.exists() for f in (seal,report,evidence,image,Path(str(image)+'.sha256')))
assert 'CONFIG_A733_PRODUCT_V12=y' in (a.build/'.config').read_text()
binary=(a.build/'nuttx.bin').read_bytes()
for label in (b'desktop pet v1.2fix PREVIEW (v221;',b'voice-service-v1.2fix-preview(v221)',
              b'mcu-v221:',b'config-get',b'stepper-stat',b'link-stat',b'mic-monitor-v219',b'M2 submitted to USB'):
    assert label in binary,label
old_images={
 'openvela-a733-cubie-a7z-sd-v1.1-v219-candidate.img':'fe7a28ce9996e3344a1866610c7cdb9137a1088965b0207161df6ffab4c3359f',
 'openvela-a733-cubie-a7z-sd-v1.2-v220-preview.img':'89a6d7f7d508427da0b4f193566c49d958451cc52c8e628a680f32a1062200a1'}
for name,digest in old_images.items():
    assert (r.parent/name).stat().st_size==2147483648 and sha(r.parent/name)==digest
assert Path(os.environ['A733_PRODUCT_LLM']).is_file()
seal.mkdir()
for name,digest in full['build_artifact_sha256'].items():
    assert sha(a.build/name)==digest;shutil.copyfile(a.build/name,seal/name)
for name in ('imx214-1080p.a7cm','imx214-reference.c','SOURCE-LICENSE.txt'):
    shutil.copyfile(r/'out/v216-release-20261030'/name,seal/name)
subprocess.run([sys.executable,str(r/'tools/package-product-v12fix-v221.py'),str(seal),str(image),str(report),
 '--firmware-commit',commit,'--voice-gates',str(a.voice_report/'summary.json'),'--full-gates',str(a.full_report/'summary.json')],cwd=r,check=True)
package=read(report/'summary.json')
assert package['result']=='V221_RAM_LIVE_IMAGE_PACKAGED_NOT_BOARD_TESTED'
assert image.stat().st_size==2147483648 and sha(image)==package['sha256']
for name,digest in old_images.items():assert sha(r.parent/name)==digest
for name in ('camera-v221.cmd','camera-v221.scr','manifest.json'):
    shutil.copyfile(report/'assets'/name,seal/name)
evidence.mkdir()
for source,name in ((a.voice_report/'summary.json','host-arm-summary.json'),(a.full_report/'summary.json','full-link-summary.json'),
 (report/'summary.json','package-summary.json'),(a.full_report/'elf-audit.json','elf-audit.json'),
 (a.build_report/'sdk-restored.json','sdk-restored.json'),(a.voice_report/'mcu-v221/summary.json','mcu-summary.json')):
    shutil.copyfile(source,evidence/name)
for name in ('resident-lease-v219','runtime-lease-v219','wifi-v219','product-service','preview-profile-v220','mcu-v221'):
    shutil.copyfile(a.voice_report/(name+'.log'),evidence/(name+'.log'))
for name in ('summary.json','run.log'):
    shutil.copyfile(r/'out/llm-quality-v220-r1'/name,evidence/('inherited-real-gguf-'+name))
result=dict(result='V221_FULL_SOFTWARE_IMAGE_VERIFIED_NOT_BOARD_TESTED',firmware_commit=commit,
 image=image.name,bytes=image.stat().st_size,sha256=sha(image),kernel_sha256=sha(seal/'nuttx.bin'),
 sealed_directory=str(seal.relative_to(r)),package_directory=str(report.relative_to(r)),
 build_report_directory=str(a.build_report.relative_to(r)),voice_gate_directory=str(a.voice_report.relative_to(r)),
 full_gate_directory=str(a.full_report.relative_to(r)),software_gates=28,SDK_restored_paths=316,
 old_images_preserved=old_images,default_local_history_pairs=0,MCU_upstream='efdb437948eda62cbc782692eb8fd291e5d914ae',
 LLM_oracle_inherited_exact_math_banner_only_verified=True,physical_disks_written=False,ufs_written=False,
 board_tested=False,latency_accepted=False,automatic_motion_arming=False,PR_created=False,git_pushed=False)
(evidence/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2),flush=True)
