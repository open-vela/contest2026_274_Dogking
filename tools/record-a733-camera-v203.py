#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Archive already-passed v203 checks, sealed symbols and package lineage only."""
from pathlib import Path
import argparse,hashlib,io,json,shutil,struct,subprocess,zlib
from elftools.elf.elffile import ELFFile
repo=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
for name in ('check','package','build_report','seal','output'):p.add_argument('--'+name.replace('_','-'),type=Path,required=True)
p.add_argument('--windows-sha256',required=True)
a=p.parse_args()
def sha(f):
    h=hashlib.sha256()
    with f.open('rb') as s:
        while b:=s.read(1048576):h.update(b)
    return h.hexdigest()
check=json.loads((a.check/'summary.json').read_text());package=json.loads((a.package/'summary.json').read_text())
assert check['result']=='CAMERA_V203_HOST_ARM_FULL_BUILD_PASSED_NOT_BOARD_TESTED'
assert package['result']=='V203_CAMERA_IMAGE_PACKAGED_NOT_BOARD_TESTED'
assert not check['hardware_verified'] and not package['hardware_verified']
assert not package['physical_disks_written'] and not package['ufs_written'] and package['models_fat_unchanged']
image=Path(package['image']);assert image.is_file() and image.stat().st_size==2147483648
assert sha(image)==package['sha256']==a.windows_sha256.lower()
assert sha(a.seal/'nuttx.bin')==package['kernel_sha256']==check['seal_sha256']['nuttx.bin']
assert (a.build_report/'wrapper-status.txt').read_text().strip()=='0'
assert json.loads((a.build_report/'sdk-restored.json').read_text())==check['sdk_restore']==dict(result='SDK_RESTORED_BYTE_EXACT',paths=289)
assert sha(a.build_report/'build.log')==check['build_log_sha256']
assert sha(a.check/'v203.log')==check['regression_log_sha256']
for name,digest in check['seal_sha256'].items():assert sha(a.seal/name)==digest
for name,digest in check['firmware_source_sha256'].items():
    assert sha(repo/name)==digest
    assert hashlib.sha256(subprocess.check_output(['git','show',check['firmware_source_commit']+':'+name],cwd=repo)).hexdigest()==digest
elf=ELFFile(io.BytesIO((a.seal/'nuttx').read_bytes()));symbols=elf.get_section_by_name('.symtab')
found={}
for name in ('a733_camera_letterbox_tone','a733_camera_tone_valid','frame_readers','capture_generation','repeat_ready','gamma22'):
    symbol=symbols.get_symbol_by_name(name)
    assert symbol and symbol[0]['st_shndx']!='SHN_UNDEF',name
    found[name]=dict(address=hex(symbol[0]['st_value']),bytes=symbol[0]['st_size'])
assert found['gamma22']['bytes']==1024
manifest=json.loads((a.seal/'camera-v203-manifest.json').read_text())
assert manifest==package['boot_assets']
assert manifest['pmic_writes'] and not manifest['pmic_voltage_writes'] and not manifest['runtime_pmic_access']
script=(a.seal/'camera-v203.scr').read_bytes();source=(a.seal/'camera-v203.cmd').read_bytes()
assert script==(a.package/'assets/camera-v203.scr').read_bytes()
assert source==(a.package/'assets/camera-v203.cmd').read_bytes()
assert hashlib.sha256(script).hexdigest()==manifest['script_sha256']
header=struct.unpack('>7I4B32s',script[:64]);assert header[0]==0x27051956
assert zlib.crc32(script[:4]+bytes(4)+script[8:64])==header[1]
assert len(script[64:])==header[3] and zlib.crc32(script[64:])==header[6]
assert script[72:]==source and header[10]==0 and header[9]==6
output=a.output.resolve();assert output.is_relative_to(repo/'docs/evidence') and not output.exists()
output.mkdir(parents=True);copied={}
def copy(src,relative):
    dest=output/relative;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,dest);copied[str(relative)]=sha(dest)
for base,prefix in ((a.check,'host'),(a.package,'package')):
    for src in base.rglob('*'):
        if src.is_file() and src.suffix in ('.json','.log','.cmd','.scr'):copy(src,Path(prefix)/src.relative_to(base))
for name in ('wrapper-status.txt','sdk-restored.json','build.log'):copy(a.build_report/name,Path('build')/name)
result=dict(result='V203_SEALED_LINEAGE_AND_IMAGE_VERIFIED_NOT_BOARD_TESTED',image=image.name,
    sha256=package['sha256'],bytes=image.stat().st_size,windows_sha256=a.windows_sha256.lower(),
    firmware_commit=subprocess.check_output(['git','rev-parse',check['firmware_source_commit']],cwd=repo,text=True).strip(),
    package_commit=package['source_commit'],symbols=found,archive_sha256=copied,
    seal_sha256={f.name:sha(f) for f in a.seal.iterdir() if f.is_file()},
    hardware_verified=False,private_camera_images_archived=False)
(output/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
print(result['result'],len(copied),'evidence files')
