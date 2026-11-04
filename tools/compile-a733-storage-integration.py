#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compile real storage boot/Agent/service units with SDK ARM64 flags."""
from pathlib import Path
import argparse,hashlib,json,shlex,subprocess
p=argparse.ArgumentParser();p.add_argument('build',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
repo=Path(__file__).resolve().parent.parent;overlay=repo/'board/a733-cubie-a7z/openvela-overlay'
db=json.loads((a.build/'compile_commands.json').read_text());a.output=a.output.resolve();a.output.mkdir()
results={}
for name,source,ref in [('boot',repo/'app/a733storage/a733storage_boot.c','a733wifi_main.c'),
 ('local',overlay/'apps/system/aipet/local_llm.cxx','local_llm.cxx'),
 ('services',overlay/'apps/system/a733services/a733services_main.c','a733services_main.c'),
 ('config',overlay/'apps/system/a733services/services_config.c','services_config.c')]:
    e=next(e for e in db if e['file'].endswith('/'+ref));args=shlex.split(e['command']);command=[args[0]];i=1
    while i<len(args):
        if args[i] in ('-c','-o','-MF','-MT','-MQ'):i+=2
        elif args[i] in ('-MD','-MMD'):i+=1
        else:command.append(args[i]);i+=1
    obj=a.output/(name+'.o')
    command+=['-I'+str(overlay/'apps/include'),'-fstack-usage','-Wframe-larger-than=4096','-Werror=frame-larger-than=4096','-c',str(source),'-o',str(obj)]
    with (a.output/(name+'.log')).open('xb') as log:subprocess.run(command,cwd=e['directory'],stdout=log,stderr=subprocess.STDOUT,check=True)
    maximum=0
    for line in obj.with_suffix('.su').read_text().splitlines():
        _,size,kind=line.rsplit('\t',2)
        if kind!='static' or not size.isdigit() or int(size)>4096:raise ValueError(line)
        maximum=max(maximum,int(size))
    results[name]=dict(source=str(source.relative_to(repo)),source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),object_sha256=hashlib.sha256(obj.read_bytes()).hexdigest(),maximum_frame=maximum)
    (a.output/(name+'.command.json')).write_text(json.dumps(command,indent=2))
(a.output/'summary.json').write_text(json.dumps(dict(result='ARM64_INTEGRATION_COMPILED',units=results,hardware_verified=False),indent=2))
print(json.dumps(results,indent=2))
