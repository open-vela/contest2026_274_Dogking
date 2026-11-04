#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compile v181 units with actual NuttX/ARM64 commands, no SDK source edits."""
import hashlib
import json
import os
import shlex
import subprocess
import sys
from pathlib import Path

repo = Path(__file__).resolve().parents[1]
sdk = (repo / '../../quickly-openvela').resolve()
build = Path(os.environ.get('A733_VOICE_ABI_BUILD',sdk / 'cmake_out/cubie-a7z_nsh_v180_llm_context')).resolve()
ninja = sdk / 'prebuilts/tools/ninja/bin/ninja'
overlay = repo / 'board/a733-cubie-a7z/openvela-overlay'
dest = Path(sys.argv[1]).resolve()
dest.mkdir()
commands = subprocess.check_output([str(ninja), '-C', str(build), '-t', 'commands'], text=True).splitlines()
pet = 'apps/system/aipet/'
units = [(pet + name, pet + 'audio_app.cxx') for name in (
    'audio_app.cxx', 'audio_resample_stream.cxx', 'voice_asr_stream.cxx',
    'voice_general_asr.cxx', 'voice_general_provider.cxx',
    'voice_acoustic.cxx', 'voice_tts_engine.cxx',
    'voice_play_stream.cxx', 'voice_phrase_pipe.cxx', 'voice_tts_frontend.cxx',
    'pet_routes.cxx', 'local_llm.cxx', 'aipet_main.cxx')]
for name in ('aipetllm_modelcheck.c',):
    path = 'apps/system/aipetllm/' + name
    units.append((path, path))
for name in ('a733_i2s0_audio.c', 'a733_audio_client.c'):
    path = 'vendor/allwinnertech/chips/a733/' + name
    units.append((path, path))
summary = {}
for source, anchor in units:
    args = next(shlex.split(s) for s in reversed(commands)
                if ' -c ' in s and str(sdk / anchor) in s)
    if args[0] == 'ccache':
        args = args[1:]
    args[1:1] = ['-I' + str(overlay / 'apps/system/aipetllm'),
                 '-I' + str(overlay / 'apps/system/aipet'),
                 '-I' + str(overlay / 'apps/include'),
                 '-I' + str(overlay / 'nuttx/include'),
                 '-I' + str(overlay / 'apps/system/a733display'),
                 '-fstack-usage', '-Wframe-larger-than=4096',
                 '-Werror=frame-larger-than=4096']
    if os.environ.get('A733_GENERAL_VOICE_ARM_CHECK')=='1' and source.startswith(pet):
        args.insert(1,'-DCONFIG_A733_CPU_ACOUSTIC=1')
    src = overlay / source
    args[args.index('-c') + 1] = str(src)
    obj = dest / (src.name + '.o')
    args[args.index('-o') + 1] = str(obj)
    if '-MF' in args:
        args[args.index('-MF') + 1] = str(dest / (src.name + '.d'))
    with (dest / (src.name + '.log')).open('x') as log:
        subprocess.run(args, cwd=build, check=True, stdout=log, stderr=subprocess.STDOUT)
    maximum = 0
    for line in obj.with_suffix('.su').read_text().splitlines():
        _, size, kind = line.rsplit('\t', 2)
        if kind != 'static' or not size.isdigit() or int(size) > 4096:
            raise ValueError('Unbounded/large stack frame: ' + line)
        maximum = max(maximum, int(size))
    summary[source] = {'sha256': hashlib.sha256(src.read_bytes()).hexdigest(),
                       'actual_nuttx_flags': True, 'max_static_frame': maximum, 'passed': True}
(dest / 'summary.json').write_text(json.dumps(summary, indent=2))
print(json.dumps(summary, indent=2))
