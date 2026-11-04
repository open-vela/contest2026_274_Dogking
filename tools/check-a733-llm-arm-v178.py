#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compile changed units with actual generated ARM64/NuttX commands.

Only output paths and source paths change; no SDK or build-tree writes.
"""
import hashlib,json,shlex,subprocess,sys
from pathlib import Path
repo=Path(__file__).resolve().parents[1]
sdk=(repo/'../../quickly-openvela').resolve()
build=sdk/'cmake_out/cubie-a7z_nsh_v178_llm_candidate'
ninja=sdk/'prebuilts/tools/ninja/bin/ninja'
dest=repo/'out'/sys.argv[1];dest.mkdir()
overlay=repo/'board/a733-cubie-a7z/openvela-overlay'
units={'aipetllm':('aipetllm_modelcheck.c','aipetllm_activation.c','aipetllm_bpe.cxx'),
       'aipet':('local_llm.cxx','pet_routes.cxx')}
summary={}
for app,names in units.items():
    for name in names:
        target='apps/system/'+app+'/CMakeFiles/apps_'+app+'.dir/'+name+'.o'
        lines=subprocess.check_output([str(ninja),'-C',str(build),'-t','commands',target],text=True).splitlines()
        command=next(shlex.split(s) for s in reversed(lines) if ' -c ' in s and str(sdk/'apps/system'/app/name) in s)
        if command[0]=='ccache':command=command[1:]
        command[1:1]=['-I'+str(overlay/'apps/include'),
                      '-I'+str(overlay/'apps/system/a733display')]
        src=overlay/'apps/system'/app/name
        command[command.index('-c')+1]=str(src)
        command[command.index('-o')+1]=str(dest/(name+'.o'))
        if '-MF' in command:command[command.index('-MF')+1]=str(dest/(name+'.d'))
        with (dest/(name+'.log')).open('x') as log:
            subprocess.run(command,cwd=build,check=True,stdout=log,stderr=subprocess.STDOUT)
        summary[name]={'source':str(src.relative_to(repo)),
                       'sha256':hashlib.sha256(src.read_bytes()).hexdigest(),
                       'actual_nuttx_flags':True,'passed':True}
with (dest/'summary.json').open('x') as f:json.dump(summary,f,indent=2)
print(json.dumps(summary,indent=2))
