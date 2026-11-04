#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""New regular-file image only. v199 kernel unchanged; guarded SD boot script.

The extlinux entry is preserved under a non-autoloaded backup name, because
the vendor distro scanner tries extlinux BEFORE boot.scr. No U-Boot binary,
environment, physical media, UFS contents or model FAT is changed here.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import runpy
import shutil
import stat
import subprocess
import tempfile

REPO=Path(__file__).resolve().parents[1]
BASE_NAME='openvela-a733-cubie-a7z-sd-v199-camera-sizing-repair-candidate.img'
BASE_SHA='78d74c627ba002d5a94bcd8a877bef58b17ba848155858e3188f0d118bbad724'
KERNEL_SHA='19e2e0772816a44ee9ec671c3eac92f7978b2587a8a8c502c7abd60919cf96c0'
ROOT_BEGIN,ROOT_BYTES=348127232,121634816
ACTIVE='/boot/extlinux/extlinux.conf'
BACKUP='/boot/extlinux/extlinux.conf.before-v200'
ASSETS=('camera-v200.cmd','camera-v200.scr','manifest.json',
        'imx214-1080p.a7cm','imx214-reference.c','SOURCE-LICENSE.txt')
digest=runpy.run_path(str(REPO/'tools/package-a733-boot-v188.py'))['digest']


def command(argv):
    result=subprocess.run([str(x) for x in argv],capture_output=True,text=True,
                          check=True,timeout=180,env={**os.environ,'LC_ALL':'C'})
    return result.stdout+result.stderr


class Root:
    def __init__(self,path,work):
        self.path=path.resolve();self.work=work.resolve();self.serial=0
        if path.is_symlink() or self.path.parent!=self.work or not stat.S_ISREG(self.path.stat().st_mode):
            raise ValueError('Only a private temporary regular rootfs file may be edited')
    def exists(self,name):
        return 'Inode:' in command(['debugfs','-R','stat '+name,self.path])
    def read(self,name):
        self.serial+=1;out=self.work/f'dump-{self.serial}'
        command(['debugfs','-R',f'dump {name} {out}',self.path])
        if not out.is_file():raise ValueError('Missing image file: '+name)
        return out.read_bytes()
    def write_new(self,name,source):
        if self.exists(name):raise ValueError('Refuse image-file overwrite: '+name)
        command(['debugfs','-w','-R',f'write {source.resolve()} {name}',self.path])
        if self.read(name)!=source.read_bytes():raise ValueError('Image write/readback failed: '+name)
    def remove_active(self):
        # Only the fixed active config, AFTER a byte-equal backup exists.
        if self.read(ACTIVE)!=self.read(BACKUP):raise ValueError('Backup mismatch')
        command(['debugfs','-w','-R','rm '+ACTIVE,self.path])
        if self.exists(ACTIVE):raise ValueError('Active extlinux bypass remains')


def verify_root(root,assets,original):
    if root.exists(ACTIVE):raise ValueError('extlinux would bypass the power gate')
    if root.read(BACKUP)!=original:raise ValueError('Original extlinux not preserved')
    if hashlib.sha256(root.read('/boot/openvela/a733/Image')).hexdigest()!=KERNEL_SHA:
        raise ValueError('Kernel changed')
    for name in ASSETS:
        target='/boot/openvela/a733/'+('swout1-v200.json' if name=='manifest.json' else name)
        if root.read(target)!=(assets/name).read_bytes():raise ValueError('Asset mismatch: '+name)
    if root.read('/boot/boot.scr')!=(assets/'camera-v200.scr').read_bytes():
        raise ValueError('Automatic and manual scripts differ')
    for name in ('/extlinux/extlinux.conf','/boot.scr','/boot.scr.uimg','/boot/boot.scr.uimg'):
        if root.exists(name):raise ValueError('Unexpected earlier boot entry: '+name)
    return dict(kernel_sha256=KERNEL_SHA,extlinux_backup_sha256=hashlib.sha256(original).hexdigest(),
                auto_script='/boot/boot.scr',active_extlinux_absent=True,
                auto_script_matches_manual=True,original_extlinux_preserved=True)


def install(root,assets):
    for name in (BACKUP,'/boot/boot.scr','/extlinux/extlinux.conf','/boot.scr','/boot.scr.uimg','/boot/boot.scr.uimg'):
        if root.exists(name):raise ValueError('Unexpected preexisting boot file: '+name)
    if hashlib.sha256(root.read('/boot/openvela/a733/Image')).hexdigest()!=KERNEL_SHA:
        raise ValueError('Only the sealed v199 kernel image is permitted')
    original=root.read(ACTIVE)
    if b'/boot/openvela/a733/Image' not in original or len(original)>4096:
        raise ValueError('Unexpected old boot configuration')
    # Fail before mutations if inherited mode assets changed.
    for name in ASSETS[3:]:
        if root.read('/boot/openvela/a733/'+name)!=(assets/name).read_bytes():
            raise ValueError('Inherited mode source/data changed')
    backup=root.work/'extlinux-original';backup.write_bytes(original)
    root.write_new(BACKUP,backup)
    for name in ASSETS[:3]:
        target='/boot/openvela/a733/'+('swout1-v200.json' if name=='manifest.json' else name)
        root.write_new(target,assets/name)
    root.write_new('/boot/boot.scr',assets/'camera-v200.scr')
    root.remove_active()
    return verify_root(root,assets,original)


def extract(image,out):
    command(['dd','if='+str(image),'of='+str(out),'bs=64K','iflag=skip_bytes,count_bytes',
             'skip='+str(ROOT_BEGIN),'count='+str(ROOT_BYTES),'status=none'])
    if out.stat().st_size!=ROOT_BYTES:raise ValueError('Short rootfs extraction')


def main():
    if not __debug__:raise RuntimeError('Do not run with -O')
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('seal',type=Path);p.add_argument('assets',type=Path)
    p.add_argument('new_image',type=Path);p.add_argument('new_report',type=Path)
    p.add_argument('--verify-existing-image',action='store_true');a=p.parse_args()
    image=a.new_image.resolve();report=a.new_report.resolve();base=REPO.parent/BASE_NAME
    if not image.is_relative_to(REPO.parent) or image==base.resolve() or image.suffix!='.img' or a.new_image.is_symlink():
        raise ValueError('Invalid candidate image path')
    if report.exists() or not report.is_relative_to(REPO/'out'):raise ValueError('NEW out/ report required')
    if a.verify_existing_image:
        if not image.is_file():raise ValueError('Existing regular candidate required')
    elif image.exists():raise ValueError('Refuse existing candidate overwrite')
    if base.stat().st_size!=2147483648 or digest(base)!=BASE_SHA:raise ValueError('Base identity mismatch')
    report.mkdir(parents=True)
    command(['python3',REPO/'tools/build-a733-swout1-v200.py',a.seal,report/'assets-recomputed'])
    for name in ASSETS:
        if (a.assets/name).is_symlink() or (a.assets/name).read_bytes()!=(report/'assets-recomputed'/name).read_bytes():
            raise ValueError('Supplied assets do not match exact kernel: '+name)
    with tempfile.TemporaryDirectory(prefix='a733-v200-package-') as temp:
        work=Path(temp);rootfile=work/'root.ext4'
        extract(base,rootfile);root=Root(rootfile,work)
        original=root.read(ACTIVE)
        log=command(['e2fsck','-fn',rootfile])
        if a.verify_existing_image:
            extract(image,rootfile)
            layout=verify_root(root,a.assets,original)
        else:
            layout=install(root,a.assets)
            log+=command(['e2fsck','-fn',rootfile])
            # The explicit NEW image is the only persistent binary output.
            shutil.copyfile(base,image)
            log+=command(['dd','if='+str(rootfile),'of='+str(image),'bs=64K','iflag=count_bytes',
                          'oflag=seek_bytes','seek='+str(ROOT_BEGIN),'count='+str(ROOT_BYTES),
                          'conv=notrunc','status=none'])
            extract(image,rootfile);layout=verify_root(root,a.assets,original)
        log+=command(['e2fsck','-fn',rootfile])
        (report/'extlinux-original.conf').write_bytes(original)
    log+=command(['bash',REPO/'tools/verify-a733-image.sh',image,KERNEL_SHA])
    spans=[]
    for begin,end in ((0,ROOT_BEGIN),(ROOT_BEGIN+ROOT_BYTES,2147483648)):
        old=digest(base,begin,end);new=digest(image,begin,end)
        if old!=new:raise ValueError('Non-rootfs partition changed')
        spans.append(dict(begin=begin,end=end,sha256=new))
    if image.stat().st_size!=2147483648:raise ValueError('Image length mismatch')
    result=dict(result='V200_GUARDED_SWOUT1_SD_IMAGE_PASSED_NOT_BOARD_TESTED',
                image=str(image),bytes=image.stat().st_size,sha256=digest(image),base_sha256=BASE_SHA,
                layout=layout,unchanged_spans=spans,kernel_unchanged=True,kernel_release=199,
                firmware_source_commit='6d1ece1c1a385db8f1f6163b172fbbf51a99a002',
                packaging_commit=command(['git','-C',REPO,'rev-parse','HEAD']).strip(),
                model_fat_unchanged=True,physical_disks_written=False,ufs_contents_written=False,
                bootloader_binary_changed=False,saveenv=False,camera_capture_verified=False,
                automatic_script_execution_verified_on_board=False,hardware_verified=False,
                candidate_revalidated=a.verify_existing_image,
                power_gate=json.loads((a.assets/'manifest.json').read_text()))
    (report/'package.log').write_text(log)
    (report/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))


if __name__=='__main__':main()
