#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Real ARM64 application object and stack-frame check, no SDK source writes."""
import argparse,hashlib,json,shlex,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('build',type=Path);p.add_argument('output',type=Path);p.add_argument('--unit',choices=['voice','audio','driver'],default='voice');a=p.parse_args()
a.build=a.build.resolve(strict=True);a.output=a.output.resolve()
repo=Path(__file__).resolve().parent.parent;overlay=repo/'board/a733-cubie-a7z/openvela-overlay'
source=overlay/({'voice':'apps/system/aipet/voice_npu_check.cxx','audio':'apps/system/aipet/audio_app.cxx','driver':'vendor/allwinnertech/chips/a733/a733_i2s0_audio.c'}[a.unit])
db=json.loads((a.build/'compile_commands.json').read_text());entry=next(e for e in db if e['file'].endswith('/'+source.name))
a.output.mkdir();args=shlex.split(entry['command']);command=[args[0],'-I'+str(overlay/'apps/include')];i=1
while i<len(args):
    if args[i] in ['-c','-o','-MF','-MT','-MQ']:i+=2
    elif args[i] in ['-MD','-MMD']:i+=1
    else:command.append(args[i]);i+=1
command+=['-fstack-usage','-Wframe-larger-than=4096','-Werror=frame-larger-than=4096','-c',str(source),'-o',str(a.output/'voice.o')]
with (a.output/'compile.log').open('xb') as stream:result=subprocess.run(command,cwd=entry['directory'],stdout=stream,stderr=subprocess.STDOUT).returncode
if result:raise SystemExit('Target compile failed, see compile.log')
lines=(a.output/'voice.su').read_text().splitlines();maximum=0
for line in lines:
    columns=line.rsplit('\t',2)
    if len(columns)!=3 or not columns[1].isdigit():raise ValueError('Unexpected stack report')
    value=int(columns[1]);maximum=max(maximum,value)
    if value>4096 or columns[2]!='static':raise ValueError('Unbounded/large stack frame: '+line)
related=([source,source.with_name('a733_i2s0_playback.inc'),source.with_name('a733_audio_client.c')]
         if a.unit=='driver' else [source,source.with_suffix('.hxx'),source.with_name('voice_npu_stream.inc') if a.unit=='voice' else source.with_name('voice_kws_input.inc')])
if a.unit=='audio':related.append(source.with_name('voice_frontend_audit.inc'))
(a.output/'summary.json').write_text(json.dumps(dict(object_sha256=hashlib.sha256((a.output/'voice.o').read_bytes()).hexdigest(),maximum_frame=maximum,
    frames=len(lines),unit=a.unit,sources={str(f.relative_to(repo)):hashlib.sha256(f.read_bytes()).hexdigest() for f in related},
    command=command,hardware_executed=False,linked_image=False),indent=2))
print('ARM64 actual KWS application PASS; max static frame=%d bytes (%d functions)'%(maximum,len(lines)))
