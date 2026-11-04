#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Seal NEW v225 only after full checks. Never overwrite an old image or disk."""
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
    with f.open('rb') as s:
        for b in iter(lambda:s.read(8*1024*1024),b''):h.update(b)
    return h.hexdigest()
read=lambda f:json.loads(f.read_text())
assert (a.build_report/'wrapper-status.txt').read_text().strip()=='0'
assert read(a.build_report/'sdk-restored.json')==dict(result='SDK_RESTORED_BYTE_EXACT',paths=316)
voice=read(a.voice_report/'summary.json');full=read(a.full_report/'summary.json')
assert voice['result']=='V225_VOICE_HOST_ARM_PASSED_NOT_BOARD' and len(voice['tests'])==36
assert full['result']=='V225_VOICE_CAMERA_HOST_ARM_FULL_LINK_PASSED_NOT_BOARD_TESTED' and full['firmware_commit']==commit
seal=r/'out/v225-product-v20pre-release-r1';report=r/'out/product-v20pre-v225-package-r1'
evidence=r/'docs/evidence/product-v20pre-v225-image'
image=r.parent/'openvela-a733-cubie-a7z-sd-v2.0pre-v225-candidate.img'
assert not any(f.exists() for f in (seal,report,evidence,image,Path(str(image)+'.sha256')))
assert 'CONFIG_A733_PRODUCT_V12=y' in (a.build/'.config').read_text()
binary=(a.build/'nuttx.bin').read_bytes()
for label in (b'desktop pet v2.0pre CANDIDATE (v225;',b'voice-service-v2.0pre(v225)',
 b'mcu-desktop-v225:',b'product-setting-v225:',b'product-boot-v225:',b'--pet-boot',
 b'gate-end-v224:',b'resident-ASR-discard-no-final-NPU',b'mic-monitor-v219',b'M2 submitted to USB'):
    assert label in binary,label
old_images={
 'openvela-a733-cubie-a7z-sd-v1.1-v219-candidate.img':'fe7a28ce9996e3344a1866610c7cdb9137a1088965b0207161df6ffab4c3359f',
 'openvela-a733-cubie-a7z-sd-v1.2-v220-preview.img':'89a6d7f7d508427da0b4f193566c49d958451cc52c8e628a680f32a1062200a1',
 'openvela-a733-cubie-a7z-sd-v1.2fix-v221-preview.img':'c07d26842cc0ddaa4a1e16b957c2c06297c94980c1af9c92bd6a303f5d8bac2c',
 'openvela-a733-cubie-a7z-sd-v1.3fix-v223-candidate.img':'43380317ae7958d0d2eb12e61984603d411b05d478fbfa1db8d7ad20b522bb31',
 'openvela-a733-cubie-a7z-sd-v1.3fix2-v224-candidate.img':'30ca81ce9d0aeb846207b0f816603bfcafb389804763a393c4a18186cb0be25b'}
for name,digest in old_images.items():
    assert (r.parent/name).stat().st_size==2147483648 and sha(r.parent/name)==digest
assert Path(os.environ['A733_PRODUCT_LLM']).is_file()
seal.mkdir()
for name,digest in full['build_artifact_sha256'].items():
    assert sha(a.build/name)==digest;shutil.copyfile(a.build/name,seal/name)
for name in ('imx214-1080p.a7cm','imx214-reference.c','SOURCE-LICENSE.txt'):
    shutil.copyfile(r/'out/v216-release-20261030'/name,seal/name)
subprocess.run([sys.executable,str(r/'tools/package-product-v20pre-v225.py'),str(seal),str(image),str(report),
 '--firmware-commit',commit,'--voice-gates',str(a.voice_report/'summary.json'),'--full-gates',str(a.full_report/'summary.json')],cwd=r,check=True)
package=read(report/'summary.json')
assert package['result']=='V225_RAM_LIVE_IMAGE_PACKAGED_NOT_BOARD_TESTED'
assert image.stat().st_size==2147483648 and sha(image)==package['sha256']
for name,digest in old_images.items():assert sha(r.parent/name)==digest
for name in ('camera-v225.cmd','camera-v225.scr','manifest.json'):
    shutil.copyfile(report/'assets'/name,seal/name)
evidence.mkdir()
for source,name in ((a.voice_report/'summary.json','host-arm-summary.json'),(a.full_report/'summary.json','full-link-summary.json'),
 (report/'summary.json','package-summary.json'),(a.full_report/'elf-audit.json','elf-audit.json'),
 (a.build_report/'sdk-restored.json','sdk-restored.json'),(a.voice_report/'mcu-v221/summary.json','mcu-summary.json'),
 (a.voice_report/'voicefix/endpoint.json','old-new-endpoint.json'),
 (a.voice_report/'voicefix/provider.log','duration-boundaries.log'),
 (a.voice_report/'real-acoustic/summary.json','real-acoustic-summary.json'),
 (a.voice_report/'real-acoustic/run.log','real-acoustic.log'),
 (a.voice_report/'voicefix-arm/summary.json','voicefix-arm.json'),
 (a.voice_report/'asr-native-discard-replay/summary.json','asr-native-discard-replay.json'),
 (a.voice_report/'asr-native-discard-replay/run.log','asr-native-discard-replay.log')):
    shutil.copyfile(source,evidence/name)
for name in ('resident-lease-v219','runtime-lease-v219','wifi-v219','product-service','preview-profile-v220','mcu-v221',
 'temporal-endpoint','v225-command-route','v225-boot-policy','v225-boot-worker'):
    shutil.copyfile(a.voice_report/(name+'.log'),evidence/(name+'.log'))
for name in ('summary.json','run.log'):
    shutil.copyfile(r/'out/llm-quality-v220-r1'/name,evidence/('inherited-real-gguf-'+name))
result=dict(result='V225_FULL_SOFTWARE_IMAGE_VERIFIED_NOT_BOARD_TESTED',firmware_commit=commit,
 image=image.name,bytes=image.stat().st_size,sha256=sha(image),kernel_sha256=sha(seal/'nuttx.bin'),
 sealed_directory=str(seal.relative_to(r)),package_directory=str(report.relative_to(r)),
 build_report_directory=str(a.build_report.relative_to(r)),voice_gate_directory=str(a.voice_report.relative_to(r)),
 full_gate_directory=str(a.full_report.relative_to(r)),software_gates=36,SDK_restored_paths=316,
 old_images_preserved=old_images,default_local_history_pairs=0,MCU_upstream='efdb437948eda62cbc782692eb8fd291e5d914ae',
 inherited_LLM_math_sources=voice['inherited_LLM_math_sources'],application_delta=voice['application_delta'],
 physical_disks_written=False,ufs_written=False,board_tested=False,latency_accepted=False,
 boot_listening_default=False,desktop_exact_human_command_consent=True,autonomous_motion=False,
 spoken_numeric_steps_scale=10,voice_move_Hz=300,PR_created=False,git_pushed=False)
(evidence/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2),flush=True)
