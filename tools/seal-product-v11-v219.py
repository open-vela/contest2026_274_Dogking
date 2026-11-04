#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Seal a checked v1.1 candidate and NEW regular SD image; NOT board accepted."""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess,sys
r=Path(__file__).resolve().parents[1];p=argparse.ArgumentParser(description=__doc__)
for name in ('build','build_report','voice_report','full_report'):p.add_argument('--'+name.replace('_','-'),type=Path,required=True)
p.add_argument('--firmware-commit',required=True);a=p.parse_args()
for name in ('build','build_report','voice_report','full_report'):
    setattr(a,name,getattr(a,name).resolve(strict=True))
commit=subprocess.check_output(['git','rev-parse',a.firmware_commit],cwd=r,text=True).strip()
read=lambda f:json.loads(f.read_text())
def sha(f):
    h=hashlib.sha256()
    with f.open('rb') as stream:
        for b in iter(lambda:stream.read(8*1024*1024),b''):h.update(b)
    return h.hexdigest()
assert (a.build_report/'wrapper-status.txt').read_text().strip()=='0'
assert read(a.build_report/'sdk-restored.json')==dict(result='SDK_RESTORED_BYTE_EXACT',paths=316)
voice=read(a.voice_report/'summary.json');full=read(a.full_report/'summary.json')
assert voice['result']=='V219_VOICE_HOST_ARM_PASSED_NOT_BOARD' and len(voice['tests'])==26
assert full['result']=='V219_VOICE_CAMERA_HOST_ARM_FULL_LINK_PASSED_NOT_BOARD_TESTED' and full['firmware_commit']==commit
seal=r/'out/v219-product-v11-release-r1';report=r/'out/product-v11-v219-package-r1'
evidence=r/'docs/evidence/product-v11-v219';image=r.parent/'openvela-a733-cubie-a7z-sd-v1.1-v219-candidate.img'
assert not any(f.exists() for f in (seal,report,evidence,image,Path(str(image)+'.sha256')))
assert 'CONFIG_A733_PRODUCT_V1=y' in (a.build/'.config').read_text()
binary=(a.build/'nuttx.bin').read_bytes()
for label in (b'desktop pet v1.1 CANDIDATE (v219)',b'voice-service-v1.1(v219)',b'mic-monitor-v219',b'M2 submitted to USB'):
    assert label in binary,label
old=r.parent/'openvela-a733-cubie-a7z-sd-v1.0-v217-product-audited.img'
old_sha='98daee1f3b4653d3d65a14a3e92c2713560aac2fd6183946d2b06b7a3c79bc21'
assert old.stat().st_size==2147483648 and sha(old)==old_sha
assert Path(os.environ['A733_PRODUCT_LLM']).is_file()
seal.mkdir()
for name,digest in full['build_artifact_sha256'].items():
    assert sha(a.build/name)==digest;shutil.copyfile(a.build/name,seal/name)
for name in ('imx214-1080p.a7cm','imx214-reference.c','SOURCE-LICENSE.txt'):
    shutil.copyfile(r/'out/v216-release-20261030'/name,seal/name)
subprocess.run([sys.executable,str(r/'tools/package-product-v11-v219.py'),str(seal),str(image),str(report),
 '--firmware-commit',commit,'--voice-gates',str(a.voice_report/'summary.json'),'--full-gates',str(a.full_report/'summary.json')],cwd=r,check=True)
for name in ('camera-v219.cmd','camera-v219.scr'):
    shutil.copyfile(report/'assets'/name,seal/name)
shutil.copyfile(report/'assets/manifest.json',seal/'camera-v219-manifest.json')
assert sha(old)==old_sha
evidence.mkdir()
for source,name in ((a.voice_report/'summary.json','host-arm-summary.json'),(a.full_report/'summary.json','full-link-summary.json'),(report/'summary.json','package-summary.json')):
    shutil.copyfile(source,evidence/name)
shutil.copyfile(a.build_report/'sdk-restored.json',evidence/'sdk-restored.json')
shutil.copyfile(a.full_report/'elf-audit.json',evidence/'elf-audit.json')
logs={}
for name in ('resident-lease-v219','runtime-lease-v219','wifi-v219','product-service'):
    source=a.voice_report/(name+'.log');assert source.is_file()
    shutil.copyfile(source,evidence/(name+'.log'));logs[name]=sha(source)
unchanged={}
for tail in ('apps/system/aipet/local_llm.cxx','apps/system/aipetllm/aipetllm_api.cxx','apps/system/aipetllm/aipetllm_sequence.h'):
    path='board/a733-cubie-a7z/openvela-overlay/'+tail
    old_source=subprocess.check_output(['git','show','8c5a7b3b267128b7859c37d1e1000ec9054732f4:'+path],cwd=r)
    assert old_source.replace(b'\r\n',b'\n')==(r/path).read_bytes().replace(b'\r\n',b'\n')
    unchanged[path]=sha(r/path)
result=dict(result='PRODUCT_V11_V219_SOFTWARE_IMAGE_VERIFIED_NOT_BOARD_ACCEPTED',firmware_commit=commit,
 image=image.name,bytes=image.stat().st_size,sha256=sha(image),kernel_sha256=sha(seal/'nuttx.bin'),
 sealed_directory=str(seal.relative_to(r)),package_directory=str(report.relative_to(r)),
 build_report_directory=str(a.build_report.relative_to(r)),voice_gate_directory=str(a.voice_report.relative_to(r)),full_gate_directory=str(a.full_report.relative_to(r)),
 recovery_directory='out/checkpoints/a733-v11-v219-recovery-20261031',
 base_preserved=dict(tag='a733-v1.0-v217-audited-20261031',image=old.name,sha256=old_sha),
 software_gates=26,sdk_restored_byte_exact_paths=316,proof_log_sha256=logs,LLM_sources_unchanged=unchanged,
 source_feedback_sha256='cdd010dd448ab95d3013b344ae252d13b37bbe1198d96a57bfcbc1842c1bcc31',
 source_feedback_contains_private_network_names=True,raw_feedback_published=False,
 resident_lease_baseline_reproduced=True,real_RuntimePorts_regression=True,
 wifi_real_AP_tested=False,voice_hardware_accepted=False,camera_new_image_board_tested=False,
 cloud_API_executed=False,physical_audio_onset_measured=False,latency_3_to_5_seconds_accepted=False,
 system_scheduling_changed=False,automatic_motion_arming=False,boot_recording=False,
 physical_disks_written=False,ufs_written=False,PR_created=False,git_pushed=False)
(evidence/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(dict(result=result['result'],image=str(image),sha256=result['sha256']),indent=2))
