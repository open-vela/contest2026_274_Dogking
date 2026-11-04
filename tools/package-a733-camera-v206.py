#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""New v206 regular SD image from exact passed v204, never a physical disk."""
from pathlib import Path
import argparse,hashlib,importlib.util,json,shutil,struct,subprocess,sys,tempfile,zlib
repo=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('p203',repo/'tools/package-a733-camera-v203.py')
p203=importlib.util.module_from_spec(spec);spec.loader.exec_module(p203)
old=p203.old
BASE_SHA='f21a9d0bd8aab20264db3e44273ebf372c629f33ee7bba6c49659f0e2e82326d'
OLD_KERNEL='cd6bf4cc8e9ec6d2fee01f92ee2c660f8abadca563c530aca47377f536c22cc9'
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('seal',type=Path);p.add_argument('new_image',type=Path);p.add_argument('new_report',type=Path)
    p.add_argument('--firmware-commit',required=True);a=p.parse_args()
    image=a.new_image.resolve();report=a.new_report.resolve();seal=a.seal.resolve()
    base=repo.parent/'openvela-a733-cubie-a7z-sd-v204-camera-yolo-live-candidate.img'
    if image.exists() or image.parent!=repo.parent or image.suffix!='.img' or a.new_image.is_symlink():raise ValueError('NEW sibling image only')
    if report.exists() or not report.is_relative_to(repo/'out'):raise ValueError('NEW out report only')
    if base.stat().st_size!=2147483648 or old.digest(base)!=BASE_SHA:raise ValueError('Passed base changed')
    config=(seal/'.config').read_text()
    for option in ('CONFIG_A733_CAMERA_PERF_TEST=y','CONFIG_A733_CAMERA_LIVE_TEST=y','CONFIG_FS_TMPFS=y','CONFIG_BUILD_FLAT=y'):assert option in config
    sources={};line_endings=[]
    for f in (repo/'board/a733-cubie-a7z/openvela-overlay').rglob('*'):
        if not f.is_file():continue
        name=str(f.relative_to(repo));data=f.read_bytes()
        committed=subprocess.check_output(['git','show',a.firmware_commit+':'+name],cwd=repo)
        if committed!=data:
            text_file=f.suffix in ('.c','.h','.cpp','.cxx','.hxx','.json','.txt','.xml','.cmake','.mk') or f.name in ('Kconfig','Makefile','Make.defs','defconfig')
            assert text_file and committed==data.replace(b'\r\n',b'\n'),('firmware drift',name)
            line_endings.append(name) # Preserve exact bytes, do NOT rewrite historical CRLF.
        sources[name]=hashlib.sha256(data).hexdigest()
    report.mkdir(parents=True);assets=report/'assets';assets.mkdir()
    with tempfile.TemporaryDirectory(prefix='asset204-') as tmp:
        prior=Path(tmp)/'prior';manifest=p203.assets(seal,prior)
        source=(prior/'camera-v203.cmd').read_text().replace('v203','v206')
        script=p203.guard.legacy_image(source)
        header=script[:32]+b'A733 camera v206'.ljust(32,b'\0');header=header[:4]+bytes(4)+header[8:]
        script=header[:4]+struct.pack('>I',zlib.crc32(header))+header[8:]+script[64:]
        (assets/'camera-v206.cmd').write_text(source,newline='\n');(assets/'camera-v206.scr').write_bytes(script)
        manifest.update(release=206,kernel_release=206,script_sha256=hashlib.sha256(script).hexdigest(),
          base_release=204,live_max_seconds=3600,clean_cycle_budget=32768,continuous_sensor_stream=True,default_sensor_stream=False,continuous_dma=False,
          ram_filesystem='official tmpfs',per_frame_media_files=False,mp4_recording=False,
          dma_rearm_resubmits_addresses=True,stream_stop_reports_cleanup_only=True,
          data_errors_require_reboot=True,npu_sources_unchanged_from_v205=True)
        (assets/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    with tempfile.TemporaryDirectory(prefix='image204-') as tmp:
        work=Path(tmp);fs=work/'root.ext4';old.extract(base,fs);root=old.Root(fs,work)
        assert not root.exists(old.ACTIVE) and root.exists(old.BACKUP)
        assert hashlib.sha256(root.read('/boot/openvela/a733/Image')).hexdigest()==OLD_KERNEL
        assert root.read('/boot/boot.scr')==(repo/'out/v204-release-20261029/camera-v204.scr').read_bytes()
        for name in p203.guard.MODE_NAMES:assert root.read('/boot/openvela/a733/'+name)==(seal/name).read_bytes()
        for target,new,backup in (('/boot/openvela/a733/Image',seal/'nuttx.bin','/boot/openvela/a733/Image.before-v206'),
                                   ('/boot/boot.scr',assets/'camera-v206.scr','/boot/boot-v204.scr')):
            previous=work/('old-'+str(root.serial));previous.write_bytes(root.read(target));root.write_new(backup,previous)
            old.command(['debugfs','-w','-R','rm '+target,fs]);root.write_new(target,new)
        for name in ('camera-v206.cmd','camera-v206.scr','manifest.json'):
            root.write_new('/boot/openvela/a733/'+('camera-v206.json' if name=='manifest.json' else name),assets/name)
        log=old.command(['e2fsck','-fn',fs]);shutil.copyfile(base,image)
        log+=old.command(['dd','if='+str(fs),'of='+str(image),'bs=64K','iflag=count_bytes','oflag=seek_bytes',
          'seek='+str(old.ROOT_BEGIN),'count='+str(old.ROOT_BYTES),'conv=notrunc','status=none'])
        old.extract(image,fs)
        assert root.read('/boot/openvela/a733/Image')==(seal/'nuttx.bin').read_bytes()
        assert root.read('/boot/boot.scr')==script
        assert root.read('/boot/openvela/a733/camera-v206.json')==(assets/'manifest.json').read_bytes()
        log+=old.command(['e2fsck','-fn',fs])
    log+=old.command(['bash',repo/'tools/verify-a733-image.sh',image,manifest['kernel_sha256']])
    spans=[]
    for begin,end in ((0,old.ROOT_BEGIN),(old.ROOT_BEGIN+old.ROOT_BYTES,2147483648)):
        digest=old.digest(base,begin,end);assert old.digest(image,begin,end)==digest
        spans.append(dict(begin=begin,end=end,sha256=digest))
    result=dict(result='V206_RAM_LIVE_IMAGE_PACKAGED_NOT_BOARD_TESTED',image=str(image),bytes=image.stat().st_size,
      sha256=old.digest(image),kernel_sha256=manifest['kernel_sha256'],base_sha256=BASE_SHA,
      firmware_commit=a.firmware_commit,package_commit=old.command(['git','rev-parse','HEAD']).strip(),
      firmware_source_sha256=sources,working_CRLF_only_files=line_endings,boot_assets=manifest,unchanged_spans=spans,
      physical_disks_written=False,ufs_written=False,models_fat_unchanged=True,hardware_verified=False)
    (report/'package.log').write_text(log);(report/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
    with Path(str(image)+'.sha256').open('x') as f:f.write(result['sha256']+'  '+image.name+'\n')
    print('V206_RAM_LIVE_IMAGE_PACKAGED_NOT_BOARD_TESTED',result['sha256'])
if __name__=='__main__':main()
