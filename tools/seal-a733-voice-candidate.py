#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Archive an already-built voice candidate; never build, flash or contact HW.

Preserves dirty source content as well as the Git revision; does not pretend
that HEAD alone reproduces an uncommitted working tree. Models stay separate.
"""
import argparse,hashlib,json,shutil,subprocess,tarfile
from pathlib import Path
def sha(p):
    value=hashlib.sha256()
    with p.open('rb') as stream:
        while block:=stream.read(1024*1024):value.update(block)
    return value.hexdigest()
p=argparse.ArgumentParser(description=__doc__)
for n in ['build','image','assets','candidate','source_archive','staged_record']:p.add_argument(n,type=Path)
a=p.parse_args();repo=Path(__file__).resolve().parent.parent;overlay=repo/'board/a733-cubie-a7z/openvela-overlay'
if a.candidate.exists() or a.source_archive.exists():raise ValueError('NEW output paths required')
staged=json.loads(a.staged_record.read_text())
if staged['result']!='STAGED_SOURCES_MATCH':raise ValueError('Missing staged-source gate')
for name,digest in staged['files'].items():
    if sha(overlay/name)!=digest:raise ValueError('Source changed since staging: '+name)
if a.image.stat().st_size!=2147483648:raise ValueError('Unexpected SD image length')
a.candidate.mkdir()
for src,dest in [('nuttx.bin','Image'),('nuttx','nuttx.elf'),('.config','build.config'),('System.map','System.map')]:
    shutil.copyfile(a.build/src,a.candidate/dest)
shutil.copyfile(a.staged_record,a.candidate/'staged-sources.json')
paths=[]
for root in ['board/a733-cubie-a7z/openvela-overlay','app/a733home','app/a733storage','patches','tools']:
    for file in (repo/root).rglob('*'):
        if file.is_file() and not file.is_symlink() and not {'.git','__pycache__'}.intersection(file.parts):paths.append(file)
paths.sort();source={str(f.relative_to(repo)):sha(f) for f in paths}
(a.candidate/'source-sha256.json').write_text(json.dumps(source,indent=2,sort_keys=True))
with tarfile.open(a.source_archive,'x:gz') as archive:
    for file in paths:archive.add(file,arcname=str(file.relative_to(repo)))
head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=repo,text=True).strip()
status=subprocess.check_output(['git','status','--porcelain','-uall'],cwd=repo,text=True)
manifest=dict(head=head,dirty=bool(status),source_files=len(paths),source_archive=dict(path=str(a.source_archive.resolve()),sha256=sha(a.source_archive)),
              image=dict(path=str(a.image.resolve()),sha256=sha(a.image),bytes=a.image.stat().st_size),
              private_assets=dict(path=str(a.assets.resolve()),sha256=sha(a.assets)),
              artifacts={f.name:dict(bytes=f.stat().st_size,sha256=sha(f)) for f in a.candidate.iterdir() if f.is_file()},
              hardware_executed=False,boot_passed=False,live_microphone=False,asr=False,end_to_end_voice_assistant=False)
(a.candidate/'manifest.json').write_text(json.dumps(manifest,indent=2))
print(json.dumps(manifest,indent=2))
