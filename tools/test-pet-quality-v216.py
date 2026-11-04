#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Real application pipeline contracts with explicitly MOCKED engines/device."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1]
p=r/'board/a733-cubie-a7z/openvela-overlay/apps/system/aipet'
with tempfile.TemporaryDirectory(prefix='pet-quality-v216-') as tmp:
    exe=Path(tmp)/'pipeline'
    subprocess.run(['g++','-O2','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
      '-pthread','-I'+str(p),'-I'+str(p.parents[1]/'include'),str(r/'tools/test-voice-pipeline-v216.cxx'),
      *[str(p/n) for n in ('voice_phrase_pipe.cxx','voice_play_stream.cxx','voice_local.cxx','audio_resample_stream.cxx','pet_core.cxx')],
      '-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=60)
print('V216_APPLICATION_CONTRACTS_PASSED_MOCK_ENGINES_NOT_BOARD')
