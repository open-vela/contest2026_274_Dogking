#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""v200 power guard, NEW v203 firmware, NEW regular-file SD image only."""
from pathlib import Path
import argparse,hashlib,importlib.util,json,shutil,struct,subprocess,sys,tempfile,zlib
REPO=Path(__file__).resolve().parents[1]

def module(name,file):
    spec=importlib.util.spec_from_file_location(name,REPO/'tools'/file)
    m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
old=module('package200','package-a733-swout1-v200.py')
guard=module('guard200','build-a733-swout1-v200.py')

def assets(seal,target):
    with tempfile.TemporaryDirectory(prefix='assets203-') as temp:
        base=Path(temp)/'legacy'
        old.command([sys.executable,REPO/'tools/build-a733-camera-handoff-v192.py',seal,base,
                     '--release','199','--mode-directory',seal])
        manifest=json.loads((base/'manifest.json').read_text())
        source=guard.guarded_source((base/'camera-v199.cmd').read_text(),int(manifest['handoff_loaded_address'],16)).replace('v200','v203')
        script=guard.legacy_image(source)
        header=script[:32]+b'A733 camera v203'.ljust(32,b'\0')
        header=header[:4]+bytes(4)+header[8:]
        script=header[:4]+struct.pack('>I',zlib.crc32(header))+header[8:]+script[64:]
        target.mkdir()
        (target/'camera-v203.cmd').write_text(source,newline='\n')
        (target/'camera-v203.scr').write_bytes(script)
        manifest.update(release=203,kernel_release=203,script_sha256=hashlib.sha256(script).hexdigest(),
                        v200_power_guard_reused=True,hardware_verified=False,
                        pmic_writes=True,pmic_voltage_writes=False,runtime_pmic_access=False,
                        pmic_write_policy='U-Boot only; observed PMU 0x0e=03, voltage 0x12=17; control 0x11 17->1f only; verify readback; already-1f unchanged; all other profiles rejected')
        (target/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
        return manifest

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('seal',type=Path);p.add_argument('new_image',type=Path);p.add_argument('new_report',type=Path)
    a=p.parse_args();image=a.new_image.resolve();report=a.new_report.resolve();seal=a.seal.resolve()
    base=REPO.parent/'openvela-a733-cubie-a7z-sd-v200-swout1-raw-candidate.img'
    if image.exists() or image.parent!=REPO.parent or image.suffix!='.img' or a.new_image.is_symlink():raise ValueError('NEW sibling image required')
    if report.exists() or not report.is_relative_to(REPO/'out'):raise ValueError('NEW out/ report required')
    if not base.is_file() or base.stat().st_size!=2147483648 or old.digest(base)!='a729b7442ffc764063b191d8ac3ebb38c4a2812c18f21cc9dacf2cd02fbaaa33':raise ValueError('Base image mismatch')
    report.mkdir(parents=True)
    manifest=assets(seal,report/'assets')
    kernel=manifest['kernel_sha256'];source_commit=old.command(['git','-C',REPO,'rev-parse','HEAD']).strip()
    with tempfile.TemporaryDirectory(prefix='package203-') as tmp:
        work=Path(tmp);file=work/'root.ext4';old.extract(base,file);root=old.Root(file,work)
        if root.exists(old.ACTIVE) or not root.exists(old.BACKUP):raise ValueError('Base bypasses tested power script')
        if hashlib.sha256(root.read('/boot/openvela/a733/Image')).hexdigest()!=old.KERNEL_SHA:raise ValueError('Old Image mismatch')
        if root.read('/boot/boot.scr')!=(REPO/'out/v200-release-20261028/camera-v200.scr').read_bytes():raise ValueError('Base boot script mismatch')
        for name in guard.MODE_NAMES:
            if root.read('/boot/openvela/a733/'+name)!=(seal/name).read_bytes():raise ValueError('Mode/source changed')
        for name,new,backup in (('/boot/openvela/a733/Image',seal/'nuttx.bin','/boot/openvela/a733/Image.before-v203'),
                                  ('/boot/boot.scr',report/'assets/camera-v203.scr','/boot/boot-v200.scr')):
            original=work/('original-'+str(root.serial));original.write_bytes(root.read(name))
            root.write_new(backup,original)
            # Only these two fixed files in the private regular rootfs copy.
            old.command(['debugfs','-w','-R','rm '+name,file]);root.write_new(name,new)
        for name in ('camera-v203.cmd','camera-v203.scr','manifest.json'):
            root.write_new('/boot/openvela/a733/'+('camera-v203.json' if name=='manifest.json' else name),report/'assets'/name)
        log=old.command(['e2fsck','-fn',file])
        shutil.copyfile(base,image)
        log+=old.command(['dd','if='+str(file),'of='+str(image),'bs=64K','iflag=count_bytes','oflag=seek_bytes',
                          'seek='+str(old.ROOT_BEGIN),'count='+str(old.ROOT_BYTES),'conv=notrunc','status=none'])
        old.extract(image,file)
        if root.read('/boot/openvela/a733/Image')!=(seal/'nuttx.bin').read_bytes():raise ValueError('Kernel readback')
        if root.read('/boot/boot.scr')!=(report/'assets/camera-v203.scr').read_bytes():raise ValueError('Script readback')
        log+=old.command(['e2fsck','-fn',file])
    log+=old.command(['bash',REPO/'tools/verify-a733-image.sh',image,kernel])
    spans=[]
    for begin,end in ((0,old.ROOT_BEGIN),(old.ROOT_BEGIN+old.ROOT_BYTES,2147483648)):
        digest=old.digest(base,begin,end)
        if old.digest(image,begin,end)!=digest:raise ValueError('Non-root partition changed')
        spans.append(dict(begin=begin,end=end,sha256=digest))
    result=dict(result='V203_CAMERA_IMAGE_PACKAGED_NOT_BOARD_TESTED',source_commit=source_commit,
                image=str(image),bytes=image.stat().st_size,sha256=old.digest(image),kernel_sha256=kernel,
                unchanged_spans=spans,boot_assets=manifest,hardware_verified=False,
                physical_disks_written=False,ufs_written=False,models_fat_unchanged=True)
    (report/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
    (report/'package.log').write_text(log)
    with Path(str(image)+'.sha256').open('x') as f:f.write(result['sha256']+'  '+image.name+'\n')
    print(json.dumps(result,indent=2))
if __name__=='__main__':main()
