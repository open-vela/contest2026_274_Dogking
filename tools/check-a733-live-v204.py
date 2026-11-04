#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Final live host/ARM/full link gates; never execute on a board."""
from pathlib import Path
import argparse,hashlib,io,json,runpy,subprocess,sys
from elftools.elf.elffile import ELFFile
repo=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
for name in ('build','build_report','output'):p.add_argument('--'+name.replace('_','-'),type=Path,required=True)
p.add_argument('--firmware-commit',required=True);a=p.parse_args();a.output.mkdir()
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
assert (a.build_report/'wrapper-status.txt').read_text().strip()=='0'
restore=json.loads((a.build_report/'sdk-restored.json').read_text())
assert restore==dict(result='SDK_RESTORED_BYTE_EXACT',paths=291)
config=(a.build/'.config').read_text()
for flag in ('CONFIG_A733_CAMERA_LIVE_TEST=y','CONFIG_FS_TMPFS=y','CONFIG_BUILD_FLAT=y'):assert flag in config
subprocess.run([sys.executable,str(repo/'tools/check-a733-camera-v199.py'),
  '--build',str(a.build),'--output',str(a.output/'base')],check=True,timeout=900)
subprocess.run([sys.executable,str(repo/'tools/check-a733-live-arm-v204.py'),
  '--build',str(a.build),'--output',str(a.output/'native')],check=True,timeout=180)
tests=['test-a733-live-v204.py','test-a733-live-display-v204.py','test-a733-tone-compat-v204.py','test-a733-live-vfs-v204.py','test-a733-live-log-v204.py','test-a733-camera-v203.py']
for name in tests:
    with (a.output/(name+'.log')).open('x') as log:
        subprocess.run([sys.executable,str(repo/'tools'/name)],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=180)
audit=runpy.run_path(str(repo/'tools/audit-a733-boot-elf.py'))['audit'](a.build/'nuttx')
assert len(audit['constructors'])==24 and int(audit['layout']['_image_end'],16)<0x48000000
elf=ELFFile(io.BytesIO((a.build/'nuttx').read_bytes()));sym=elf.get_section_by_name('.symtab');symbols={}
for name in ('a733_camera_live_command','a733_camera_live_busy','a733_display_live_begin','a733_display_live_frame','a733_display_live_end'):
    s=sym.get_symbol_by_name(name);assert s and s[0]['st_shndx']!='SHN_UNDEF';symbols[name]=hex(s[0]['st_value'])
frame=sym.get_symbol_by_name('frame_buffer')[0]
assert frame['st_value']%4096==0 and frame['st_size']==4194304
assert elf.get_section(frame['st_shndx'])['sh_type']=='SHT_NOBITS' and frame['st_value']+frame['st_size']<0x48000000
files={};record=json.loads((a.output/'native/summary.json').read_text())
for item in record['files']:
    f=repo/item['source'];assert sha(f)==item['sha256']
    assert hashlib.sha256(subprocess.check_output(['git','show',a.firmware_commit+':'+item['source']],cwd=repo)).hexdigest()==item['sha256']
    files[item['source']]=item['sha256']
for name in ('a733_npu_runtime.c',):
    f=repo/'board/a733-cubie-a7z/openvela-overlay/vendor/allwinnertech/chips/a733'/name
    previous=subprocess.check_output(['git','show','121492d:'+str(f.relative_to(repo))],cwd=repo).decode()
    current=f.read_text();start=previous.index('static int a733_rt_validate_path(');end=previous.index('\n}\n',start)+3
    now_start=current.index('static int a733_rt_validate_path(');now_end=current.index('\n}\n',now_start)+3
    assert previous[:start]==current[:now_start] and previous[end:]==current[now_end:]
(a.output/'elf-audit.json').write_text(json.dumps(audit,indent=2)+'\n')
result=dict(result='V204_LIVE_HOST_ARM_FULL_LINK_PASSED_NOT_BOARD_TESTED',firmware_commit=a.firmware_commit,
    firmware_source_sha256=files,build_artifact_sha256={name:sha(a.build/name) for name in ('nuttx','nuttx.bin','nuttx.map','.config')},
    sdk_restore=restore,build_log_sha256=sha(a.build_report/'build.log'),symbols=symbols,
    frame_pool=dict(address=hex(frame['st_value']),bytes=frame['st_size']),runtime_end=audit['layout']['_image_end'],constructors=24,
    tests={name:sha(a.output/(name+'.log')) for name in tests},npu_execution_core_unchanged=True,
    npu_path_extension='two exact /camram-v204 files only; old ABI unchanged',
    hardware_verified=False,continuous_video_qualified=False)
(a.output/'summary.json').write_text(json.dumps(result,indent=2)+'\n');print(result['result'])
