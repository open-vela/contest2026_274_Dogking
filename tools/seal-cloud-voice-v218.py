#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Record verified SOFTWARE evidence only; no API/board/image acceptance claim."""
from pathlib import Path
import argparse,hashlib,json,shutil,subprocess
r=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
for k in ('build','build_report','full_report','output'):p.add_argument('--'+k.replace('_','-'),type=Path,required=True)
p.add_argument('--firmware-commit',required=True)
a=p.parse_args();commit=subprocess.check_output(['git','rev-parse',a.firmware_commit],cwd=r,text=True).strip()
read=lambda f:json.loads(f.read_text())
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
assert (a.build_report/'wrapper-status.txt').read_text().strip()=='0'
assert read(a.build_report/'sdk-restored.json')==dict(result='SDK_RESTORED_BYTE_EXACT',paths=315)
full=read(a.full_report/'summary.json')
assert full['result']=='V218_CLOUD_VOICE_HOST_ARM_FULL_LINK_PASSED_NOT_CLOUD_OR_BOARD_TESTED'
assert full['firmware_commit']==commit
reports={
 'protocol-provider':r/'out/cloud-voice-v218-host-r4/summary.json',
 'TLS-WebSocket':r/'out/cloud-voice-v218-socket-r4/summary.json',
 'Edge-HTTPS-handler':r/'out/cloud-voice-v218-edge-r2/summary.json',
 'ARM-units':r/'out/cloud-voice-v218-arm-r3/summary.json'}
proof={}
for name,f in reports.items():
    v=read(f);assert 'PASSED' in v['result'] or name=='ARM-units'
    proof[name]=dict(path=str(f.relative_to(r)),sha256=sha(f),result=v['result'])
overlay=r/'board/a733-cubie-a7z/openvela-overlay'
for name,v in read(reports['ARM-units'])['units'].items():
    assert sha(overlay/name)==v['sha256'],('ARM source changed',name)
    assert v['max_frame']<=4096
sdk=(r/'../../quickly-openvela').resolve()
names=subprocess.check_output([str(sdk/'prebuilts/gcc/linux-x86_64/aarch64-none-elf/bin/aarch64-none-elf-nm'),
    '-u',str(r/'out/cloud-voice-v218-arm-r3/cloud_voice_socket.cxx.o')],text=True)
assert any(line.strip()=='U getrandom' for line in names.splitlines()) and '_Z9getrandom' not in names
regression={}
for file,marker in [('setup.log','Ctrl+C byte/signal'),('audio.log','openvela-boundary: passed'),
                    ('service.log','PASS PRODUCT SERVICE'),('product.log','PASS actual runtime')]:
    folder='cloud-voice-v218-regression-r2' if file=='setup.log' else 'cloud-voice-v218-regression-r1'
    f=r/'out'/folder/file
    assert marker in f.read_text();regression[file]=dict(path=str(f.relative_to(r)),sha256=sha(f))
for name,digest in full['build_artifact_sha256'].items():assert sha(a.build/name)==digest
a.output.mkdir(parents=True)
def retain(source,name,digest):
    target=a.output/name
    shutil.copy2(source,target)
    assert sha(target)==digest,('evidence copy changed',name)
for name,f in reports.items():
    item=proof[name];item['evidence_file']=name+'.json'
    retain(f,item['evidence_file'],item['sha256'])
for name,item in regression.items():
    item['evidence_file']='suite-'+name
    retain(r/item['path'],item['evidence_file'],item['sha256'])
full_digest=sha(a.full_report/'summary.json')
retain(a.full_report/'summary.json','full-gate.json',full_digest)
retain(a.build_report/'sdk-restored.json','sdk-restored.json',sha(a.build_report/'sdk-restored.json'))
result=dict(result='V218_CLOUD_VOICE_SOFTWARE_VERIFIED_NOT_CLOUD_OR_BOARD_ACCEPTED',
 firmware_commit=commit,base_preserved_tag='a733-v1.0-v217-audited-20261031',
 providers=dict(asr=['volc','xfyun','tencent'],tts=['volc','xfyun','tencent','edge']),
 edge_mode='EXPERIMENTAL_OPTIONAL_HTTPS_COMPANION_NOT_NATIVE_RTOS',
 firmware_directory=str(a.build.resolve()),firmware_artifact_sha256=full['build_artifact_sha256'],
 full_gate=dict(path=str(a.full_report/'summary.json'),sha256=full_digest,evidence_file='full-gate.json'),
 proofs=proof,existing_regression=regression,entropy_c_abi_checked=True,
 sdk_restored_byte_exact=True,cloud_api_executed=False,real_edge_synthesis_tested=False,
 real_mp3_decode_tested=False,board_tested=False,new_SD_image_created=False,
 boot_or_recording_or_motion_auto_enabled=False,PR_created=False,git_pushed=False)
(a.output/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(dict(result=result['result'],firmware_commit=commit,output=str(a.output)),indent=2))
