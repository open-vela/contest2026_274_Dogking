#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compile the actual probe with SDK AArch64 BSP flags, without staging SDK."""
from pathlib import Path
import argparse,hashlib,json,shlex,subprocess
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('build',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
repo=Path(__file__).resolve().parent.parent
db=json.loads((a.build/'compile_commands.json').read_text())
entry=next(e for e in db if e['file'].endswith('/a733_ufs_data.c'))
args=shlex.split(entry['command']);command=[args[0]];i=1
while i<len(args):
    if args[i] in ('-c','-o','-MF','-MT','-MQ'):i+=2
    elif args[i] in ('-MD','-MMD'):i+=1
    else:command.append(args[i]);i+=1
a.output=a.output.resolve();a.output.mkdir()
source=repo/'board/a733-cubie-a7z/openvela-overlay/vendor/allwinnertech/chips/a733/a733_imx214_probe.c'
obj=a.output/'probe.o'
command+=['-fstack-usage','-Werror=frame-larger-than=4096','-Wframe-larger-than=4096',
          '-Werror=implicit-function-declaration','-c',str(source),'-o',str(obj)]
with (a.output/'compile.log').open('xb') as log:
    subprocess.run(command,cwd=entry['directory'],stdout=log,stderr=subprocess.STDOUT,check=True)
maximum=0
for line in obj.with_suffix('.su').read_text().splitlines():
    _,size,kind=line.rsplit('\t',2)
    if kind!='static' or not size.isdigit() or int(size)>4096:raise ValueError(line)
    maximum=max(maximum,int(size))
sha=lambda file:hashlib.sha256(file.read_bytes()).hexdigest()
(a.output/'command.json').write_text(json.dumps(command,indent=2))
record=dict(result='ARM64_IMX214_PROBE_COMPILED',source_sha256=sha(source),
    header_sha256=sha(source.with_suffix('.h')),object_sha256=sha(obj),maximum_frame=maximum,
    full_driver=False,linked_into_v175=False,hardware_verified=False)
(a.output/'summary.json').write_text(json.dumps(record,indent=2));print(json.dumps(record,indent=2))
