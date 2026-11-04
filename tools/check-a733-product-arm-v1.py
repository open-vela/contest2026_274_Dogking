#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compile product integration with actual NuttX ARM flags; no SDK mutation."""
from pathlib import Path
import argparse,subprocess,shlex,json,hashlib
r=Path(__file__).resolve().parents[1];sdk=(r/'../../quickly-openvela').resolve()
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
out=a.output.resolve();assert out.is_relative_to(r/'out');out.mkdir()
build=a.build.resolve();overlay=r/'board/a733-cubie-a7z/openvela-overlay'
commands=subprocess.check_output([str(sdk/'prebuilts/tools/ninja/bin/ninja'),'-C',str(build),'-t','commands'],text=True).splitlines()
units={**{'apps/system/aipet/'+n:'apps/system/aipet/audio_app.cxx' for n in ('product_core.cxx','product_runtime.cxx','audio_app.cxx','aipet_main.cxx','mcu_protocol.cxx','mcu_link.cxx')},
 'apps/system/a733camera/a733_camera_live.c':'apps/system/a733camera/a733_camera_live.c',
 'apps/system/aipet/product_cloud_config.c':'apps/system/aipet/agent_startup.c',
 'apps/system/a733display/a733display_main.c':'apps/system/a733display/a733display_main.c'}
records={}
for source,anchor in units.items():
    args=next(shlex.split(c) for c in reversed(commands) if ' -c ' in c and str(sdk/anchor) in c)
    if args[0]=='ccache':args=args[1:]
    args[1:1]=['-DCONFIG_A733_PRODUCT_V1=1','-DCONFIG_A733_CPU_ACOUSTIC=1',
      *['-I'+str(overlay/v) for v in ('apps/system/aipet','apps/system/aipetllm','apps/system/a733display','apps/include','nuttx/include')],
      '-fstack-usage','-Wframe-larger-than=4096','-Werror=frame-larger-than=4096']
    if 'a733display' in source:
        args[1:1]=['-include',str(overlay/'vendor/allwinnertech/boards/a733/cubie-a7z/include/board.h'),
                   '-include',str(overlay/'vendor/allwinnertech/chips/a733/include/a733_peripherals.h')]
    src=overlay/source;obj=out/(src.name+'.o');args[args.index('-c')+1]=str(src);args[args.index('-o')+1]=str(obj)
    if '-MF' in args:args[args.index('-MF')+1]=str(out/(src.name+'.d'))
    with (out/(src.name+'.log')).open('x') as log:subprocess.run(args,cwd=build,check=True,stdout=log,stderr=subprocess.STDOUT)
    maxframe=0
    for line in obj.with_suffix('.su').read_text().splitlines():
        _,size,kind=line.rsplit('\t',2);assert kind=='static' and size.isdigit() and int(size)<=4096,line
        maxframe=max(maxframe,int(size))
    records[source]=dict(sha256=hashlib.sha256(src.read_bytes()).hexdigest(),max_frame=maxframe)
    print('PASS ARM '+source,flush=True)
(out/'summary.json').write_text(json.dumps(dict(result='PRODUCT_V1_NUTTX_ARM_COMPILE_NOT_BOARD',units=records),indent=2)+'\n')
