#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""NEW v210 voice/camera regular SD image from sealed v209; no physical disks."""
from pathlib import Path
import argparse,hashlib,importlib.util,json,os,shutil,struct,subprocess,sys,tempfile,zlib
repo=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('p203',repo/'tools/package-a733-camera-v203.py')
p203=importlib.util.module_from_spec(spec);spec.loader.exec_module(p203)
old=p203.old
BASE_SHA='758208e54be76ff4fa985d94530dc50389790f88c9885200707b8332597057b4'
OLD_KERNEL='8a49d22720a97ad6ce0db38a36230e1c7715edec4d56bd10b8c507b37da8500d'

def voice_checks(image,report):
    """Write only NEW known fixtures to the NEW regular image; models roundtrip."""
    sdk=(repo/'../../quickly-openvela').resolve(strict=True)
    host=sdk/'prebuilts/build-tools/linux-x86_64/bin'
    assets=repo/'out/general-voice-assets-v185-r2'
    manifest=json.loads((assets/'manifest.json').read_text())
    locked=repo/'docs/evidence/general-voice-v185/private-assets-manifest.json'
    assert old.digest(assets/'manifest.json')==old.digest(locked)
    checks=[
      ('cmd_open_light_16k.wav','be2add3f8e1cdf8c328cb6464d294713832120c3e2ebbce092483f95423a4077','帮我打开灯'),
      ('cmd_time_16k.wav','c9d88dcce6b6b50a5ecce836a2b88736850cebad93c5e75e39f90a0c81b404d2','现在几点了'),
      ('neg_silence_3s_16k.wav','d303811b8c84619667cd0501342f84ec6cbe69f7aa3856dcf52fabda374c92b8','')]
    fat=str(image)+'@@469762048';records=[]
    with tempfile.TemporaryDirectory(prefix='voice210-mtools-') as directory:
        tmp=Path(directory)
        for name in ('mdir','mmd','mcopy'):(tmp/name).symlink_to(host/'mtools')
        env=dict(os.environ,PATH=str(tmp)+os.pathsep+str(host)+os.pathsep+os.environ.get('PATH',''))
        def run(args):return subprocess.run(list(map(str,args)),env=env,capture_output=True,check=True).stdout
        def exists(name):
            return subprocess.run(['mdir','-i',fat,name],env=env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL).returncode==0
        dest='::/npu/general-asr/checks'
        assert exists('::/npu/general-asr')
        if not exists(dest):run(['mmd','-i',fat,dest])
        for i,(name,digest,expected) in enumerate(checks):
            source=repo/'out/voice-board-checks-v190-r1/checks'/name
            assert source.is_file() and not source.is_symlink() and old.digest(source)==digest
            target=dest+'/'+name
            assert not exists(target),'refuse to replace a fixture'
            run(['mcopy','-i',fat,source,target])
            check=tmp/('check'+str(i));run(['mcopy','-i',fat,target,check])
            assert old.digest(check)==digest
            records.append(dict(path='/data/npu/general-asr/checks/'+name,bytes=source.stat().st_size,sha256=digest,expected_transcript=expected))
        # Every inherited private model, vocabulary, manifest and license is
        # independently read back from the final image, not just the input tree.
        for i,item in enumerate(manifest['files']):
            check=tmp/('model'+str(i));run(['mcopy','-i',fat,'::/npu/'+item['path'],check])
            assert check.stat().st_size==item['bytes'] and old.digest(check)==item['sha256'],item['path']
            check.unlink()
    (report/'voice-fixtures.json').write_text(json.dumps(dict(fixtures=records,unchanged_private_assets=len(manifest['files']),
        model_manifest_sha256=old.digest(locked),board_asr_executed=False),ensure_ascii=False,indent=2)+'\n')
    # Read the actual BPB volume length before asserting the untouched GPT tail.
    with image.open('rb') as f:f.seek(469762048);bpb=f.read(512)
    sectors=struct.unpack_from('<H',bpb,19)[0] or struct.unpack_from('<I',bpb,32)[0]
    end=469762048+sectors*struct.unpack_from('<H',bpb,11)[0]
    assert end<=image.stat().st_size-33*512
    return records,end

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('seal',type=Path);p.add_argument('new_image',type=Path);p.add_argument('new_report',type=Path)
    p.add_argument('--firmware-commit',required=True)
    p.add_argument('--voice-gates',required=True,type=Path)
    p.add_argument('--full-gates',required=True,type=Path);a=p.parse_args()
    image=a.new_image.resolve();report=a.new_report.resolve();seal=a.seal.resolve()
    base=repo.parent/'openvela-a733-cubie-a7z-sd-v209-camera-affinity-async-candidate.img'
    if image.exists() or image.parent!=repo.parent or image.suffix!='.img' or a.new_image.is_symlink():raise ValueError('NEW sibling image only')
    if report.exists() or not report.is_relative_to(repo/'out'):raise ValueError('NEW out report only')
    voice=json.loads(a.voice_gates.read_text());full=json.loads(a.full_gates.read_text())
    assert voice['result']=='V210_VOICE_HOST_ARM_PASSED_NOT_BOARD'
    assert full['result']=='V210_VOICE_CAMERA_HOST_ARM_FULL_LINK_PASSED_NOT_BOARD_TESTED'
    assert full['firmware_commit']==a.firmware_commit
    for name,digest in full['build_artifact_sha256'].items():
        assert old.digest(seal/name)==digest,('seal changed',name)
    if base.stat().st_size!=2147483648 or old.digest(base)!=BASE_SHA:raise ValueError('Passed base changed')
    config=(seal/'.config').read_text()
    for option in ('CONFIG_A733_CAMERA_PERF_TEST=y','CONFIG_A733_CAMERA_LIVE_TEST=y','CONFIG_FS_TMPFS=y','CONFIG_BUILD_FLAT=y'):assert option in config
    sources={};line_endings=[]
    for f in (repo/'board/a733-cubie-a7z/openvela-overlay').rglob('*'):
        if not f.is_file():continue
        name=str(f.relative_to(repo));data=f.read_bytes()
        committed=subprocess.check_output(['git','show',a.firmware_commit+':'+name],cwd=repo)
        if committed!=data:
            text_file=f.suffix in ('.c','.h','.cpp','.cxx','.hxx','.inc','.json','.txt','.xml','.cmake','.mk') or f.name in ('Kconfig','Makefile','Make.defs','defconfig')
            assert text_file and committed==data.replace(b'\r\n',b'\n'),('firmware drift',name)
            line_endings.append(name) # Preserve exact bytes, do NOT rewrite historical CRLF.
        sources[name]=hashlib.sha256(data).hexdigest()
    report.mkdir(parents=True);assets=report/'assets';assets.mkdir()
    with tempfile.TemporaryDirectory(prefix='asset204-') as tmp:
        prior=Path(tmp)/'prior';manifest=p203.assets(seal,prior)
        source=(prior/'camera-v203.cmd').read_text().replace('v203','v210')
        script=p203.guard.legacy_image(source)
        header=script[:32]+b'A733 camera v210'.ljust(32,b'\0');header=header[:4]+bytes(4)+header[8:]
        script=header[:4]+struct.pack('>I',zlib.crc32(header))+header[8:]+script[64:]
        (assets/'camera-v210.cmd').write_text(source,newline='\n');(assets/'camera-v210.scr').write_bytes(script)
        manifest.update(release=210,kernel_release=210,script_sha256=hashlib.sha256(script).hexdigest(),
          base_release=209,live_max_seconds=3600,clean_cycle_budget=32768,continuous_sensor_stream=True,default_sensor_stream=False,continuous_dma=False,
          ram_filesystem='official tmpfs',per_frame_media_files=False,mp4_recording=False,
          dma_rearm_resubmits_addresses=True,stream_stop_reports_cleanup_only=True,
          data_errors_require_reboot=True,raw_crc='runtime-arm-crc32-or-portable-slicing8',rgb_slots=2,rgb_pipeline=True,raw_dma_buffers=1,async_display_optional=True,affinity_confirmation='two-identities-after-blocking-yield',full_input_template_elision=True,fused_output_scan=True,post_run_weight_verification=True,pre_run_duplicate_weight_scan=False)
        (assets/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    with tempfile.TemporaryDirectory(prefix='image204-') as tmp:
        work=Path(tmp);fs=work/'root.ext4';old.extract(base,fs);root=old.Root(fs,work)
        assert not root.exists(old.ACTIVE) and root.exists(old.BACKUP)
        assert hashlib.sha256(root.read('/boot/openvela/a733/Image')).hexdigest()==OLD_KERNEL
        assert root.read('/boot/boot.scr')==(repo/'out/v209-release-20261029/camera-v209.scr').read_bytes()
        for name in p203.guard.MODE_NAMES:assert root.read('/boot/openvela/a733/'+name)==(seal/name).read_bytes()
        for target,new,backup in (('/boot/openvela/a733/Image',seal/'nuttx.bin','/boot/openvela/a733/Image.before-v210'),
                                   ('/boot/boot.scr',assets/'camera-v210.scr','/boot/boot-v209.scr')):
            previous=work/('old-'+str(root.serial));previous.write_bytes(root.read(target));root.write_new(backup,previous)
            old.command(['debugfs','-w','-R','rm '+target,fs]);root.write_new(target,new)
        for name in ('camera-v210.cmd','camera-v210.scr','manifest.json'):
            root.write_new('/boot/openvela/a733/'+('camera-v210.json' if name=='manifest.json' else name),assets/name)
        log=old.command(['e2fsck','-fn',fs]);shutil.copyfile(base,image)
        log+=old.command(['dd','if='+str(fs),'of='+str(image),'bs=64K','iflag=count_bytes','oflag=seek_bytes',
          'seek='+str(old.ROOT_BEGIN),'count='+str(old.ROOT_BYTES),'conv=notrunc','status=none'])
        old.extract(image,fs)
        assert root.read('/boot/openvela/a733/Image')==(seal/'nuttx.bin').read_bytes()
        assert root.read('/boot/boot.scr')==script
        assert root.read('/boot/openvela/a733/camera-v210.json')==(assets/'manifest.json').read_bytes()
        log+=old.command(['e2fsck','-fn',fs])
    log+=old.command(['bash',repo/'tools/verify-a733-image.sh',image,manifest['kernel_sha256']])
    spans=[]
    for begin,end in ((0,old.ROOT_BEGIN),(old.ROOT_BEGIN+old.ROOT_BYTES,2147483648)):
        digest=old.digest(base,begin,end);assert old.digest(image,begin,end)==digest
        spans.append(dict(begin=begin,end=end,sha256=digest))
    fixtures,fat_end=voice_checks(image,report)
    final_spans=[]
    for begin,end in ((0,old.ROOT_BEGIN),(fat_end,2147483648)):
        digest=old.digest(base,begin,end);assert old.digest(image,begin,end)==digest
        final_spans.append(dict(begin=begin,end=end,sha256=digest))
    result=dict(result='V210_RAM_LIVE_IMAGE_PACKAGED_NOT_BOARD_TESTED',image=str(image),bytes=image.stat().st_size,
      sha256=old.digest(image),kernel_sha256=manifest['kernel_sha256'],base_sha256=BASE_SHA,
      firmware_commit=a.firmware_commit,package_commit=old.command(['git','rev-parse','HEAD']).strip(),
      firmware_source_sha256=sources,working_CRLF_only_files=line_endings,boot_assets=manifest,unchanged_spans=final_spans,pre_fixture_unchanged_spans=spans,voice_fixtures=fixtures,fat_changed_only_for_fixtures=True,
      physical_disks_written=False,ufs_written=False,models_fat_unchanged=True,hardware_verified=False)
    result.update(voice_gates_sha256=old.digest(a.voice_gates),full_gates_sha256=old.digest(a.full_gates),
      full_voice_chain_accepted=False,latency_3_to_5_seconds=False)
    (report/'package.log').write_text(log);(report/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
    with Path(str(image)+'.sha256').open('x') as f:f.write(result['sha256']+'  '+image.name+'\n')
    print('V210_RAM_LIVE_IMAGE_PACKAGED_NOT_BOARD_TESTED',result['sha256'])
if __name__=='__main__':main()
