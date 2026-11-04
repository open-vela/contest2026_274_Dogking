#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Real host CPU acoustic + explicitly STUBBED NPU interface/cleanup test."""
import argparse,json,hashlib,subprocess,shutil
from pathlib import Path
REPO=Path(__file__).resolve().parents[2]
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('new_output',type=Path)
    a=p.parse_args();out=a.new_output.resolve();assert out.is_relative_to(REPO/'out');out.mkdir()
    assets=out/'assets';assets.mkdir()
    source=REPO.parent/'references/matcha-icefall-zh-baker'
    for name in ('tokens.txt','lexicon.txt'):shutil.copyfile(source/name,assets/name)
    # Production loader deliberately refuses final-component symlinks.
    shutil.copyfile(REPO/'out/matcha-ort-v183-r1/model-steps-3.basic.ort',assets/'model-steps-3.basic.ort')
    pet=REPO/'board/a733-cubie-a7z/openvela-overlay/apps/system/aipet'
    lib=REPO/'out/ort-capi-host-v184-r1/libonnxruntime.so.1.23.2'
    units=[REPO/'tools/test-tts-engine-v185.cxx',*[pet/n for n in ('voice_tts_engine.cxx','voice_acoustic.cxx','voice_tts_frontend.cxx')]]
    cmd=['g++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-fno-exceptions','-fno-rtti',
         '-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(pet),
         '-I'+str(pet.parents[1]/'include'),*map(str,units),str(lib),'-Wl,-rpath,'+str(lib.parent),
         '-pthread','-Wl,--wrap=getpid','-o',str(out/'test')]
    with (out/'compile.log').open('xb') as log:subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True)
    with (out/'run.log').open('xb') as log:
        subprocess.run([str(out/'test'),str(assets.relative_to(REPO)),str(REPO/'out/acoustic-capi-host-v184-r2')],
          cwd=REPO,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=120)
    (out/'summary.json').write_text(json.dumps(dict(result='HOST_CPU_WITH_NPU_STUB_CONTRACT_PASSED',
        npu_executed=False,hardware_executed=False,audio_saved=False,full_voice_chain=False,
        command=cmd,sources={str(s.relative_to(REPO)):hashlib.sha256(s.read_bytes()).hexdigest() for s in units}),indent=2))
    print((out/'run.log').read_text())
if __name__=='__main__':main()
