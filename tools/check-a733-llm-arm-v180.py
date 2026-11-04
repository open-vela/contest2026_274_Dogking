#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compile changed context/cache units with real ARM64/NuttX flags; no SDK writes."""
import hashlib,json,shlex,subprocess,sys
from pathlib import Path
repo=Path(__file__).resolve().parents[1]
sdk=(repo/'../../quickly-openvela').resolve()
build=sdk/'cmake_out/cubie-a7z_nsh_v179_offline_voice_core'
ninja=sdk/'prebuilts/tools/ninja/bin/ninja'
dest=Path(sys.argv[1]).resolve();dest.mkdir()
overlay=repo/'board/a733-cubie-a7z/openvela-overlay'
units={'aipetllm':('aipetllm_modelcheck.c','aipetllm_api.cxx','aipetllm_bpe.cxx'),
       'aipet':('local_llm.cxx','aipet_main.cxx')}
summary={}
for app,names in units.items():
    for name in names:
        target='apps/system/'+app+'/CMakeFiles/apps_'+app+'.dir/'+name+'.o'
        lines=subprocess.check_output([str(ninja),'-C',str(build),'-t','commands',target],text=True).splitlines()
        command=next(shlex.split(s) for s in reversed(lines) if ' -c ' in s and str(sdk/'apps/system'/app/name) in s)
        if command[0]=='ccache':command=command[1:]
        command[1:1]=['-I'+str(overlay/'apps/system/aipetllm'),
                      '-I'+str(overlay/'apps/system/aipet'),'-I'+str(overlay/'apps/include'),
                      '-I'+str(overlay/'apps/system/a733display'),'-fstack-usage',
                      '-Wframe-larger-than=4096','-Werror=frame-larger-than=4096']
        src=overlay/'apps/system'/app/name
        command[command.index('-c')+1]=str(src)
        obj=dest/(name+'.o');command[command.index('-o')+1]=str(obj)
        if '-MF' in command:command[command.index('-MF')+1]=str(dest/(name+'.d'))
        with (dest/(name+'.log')).open('x') as log:
            subprocess.run(command,cwd=build,check=True,stdout=log,stderr=subprocess.STDOUT)
        maximum=0
        for line in obj.with_suffix('.su').read_text().splitlines():
            _,size,kind=line.rsplit('\t',2)
            if kind!='static' or not size.isdigit() or int(size)>4096:
                raise ValueError('Unbounded/excessive frame: '+line)
            maximum=max(maximum,int(size))
        summary[name]={'source':str(src.relative_to(repo)),'sha256':hashlib.sha256(src.read_bytes()).hexdigest(),
                       'actual_nuttx_flags':True,'max_static_frame':maximum,'passed':True}
(dest/'summary.json').write_text(json.dumps(summary,indent=2))
print(json.dumps(summary,indent=2))
