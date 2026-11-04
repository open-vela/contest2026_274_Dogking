#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
import hashlib,json,shlex,subprocess,sys
from pathlib import Path
repo=Path(__file__).resolve().parents[1]
sdk=(repo/'../../quickly-openvela').resolve()
overlay=repo/'board/a733-cubie-a7z/openvela-overlay'
build=sdk/'cmake_out/cubie-a7z_nsh_v181_voice_latency'
dest=Path(sys.argv[1]).resolve();dest.mkdir()
ninja=sdk/'prebuilts/tools/ninja/bin/ninja'
target='apps/system/aipet/CMakeFiles/apps_aipet.dir/audio_app.cxx.o'
lines=subprocess.check_output([str(ninja),'-C',str(build),'-t','commands',target],text=True).splitlines()
anchor=sdk/'apps/system/aipet/audio_app.cxx'
original=next(shlex.split(s) for s in reversed(lines) if ' -c ' in s and str(anchor) in s)
results={}
for name in ('audio_app.cxx','audio_capture_live.cxx'):
    command=original[:]
    if command[0]=='ccache':command=command[1:]
    command[1:1]=['-I'+str(overlay/'apps/system/aipet'),'-I'+str(overlay/'apps/include'),
                  '-fstack-usage','-Wframe-larger-than=4096','-Werror=frame-larger-than=4096']
    src=overlay/'apps/system/aipet'/name;obj=dest/(name+'.o')
    command[command.index('-c')+1]=str(src);command[command.index('-o')+1]=str(obj)
    if '-MF' in command:command[command.index('-MF')+1]=str(dest/(name+'.d'))
    with (dest/(name+'.log')).open('x') as log:subprocess.run(command,cwd=build,check=True,stdout=log,stderr=subprocess.STDOUT)
    maximum=0
    for line in obj.with_suffix('.su').read_text().splitlines():
        _,size,kind=line.rsplit('\t',2)
        assert kind=='static' and int(size)<=4096,line
        maximum=max(maximum,int(size))
    results[str(src.relative_to(repo))]={'sha256':hashlib.sha256(src.read_bytes()).hexdigest(),'max_static_frame':maximum,'actual_nuttx_flags':True}
(dest/'summary.json').write_text(json.dumps(results,indent=2))
print(json.dumps(results,indent=2))
