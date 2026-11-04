#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Full host/ARM/build/sealed-ELF lineage, no hardware execution."""
from pathlib import Path
import argparse,hashlib,io,json,runpy,subprocess,sys
from elftools.elf.elffile import ELFFile
repo=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--build',type=Path,required=True);p.add_argument('--build-report',type=Path,required=True)
p.add_argument('--seal',type=Path,required=True);p.add_argument('--source-commit',required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();a.output.mkdir()
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
subprocess.run([sys.executable,str(repo/'tools/check-a733-camera-v199.py'),'--build',str(a.build),
                '--output',str(a.output/'base')],check=True,timeout=900)
with (a.output/'v203.log').open('x') as log:
    subprocess.run([sys.executable,str(repo/'tools/test-a733-camera-v203.py')],stdout=log,
                   stderr=subprocess.STDOUT,check=True,timeout=180)
assert (a.build_report/'wrapper-status.txt').read_text().strip()=='0'
restore=json.loads((a.build_report/'sdk-restored.json').read_text())
assert restore==dict(result='SDK_RESTORED_BYTE_EXACT',paths=289)
firmware={};objects=[]
for f in (a.output/'base').rglob('summary.json'):
    d=json.loads(f.read_text())
    for key in ('source_sha256','headers','integration'):
        for name,value in d.get(key,{}).items():
            if name.startswith('board/'):
                assert sha(repo/name)==value,('source drift',name)
                firmware[name]=value
    for item in d.get('files',[])+d.get('objects',[]):
        if not isinstance(item,dict) or 'object_sha256' not in item:continue
        src=item['source'];obj=f.parent/(Path(src).stem+'.o')
        assert sha(obj)==item['object_sha256'] and item['maximum_stack_frame']<=4096
        if 'source_sha256' in item:assert sha(repo/src)==item['source_sha256']
        if src.startswith('board/'):firmware[src]=sha(repo/src)
        objects.append(item)
for src,value in firmware.items():
    data=subprocess.check_output(['git','show',a.source_commit+':'+src],cwd=repo)
    assert hashlib.sha256(data).hexdigest()==value,('commit drift',src)
for name in ('nuttx','nuttx.bin','nuttx.map','.config'):assert sha(a.seal/name)==sha(a.build/name)
audit=runpy.run_path(str(repo/'tools/audit-a733-boot-elf.py'))['audit'](a.seal/'nuttx')
assert len(audit['constructors'])==24 and int(audit['layout']['_image_end'],16)<=0x48000000
elf=ELFFile(io.BytesIO((a.seal/'nuttx').read_bytes()))
frame=elf.get_section_by_name('.symtab').get_symbol_by_name('frame_buffer')[0]
assert frame['st_size']==4194304 and frame['st_value']%4096==0
assert elf.get_section(frame['st_shndx'])['sh_type']=='SHT_NOBITS'
assert frame['st_value']+4194304<=int(audit['layout']['_image_end'],16)
(a.output/'elf-audit.json').write_text(json.dumps(audit,indent=2)+'\n')
result=dict(result='CAMERA_V203_HOST_ARM_FULL_BUILD_PASSED_NOT_BOARD_TESTED',
    firmware_source_commit=a.source_commit,firmware_source_sha256=firmware,objects=objects,
    seal_sha256={name:sha(a.seal/name) for name in ('nuttx','nuttx.bin','nuttx.map','.config')},
    sdk_restore=restore,build_log_sha256=sha(a.build_report/'build.log'),
    frame_pool=dict(address=hex(frame['st_value']),bytes=frame['st_size']),
    runtime_end=audit['layout']['_image_end'],constructors=24,hardware_verified=False,
    regression_log_sha256=sha(a.output/'v203.log'))
(a.output/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
print(result['result'])
