#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Full candidate gates; host/ARM/link evidence is not board performance."""
from pathlib import Path
import argparse,hashlib,io,json,re,runpy,subprocess,sys
from elftools.elf.elffile import ELFFile
r=Path(__file__).resolve().parents[1];p=argparse.ArgumentParser(description=__doc__)
for name in ('build','build_report','output'):p.add_argument('--'+name.replace('_','-'),type=Path,required=True)
p.add_argument('--firmware-commit',required=True);a=p.parse_args();a.output.mkdir()
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
assert (a.build_report/'wrapper-status.txt').read_text().strip()=='0'
restore=json.loads((a.build_report/'sdk-restored.json').read_text())
assert restore==dict(result='SDK_RESTORED_BYTE_EXACT',paths=296)
config=(a.build/'.config').read_text()
for f in ('CONFIG_A733_CAMERA_PERF_TEST=y','CONFIG_A733_CAMERA_LIVE_TEST=y','CONFIG_FS_TMPFS=y','CONFIG_BUILD_FLAT=y'):assert f in config
assert '# CONFIG_PRIORITY_INHERITANCE is not set' in config
subprocess.run([sys.executable,str(r/'tools/check-a733-camera-v199.py'),'--build',str(a.build),'--output',str(a.output/'base')],check=True,timeout=1200)
subprocess.run([sys.executable,str(r/'tools/check-a733-live-arm-v204.py'),'--build',str(a.build),'--output',str(a.output/'native'),'--perf'],check=True,timeout=240)
subprocess.run([sys.executable,str(r/'tools/test-a733-perf-v208.py'),'--output',str(a.output/'host')],check=True,timeout=900)
audit=runpy.run_path(str(r/'tools/audit-a733-boot-elf.py'))['audit'](a.build/'nuttx')
assert len(audit['constructors'])==24 and int(audit['layout']['_image_end'],16)<0x48000000
elf=ELFFile(io.BytesIO((a.build/'nuttx').read_bytes()));sym=elf.get_section_by_name('.symtab');symbols={}
for name in ('a733_camera_live_command','a733_camera_live_busy','a733_display_live_begin','a733_display_live_frame',
             'a733_display_live_present','a733_display_live_end','a733_camera_tone_plan_create','a733_camera_tone_plan_render',
             'a733_camera_tone_plan_destroy','a733_csi_capture_pause','a733_csi_capture_rearm','a733_imx214_cold_trial_session'):
    s=sym.get_symbol_by_name(name);assert s and s[0]['st_shndx']!='SHN_UNDEF';symbols[name]=hex(s[0]['st_value'])
frame=[s for s in sym.get_symbol_by_name('frame_buffer') if s['st_size']==4194304];assert len(frame)==1;frame=frame[0]
assert frame['st_value']%4096==0 and frame['st_value']+frame['st_size']<0x48000000
assert elf.get_section(frame['st_shndx'])['sh_type']=='SHT_NOBITS'
prefix='board/a733-cubie-a7z/openvela-overlay/';sources={};crlf=[]
for f in (r/prefix).rglob('*'):
    if not f.is_file():continue
    name=str(f.relative_to(r));data=f.read_bytes()
    committed=subprocess.check_output(['git','show',a.firmware_commit+':'+name],cwd=r)
    if data!=committed:
        assert f.suffix in ('.c','.h','.cpp','.cxx','.hxx','.inc','.json','.txt','.xml','.cmake','.mk') or f.name in ('Kconfig','Makefile','Make.defs','defconfig')
        assert committed==data.replace(b'\r\n',b'\n');crlf.append(name)
    sources[name]=hashlib.sha256(data).hexdigest()
baseline='a733-v204-camera-live-board-pass-20261029'
hardware={}
for tail in ('a733_npu.c','a733_npu_memory.c','include/a733_npu_internal.h','include/a733_npu_memory.h'):
    name=prefix+'vendor/allwinnertech/chips/a733/'+tail;data=(r/name).read_bytes()
    assert subprocess.check_output(['git','show',baseline+':'+name],cwd=r)==data;hardware[name]=sha(r/name)
abi=prefix+'nuttx/include/nuttx/npu/a733_vip2.h'
old=subprocess.check_output(['git','show',baseline+':'+abi],cwd=r).decode();new=(r/abi).read_text()
for definition in re.findall(r'struct\s+\w+\s*\{.*?\};',old,re.S):assert definition in new
for definition in re.findall(r'^#define A733_.*$',old,re.M):assert definition in new
runtime=prefix+'vendor/allwinnertech/chips/a733/a733_npu_runtime.c'
old=subprocess.check_output(['git','show',baseline+':'+runtime],cwd=r).decode();new=(r/runtime).read_text()
for name in ('a733_rt_validate_path','a733_rt_a7pm_map_page','a733_rt_run_a7pm'):
    pattern=r'static int '+name+r'\([^;]+?\)\s*\{.*?\n\}'
    assert re.search(pattern,old,re.S)[0]==re.search(pattern,new,re.S)[0],name
host=json.loads((a.output/'host/summary.json').read_text())
(a.output/'elf-audit.json').write_text(json.dumps(audit,indent=2)+'\n')
result=dict(result='V208_PERF_HOST_ARM_FULL_LINK_PASSED_NOT_BOARD_TESTED',firmware_commit=a.firmware_commit,
 firmware_source_sha256=sources,working_CRLF_only_files=crlf,
 build_artifact_sha256={n:sha(a.build/n) for n in ('nuttx','nuttx.bin','nuttx.map','.config')},
 sdk_restore=restore,build_log_sha256=sha(a.build_report/'build.log'),symbols=symbols,
 frame_pool=dict(address=hex(frame['st_value']),bytes=frame['st_size']),runtime_end=audit['layout']['_image_end'],constructors=24,
 tests={'host/'+t['name']:t['sha256'] for t in host['tests']},npu_hardware_execution_unchanged=True,
 hardware_source_sha256=hardware,npu_prepared_runtime_modified=True,old_public_ABI_preserved=True,
 sensor_stream='optional, source-on/gated-SCAP, single permanent guarded DMA buffer',
 hardware_verified=False,continuous_video_qualified=False,board_fps_measured=False)
changed=set()
for name in sources:
    base='bc8fc224ef465bd296296b0926739559b44d7db6'
    oldfile=subprocess.run(['git','show',base+':'+name],cwd=r,capture_output=True)
    if oldfile.returncode:changed.add(name);continue
    prior=oldfile.stdout
    if prior.replace(b'\r\n',b'\n')!=(r/name).read_bytes().replace(b'\r\n',b'\n'):changed.add(name)
allowed={prefix+tail for tail in (
 'apps/include/system/a733_crc32_fast.h','apps/include/system/a733_vision.h',
 'apps/system/a733camera/a733_camera_image.c','apps/system/a733camera/a733_camera_live.c',
 'apps/system/a733camera/a733camera_main.c','apps/system/a733camera/a733_camera_pipeline.inc',
 'apps/system/a733camera/CMakeLists.txt','vendor/allwinnertech/chips/a733/CMakeLists.txt',
 'apps/system/a733display/a733display_main.c',
 'vendor/allwinnertech/chips/a733/a733_crc32_fast.h',
 'vendor/allwinnertech/chips/a733/a733_npu_runtime.c',
 'vendor/allwinnertech/chips/a733/a733_npu_live.inc')}
assert changed==allowed,changed^allowed
result.update(v206_production_changes=sorted(changed),voice_apps_and_tensor_crc_unchanged_from_v206=True,
 raw_crc='runtime-arm-crc32-or-portable-slicing8',rgb_pipeline='two owned slots; RAW DMA unchanged',full_input_template_elision=True,fused_output_scan=True,post_run_weights_checked=True,
 pre_run_duplicate_weight_crc=False,thirty_fps_claimed=False)
(a.output/'summary.json').write_text(json.dumps(result,indent=2)+'\n');print(result['result'])
