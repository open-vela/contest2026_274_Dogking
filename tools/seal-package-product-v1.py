#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Seal a fully checked v1.0 candidate; no physical disks, no hardware claims."""
from pathlib import Path
import argparse,json,hashlib,shutil,subprocess,sys,re,os
r=Path(__file__).resolve().parents[1];p=argparse.ArgumentParser(description=__doc__)
for key in ('build','voice_gates','full_gates'):p.add_argument('--'+key.replace('_','-'),type=Path,required=True)
p.add_argument('--firmware-commit',required=True)
p.add_argument('--attempt',default='r1')
p.add_argument('--image-name',default='openvela-a733-cubie-a7z-sd-v1.0-v217-product-candidate.img')
p.add_argument('--evidence-name',default='product-v1')
p.add_argument('--recovery-label',default='a733-v1-product-recovery-20261031')
p.add_argument('--build-report',default='out/product-v1-build-r4')
a=p.parse_args()
assert re.fullmatch('[0-9a-f]{40}',a.firmware_commit) and re.fullmatch('r[1-9][0-9]*',a.attempt)
assert re.fullmatch('openvela-a733-cubie-a7z-sd-v1[.]0-v217-product-(candidate|audited)[.]img',a.image_name)
assert a.evidence_name in ('product-v1','product-v1-audit')
assert re.fullmatch('[a-z0-9-]{1,64}',a.recovery_label)
model=Path(os.environ['A733_PRODUCT_LLM']).resolve(strict=True)
assert model.is_file() and model.stat().st_size==688065920
seal=r/('out/v217-product-release-20261031-'+a.attempt);report=r/('out/product-v1-package-'+a.attempt)
image=r.parent/a.image_name;evidence=r/'docs/evidence'/a.evidence_name
assert not any(f.exists() for f in (seal,report,image,evidence,Path(str(image)+'.sha256')))
voice=json.loads(a.voice_gates.read_text());full=json.loads(a.full_gates.read_text())
assert voice['result']=='V217_VOICE_HOST_ARM_PASSED_NOT_BOARD' and len(voice['tests'])==23
assert full['result']=='V217_VOICE_CAMERA_HOST_ARM_FULL_LINK_PASSED_NOT_BOARD_TESTED' and full['firmware_commit']==a.firmware_commit
def sha(f):
    h=hashlib.sha256()
    with f.open('rb') as stream:
        for b in iter(lambda:stream.read(8*1024*1024),b''):h.update(b)
    return h.hexdigest()
config=(a.build/'.config').read_text();assert 'CONFIG_A733_PRODUCT_V1=y' in config
seal.mkdir()
for name,digest in full['build_artifact_sha256'].items():
    assert sha(a.build/name)==digest;shutil.copyfile(a.build/name,seal/name)
for name in ('imx214-1080p.a7cm','imx214-reference.c','SOURCE-LICENSE.txt'):
    shutil.copyfile(r/'out/v216-release-20261030'/name,seal/name)
subprocess.run([sys.executable,str(r/'tools/package-a733-product-v1.py'),str(seal),str(image),str(report),
 '--firmware-commit',a.firmware_commit,'--voice-gates',str(a.voice_gates),'--full-gates',str(a.full_gates)],cwd=r,check=True)
for name in ('camera-v217.cmd','camera-v217.scr'):shutil.copyfile(report/'assets'/name,seal/name)
shutil.copyfile(report/'assets/manifest.json',seal/'camera-v217-manifest.json')
evidence.mkdir()
for source,name in ((a.voice_gates,'host-arm-summary.json'),(a.full_gates,'full-link-summary.json'),(report/'summary.json','package-summary.json')):
    shutil.copyfile(source,evidence/name)
sources_unchanged={}
for name in ('apps/system/aipet/local_llm.cxx','apps/system/aipetllm/aipetllm_api.cxx','apps/system/aipetllm/aipetllm_sequence.h'):
    relative='board/a733-cubie-a7z/openvela-overlay/'+name
    old=subprocess.check_output(['git','show','1b8a7fc7cce886b752dcb0e236d35e3f318095a3:'+relative],cwd=r)
    now=(r/relative).read_bytes();assert old.replace(b'\r\n',b'\n')==now.replace(b'\r\n',b'\n')
    sources_unchanged[relative]=hashlib.sha256(now).hexdigest()
result=dict(result='PRODUCT_V1_SOFTWARE_IMAGE_VERIFIED_NOT_BOARD_ACCEPTED',firmware_commit=a.firmware_commit,
 release_identity='v217-audit2' if a.evidence_name=='product-v1-audit' else 'v217',
 sealed_directory=str(seal.relative_to(r)),package_directory=str(report.relative_to(r)),
 voice_gate_directory=str(a.voice_gates.parent),full_gate_directory=str(a.full_gates.parent),build_report_directory=a.build_report,
 image=image.name,bytes=image.stat().st_size,sha256=sha(image),kernel_sha256=sha(seal/'nuttx.bin'),
 software_gates=23,product_arm_units=9,inherited_voice_arm_units=18,
 inherited_real_GGUF_proof='out/llm-quality-v216-r4/summary.json',real_GGUF_sources_unchanged=sources_unchanged,
 local_voice_hardware_accepted=False,cloud_api_executed=False,camera_hardware_accepted=False,mcu_hardware_accepted=False,
 physical_audio_onset_measured=False,latency_3_to_5_seconds_accepted=False,automatic_motion_arming=False,
 full_duplex=False,boot_recording=False,physical_disks_written=False,ufs_written=False,
 recovery_directory='out/checkpoints/'+a.recovery_label)
(evidence/'summary.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
