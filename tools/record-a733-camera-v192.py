#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Check final source/object/build/image lineage. NEVER camera acceptance."""
import argparse
import hashlib
import json
from pathlib import Path
import runpy
import shutil
import subprocess
import sys

repo=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
for name in ('check','build_report','sdk_build','package_report','new_directory'):
    p.add_argument(name,type=Path)
p.add_argument('--source-commit',required=True)
p.add_argument('--release',type=int,choices=(192,193,194,195,196,197,198,199),default=192)
a=p.parse_args()
if not __debug__:raise RuntimeError('Assertions required; do not run with -O')


def sha(path):
    h=hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda:stream.read(65536),b''):h.update(block)
    return h.hexdigest()


def load(path):return json.loads(path.read_text())


check=a.check
for _ in range(a.release-192):check=check/'base'
baseline=load(check/'base/base/summary.json')
mode=load(check/'base/summary.json')
current=load(check/'summary.json')
source_names=set(current['source_sha256'])|set(mode['source_sha256'])
for field in ('headers','test_sources','integration'):
    source_names.update(baseline[field])
    for name,value in baseline[field].items():assert sha(repo/name)==value,('Stale source',name)
for item in baseline['files']:
    name=item['source'];source_names.add(name)
    assert sha(repo/name)==item['source_sha256']
    assert sha(check/'base/base'/(Path(name).stem+'.o'))==item['object_sha256']
for name,value in mode['source_sha256'].items():assert sha(repo/name)==value,('Stale mode',name)
for name,value in current['source_sha256'].items():assert sha(repo/name)==value,('Stale control',name)
for item in current['files']:
    assert sha(check/(Path(item['source']).stem+'.o'))==item['object_sha256']
    assert item['maximum_stack_frame']<=4096
for folder,logs in ((check/'base/base',baseline['logs']),
                    (check/'base',mode['log_sha256']),(check,current['logs'])):
    for name,value in logs.items():assert sha(folder/name)==value,('Stale log',name)
assert (a.build_report/'wrapper-status.txt').read_text().strip()=='0'
restored=load(a.build_report/'sdk-restored.json')
assert restored['result']=='SDK_RESTORED_BYTE_EXACT'
package=load(a.package_report/'summary.json')
if a.release>=193:
    if a.release==193:assert package['purpose']=='camera-id' and package['camera_id_experiment_candidate']
    elif a.release==194:assert package['purpose']=='camera-mode' and package['camera_mode_experiment_candidate']
    elif a.release==195:assert package['purpose']=='camera-packets' and package['camera_packet_experiment_candidate']
    elif a.release==196:assert package['purpose']=='camera-register-fix' and package['camera_register_fix_candidate'] and package['camera_packet_experiment_candidate']
    elif a.release==197:assert package['purpose']=='camera-frame' and package['camera_single_frame_candidate']
    elif a.release==198:assert package['purpose']=='camera-vision' and package['camera_single_frame_candidate'] and package['camera_same_input_vision_candidate']
    else:assert package['purpose']=='camera-sizing-fix' and package['camera_auto_sizing_repair_candidate'] and package['camera_same_input_vision_candidate']
    vision_root=a.check/'base' if a.release==199 else a.check
    capture_root=vision_root/'base' if a.release>=198 else a.check
    repair_root=capture_root/'base' if a.release>=197 else a.check
    packet_root=repair_root/'base' if a.release>=196 else a.check
    mode_root=packet_root/'base' if a.release>=195 else a.check
    trial_root=mode_root/'base' if a.release>=194 else a.check
    trial=load(trial_root/'summary.json')
    assert sha(check/'summary.json')==trial['base_summary_sha256']
    assert sha(trial_root/'id-trial.log')==trial['id_trial_log_sha256']
    for name,value in trial['source_sha256'].items():
        assert sha(repo/name)==value,('Stale trial',name)
    source_names.update(trial['source_sha256'])
    if a.release>=194:
        mode_trial=load(mode_root/'summary.json')
        assert sha(trial_root/'summary.json')==mode_trial['base_summary_sha256']
        for name,value in mode_trial['source_sha256'].items():
            assert sha(repo/name)==value,('Stale mode trial',name)
        for name,value in mode_trial['logs'].items():assert sha(mode_root/name)==value
        assert sha(repo.parent.parent/'quickly-openvela/nuttx/crypto/sha2.c')==mode_trial['sha2_reference_sha256']
        source_names.update(mode_trial['source_sha256'])
    if a.release>=195:
        packets=load(packet_root/'summary.json')
        assert packets['result']=='CAMERA_V195_HOST_ARM_PACKET_PATH_PASSED_NOT_HARDWARE'
        assert sha(mode_root/'summary.json')==packets['base_summary_sha256']
        for name,value in packets['source_sha256'].items():assert sha(repo/name)==value
        for name,value in packets['logs'].items():assert sha(packet_root/name)==value
        assert sha(packet_root/'a733_mipi_rx.o')==packets['object_sha256']
        assert packets['maximum_stack_frame']<=4096
        source_names.update(packets['source_sha256'])
    if a.release>=196:
        repair=load(repair_root/'summary.json')
        assert repair['result']=='CAMERA_V196_REGISTER_SEMANTICS_AND_LEGACY_REPLAY_PASSED_NOT_HARDWARE'
        assert sha(packet_root/'summary.json')==repair['base_summary_sha256']
        for name,value in repair['source_sha256'].items():assert sha(repo/name)==value
        for name,value in repair['logs'].items():assert sha(repair_root/name)==value
        assert sha(repo.parent/'bsp-product-aiot-stable/drivers/vin/vin-mipi/combo_csi/combo_csi_reg_i.h')==repair['primary_header_sha256']
        for name,value in repair['primary_reference_sha256'].items():assert sha(repo.parent/name)==value
        source_names.update(repair['source_sha256'])
    if a.release>=197:
        capture=load(capture_root/'summary.json')
        assert capture['result']=='CAMERA_V197_GUARDED_RAW_FRAME_HOST_AND_ARM_PASSED_NOT_HARDWARE'
        assert sha(repair_root/'summary.json')==capture['base_summary_sha256']
        for name,value in capture['source_sha256'].items():assert sha(repo/name)==value
        for name,value in capture['logs'].items():assert sha(capture_root/name)==value
        for name,value in capture['primary_reference_sha256'].items():assert sha(repo.parent/name)==value
        for item in capture['objects']:
            assert sha(capture_root/(Path(item['source']).stem+'.o'))==item['object_sha256']
            assert item['maximum_stack_frame']<=4096
        source_names.update(capture['source_sha256'])
    if a.release>=198:
        vision=load(vision_root/'summary.json')
        assert vision['result']=='CAMERA_V198_SAME_INPUT_VISION_HOST_AND_ARM_PASSED_NOT_HARDWARE'
        assert sha(capture_root/'summary.json')==vision['base_summary_sha256']
        for name,value in vision['source_sha256'].items():assert sha(repo/name)==value
        for name,value in vision['logs'].items():assert sha(vision_root/name)==value
        for item in vision['objects']:
            assert sha(vision_root/(Path(item['source']).stem+'.o'))==item['object_sha256']
            assert item['maximum_stack_frame']<=4096
        assert sha(repo.parent.parent/'quickly-openvela/nuttx/libs/libc/misc/lib_crc32.c')==vision['crc_primary_sha256']
        source_names.update(vision['source_sha256'])
    if a.release==199:
        fixed=load(a.check/'summary.json')
        assert fixed['result']=='CAMERA_V199_AUTO_SIZING_REPAIR_HOST_AND_ARM_PASSED_NOT_HARDWARE'
        assert sha(vision_root/'summary.json')==fixed['base_summary_sha256']
        for name,value in fixed['source_sha256'].items():assert sha(repo/name)==value
        for name,value in fixed['logs'].items():assert sha(a.check/name)==value
        source_names.update(fixed['source_sha256'])
else:
    assert package['purpose']=='camera-control' and package['camera_reset_held_control_candidate']
assert not package['hardware_executed'] and not package['camera_capture']
for name,field in (('nuttx','elf_sha256'),('nuttx.bin','kernel_sha256'),('.config','config_sha256')):
    assert sha(a.sdk_build/name)==package[field]
assert sha(Path(package['image']))==package['sha256']
assert package['camera_handoff']['kernel_sha256']==package['kernel_sha256']
assert not package['camera_handoff']['pmic_writes']
audit=runpy.run_path(str(repo/'tools/audit-a733-boot-elf.py'))['audit'](a.sdk_build/'nuttx')
assert int(audit['layout']['_image_end'],16)<=0x48000000
assert len(audit['constructors'])==24
frame_reservation=None
if a.release>=197:
    import io
    from elftools.elf.elffile import ELFFile
    elf=ELFFile(io.BytesIO((a.sdk_build/'nuttx').read_bytes()))
    symbol=elf.get_section_by_name('.symtab').get_symbol_by_name('frame_buffer')[0]
    start=int(symbol['st_value']);size=int(symbol['st_size'])
    assert size==4*1024*1024 and start%4096==0 and 0x40200000<=start
    assert start+size<=int(audit['layout']['_image_end'],16)<=0x48000000
    assert elf.get_section(symbol['st_shndx'])['sh_type']=='SHT_NOBITS'
    frame_reservation=dict(address=hex(start),bytes=size,identity_map='DRAM_STAGE1',retained_until_reboot=True)
# Firmware source commit is allowed to predate packaging-only tooling/tests.
firmware_names=[name for name in source_names if name.startswith('board/')]
for name in firmware_names:
    data=subprocess.check_output(['git','show',a.source_commit+':'+name],cwd=repo)
    assert hashlib.sha256(data).hexdigest()==sha(repo/name),('Firmware source drift',name)
test=subprocess.run([sys.executable,str(repo/'tools/test-a733-camera-package-v191.py')],
                    capture_output=True,text=True,check=True,timeout=30)
a.new_directory.mkdir()
for name in ('sdk-restored.json','wrapper-status.txt','build.log'):
    shutil.copyfile(a.build_report/name,a.new_directory/name)
for name in ('summary.json','package.log','boot-disassembly.txt'):
    shutil.copyfile(a.package_report/name,a.new_directory/('image-'+name))
(a.new_directory/'elf-audit.json').write_text(json.dumps(audit,indent=2)+'\n')
(a.new_directory/'package-profile-test.log').write_text(test.stdout)
result=dict(result=f'CAMERA_V{a.release}_FULL_LINK_IMAGE_AND_RAM_HANDOFF_PASSED_NOT_HARDWARE',
    firmware_source_commit=a.source_commit,image=package,
    firmware_source_sha256={name:sha(repo/name) for name in sorted(firmware_names)},
    check_sha256=sha(a.check/'summary.json'),build_log_sha256=sha(a.build_report/'build.log'),
    sdk_restore=restored,constructor_count=len(audit['constructors']),
    runtime_image_bytes=int(audit['layout']['_image_end'],16)-0x40200000,
    camera_handoff=package['camera_handoff'],runtime_pmic_access=False,
    reset_released=False,sensor_id_read=False,camera_capture=False,
    explicit_id_experiment_available=a.release>=193,
    explicit_standby_mode_experiment_available=a.release>=194,
    explicit_mipi_packet_experiment_available=a.release>=195,
    explicit_single_frame_experiment_available=a.release>=197,
    frame_reservation=frame_reservation,
    strict_qualification_created=False,
    hardware_verified=False,firmware_retention='ASSUMED_NOT_LIVE_MEASUREMENT',
    v190_voice_board_test='DEFERRED_BY_USER',
    v191_user_board_test='ONE_NSH_BOOT_AND_CPU_SELFTEST_AND_READONLY_BOARD_SNAPSHOT')
(a.new_directory/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
print(result['result'],'runtime=',result['runtime_image_bytes'])
