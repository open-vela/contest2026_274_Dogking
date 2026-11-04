#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
import argparse,subprocess,hashlib,json
r=Path(__file__).resolve().parents[1];p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args()
out=a.output.resolve();assert out.is_relative_to(r/'out');out.mkdir()
pet=r/'board/a733-cubie-a7z/openvela-overlay/apps/system/aipet'
with (out/'endpoint.json').open('x') as log:
    subprocess.run(['python3',str(r/'tools/test-v221-endpoint-regression.py')],cwd=r,check=True,stdout=log,stderr=subprocess.STDOUT)
with (out/'provider.log').open('x') as log:
    subprocess.run(['g++','-std=c++17','-O1','-g','-fsanitize=address,undefined','-pthread','-I'+str(pet),
     str(r/'tools/test-voice-local.cxx'),str(pet/'voice_local.cxx'),str(pet/'pet_core.cxx'),
     '-Wl,--wrap=clock_gettime','-o',str(out/'provider')],cwd=r,check=True,stdout=log,stderr=subprocess.STDOUT)
    subprocess.run([str(out/'provider')],check=True,stdout=log,stderr=subprocess.STDOUT)
(out/'summary.json').write_text(json.dumps(dict(result='V223_ENDPOINT_DURATION_PASSED_NOT_BOARD',
    sources={str(f.relative_to(r)):hashlib.sha256(f.read_bytes()).hexdigest() for f in
     [pet/'audio_capture_live.hxx',pet/'voice_local.cxx',r/'tools/test-voice-local.cxx']},
    hardware=False),indent=2)+'\n')
