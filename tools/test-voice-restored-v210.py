#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Run restored SOURCE, linking only named immutable private reference trees."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess
r=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--reference-repo',type=Path,required=True)
p.add_argument('--run-label',default='r1')
a=p.parse_args();original=a.reference_repo.resolve(strict=True)
assert original!=r and original.parent==r.parent and re.fullmatch(r'[a-z0-9_-]+',a.run_label)
out=r/'out';out.mkdir(exist_ok=True)
names=('asr-native-v183-r1','general-asr-export-v184-r2','ort-capi-host-v184-r1',
       'matcha-ort-v183-r1','acoustic-capi-host-v184-r2','qemu-deps-v208',
       'a733-kws-wav-v161-r2','a733-openvela-kws-v158-r2','a733-linux-general-asr-v183-r1',
       'imx214-mode-v187-r1','v204-release-20261029')
for name in names:
    source=(original/'out'/name).resolve(strict=True);assert source.is_relative_to(original/'out')
    dest=out/name
    if dest.exists() or dest.is_symlink():assert dest.is_symlink() and dest.resolve(strict=True)==source
    else:dest.symlink_to(source,target_is_directory=True)
build=(r/'../../quickly-openvela/cmake_out/cubie-a7z_nsh_v205_camera_perf').resolve(strict=True)
voice='out/restored-voice-v210-'+a.run_label;camera='out/restored-camera-v210-'+a.run_label
subprocess.run(['python3','tools/test-voice-v210.py',voice,'--build',str(build)],cwd=r,check=True)
subprocess.run(['python3','tools/test-a733-perf-v209.py','--output',camera],cwd=r,check=True)
subprocess.run(['python3','tools/test-voice-latency-v210.py'],cwd=r,check=True)
head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=r,text=True).strip()
saved=[json.loads(f.read_text()) for f in (original/'out/checkpoints').glob('*/manifest.json')]
saved=[m for m in saved if m['head']==head];assert len(saved)==1
for name,item in saved[0]['files'].items():
    assert hashlib.sha256((r/name).read_bytes()).hexdigest()==item['sha256'],name
# Existing Windows records intentionally retain raw CRLF bytes. Linux Git
# may mark their LF-normalized historical blobs modified; never rewrite them
# merely to obtain a cosmetically clean status. Accept only EOL-only deltas
# AND the exact archive byte hashes above, with no new/staged/renamed files.
status=subprocess.check_output(['git','status','--porcelain','-z'],cwd=r)
eol=[]
for entry in filter(None,status.split(b'\0')):
    assert entry[:3]==b' M ',entry
    name=entry[3:].decode()
    blob=subprocess.check_output(['git','show','HEAD:'+name],cwd=r)
    assert (r/name).read_bytes().replace(b'\r\n',b'\n')==blob.replace(b'\r\n',b'\n'),name
    eol.append(name)
subprocess.run(['git','fsck','--full'],cwd=r,check=True)
result=dict(result='V210_RESTORED_SOURCE_VOICE12_CAMERA22_PASSED',
 restored_repo=str(r),reference_only_out_directories=list(names),reference_repo=str(original),
 voice=json.loads((r/voice/'summary.json').read_text())['result'],
 camera=json.loads((r/camera/'summary.json').read_text())['result'],
 git_clean=not status,git_eol_only_paths=eol,source_exact_bytes_verified=len(saved[0]['files']),
 git_fsck_exit=0,hardware_verified=False)
with (out/('restore-v210-receipt-'+a.run_label+'.json')).open('x') as f:f.write(json.dumps(result,indent=2)+'\n')
print(result['result'])
