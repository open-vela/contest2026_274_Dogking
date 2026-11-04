#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Package a NEW PRIVATE candidate image after real build/restore gates.

Never writes a physical disk. Passed v182 base is hash-locked and read only.
Adds only the two NEW voice directories, round-trips every file, verifies
kernel and GPT. No format/migration, credentials, LLM or hardware acceptance.
"""
import argparse,hashlib,json,os,subprocess,tempfile,shutil
from pathlib import Path,PurePosixPath
REPO=Path(__file__).resolve().parents[1]
BASE_SHA='0bb4bae686bb7132f256eee89aae7da12dd78a50c78112445a33099efb4e88c6'
def sha(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        while block:=f.read(1024*1024):h.update(block)
    return h.hexdigest()
def run(args,log,**kw):
    return subprocess.run(list(map(str,args)),stdout=log,stderr=subprocess.STDOUT,check=True,**kw)
def main():
    # Use the SDK's existing offline host tools, just like its build wrapper.
    sdk=(REPO/'../../quickly-openvela').resolve(strict=True)
    host=sdk/'prebuilts/build-tools/linux-x86_64/bin'
    # Windows checkouts can materialize mdir/mcopy/mmd as empty symlink stubs.
    # Repair only private temporary aliases, never the shared SDK checkout.
    aliases=tempfile.TemporaryDirectory(prefix='a733-mtools185-')
    for name in ('mdir','mmd','mcopy'):(Path(aliases.name)/name).symlink_to(host/'mtools')
    os.environ['PATH']=aliases.name+os.pathsep+str(host)+os.pathsep+os.environ.get('PATH','')
    for tool in ('mdir','mmd','mcopy','debugfs','e2fsck','sha256sum','stat','dd','sgdisk'):
        assert shutil.which(tool),('missing packaging tool',tool)
    subprocess.run(['mdir','-V'],stdout=subprocess.DEVNULL,check=True)
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('build_run',type=Path);p.add_argument('sdk_build',type=Path)
    p.add_argument('private_assets',type=Path);p.add_argument('new_output_image',type=Path)
    p.add_argument('new_evidence',type=Path);a=p.parse_args()
    build=a.sdk_build.resolve(strict=True);report=a.build_run.resolve(strict=True)
    out=a.new_output_image.resolve();evidence=a.new_evidence.resolve()
    assert out.is_relative_to(REPO.parent) and out.suffix=='.img' and not out.exists()
    assert evidence.is_relative_to(REPO/'out') and not evidence.exists()
    assert (report/'wrapper-status.txt').read_text().strip()=='0'
    restored=json.loads((report/'sdk-restored.json').read_text());assert restored['result']=='SDK_RESTORED_BYTE_EXACT'
    config=(build/'.config').read_text().splitlines()
    required=['CONFIG_A733_CPU_ACOUSTIC=y','CONFIG_A733_NPU_GENERAL_ASR=y',
              'CONFIG_CXX_STANDARD="gnu++17"','CONFIG_CXX_MINI_LOCALIZATION=y',
              'CONFIG_CXX_WCHAR=y','CONFIG_LIBC_MAX_EXITFUNS=0','CONFIG_BUILD_FLAT=y']
    assert all(line in config for line in required),'incompatible full voice profile'
    assert 'CONFIG_CXX_EXCEPTION=y' not in config and 'CONFIG_CXX_RTTI=y' not in config
    kernel=build/'nuttx.bin';assert kernel.stat().st_size>6000000
    base=REPO.parent/'openvela-a733-cubie-a7z-sd-v182-live-voice-candidate.img'
    assert base.stat().st_size==2147483648 and sha(base)==BASE_SHA,'passed base changed'
    assets=a.private_assets.resolve(strict=True);manifest=json.loads((assets/'manifest.json').read_text())
    assert manifest['result']=='PRIVATE_CANDIDATE_ASSETS_NOT_BOARD_ACCEPTANCE'
    locked=REPO/'docs/evidence/general-voice-v185/private-assets-manifest.json'
    assert sha(assets/'manifest.json')==sha(locked),'asset manifest differs from reviewed version'
    records=manifest['files'];names=set()
    for r in records:
        name=r['path'];parts=PurePosixPath(name).parts
        assert parts[0] in ('general-asr','general-tts') and len(parts)>1
        assert all(x not in ('.','..') for x in parts) and '\\' not in name and ':' not in name
        assert name not in names;names.add(name)
        source=assets.joinpath(*parts)
        assert source.is_relative_to(assets) and not source.is_symlink()
        assert source.resolve(strict=True).is_relative_to(assets),'asset parent symlink escape'
        assert source.stat().st_size==r['bytes'] and sha(source)==r['sha256']
    evidence.mkdir();fat=str(out)+'@@469762048'
    with (evidence/'package.log').open('xb') as log:
        run(['bash',REPO/'tools/package-a733-kernel-image.sh',base,out,kernel],log)
        def exists(name):
            return subprocess.run(['mdir','-i',fat,name],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL).returncode==0
        for name in ('::/npu/general-asr','::/npu/general-tts'):assert not exists(name),'refuse old voice-directory collision'
        directories={'::/npu'}
        for r in records:
            parent=PurePosixPath(r['path']).parent
            for size in range(1,len(parent.parts)+1):directories.add('::/npu/'+str(PurePosixPath(*parent.parts[:size])))
        for name in sorted(directories,key=lambda s:(s.count('/'),s)):
            if not exists(name):run(['mmd','-i',fat,name],log)
        with tempfile.TemporaryDirectory(prefix='a733-voice185-') as temp:
            for i,r in enumerate(records):
                source=assets/r['path'];dest='::/npu/'+r['path']
                run(['mcopy','-i',fat,source,dest],log)
                check=Path(temp)/str(i);run(['mcopy','-i',fat,dest,check],log)
                assert check.stat().st_size==r['bytes'] and sha(check)==r['sha256']
        run(['bash',REPO/'tools/verify-a733-image.sh',out,sha(kernel)],log)
    result=dict(result='PRIVATE_IMAGE_PACKAGED_NOT_BOOTED',image=str(out),bytes=out.stat().st_size,
        source_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=REPO,text=True).strip(),
        sha256=sha(out),base_sha256=BASE_SHA,kernel_sha256=sha(kernel),asset_manifest_sha256=sha(assets/'manifest.json'),
        files_roundtripped=len(records),sdk_restore=restored,required_config=required,
        hardware_executed=False,full_voice_chain=False,latency_3_to_4_seconds=False,
        physical_disks_written=False,old_images_modified=False,models_private=True)
    (evidence/'summary.json').write_text(json.dumps(result,indent=2))
    print(json.dumps(result,indent=2))
if __name__=='__main__':main()
