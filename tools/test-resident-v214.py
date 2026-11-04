#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compile actual dialogue capture, including consumer, FIR and gate; host only."""
import subprocess,tempfile
from pathlib import Path
r=Path(__file__).resolve().parents[1];o=r/'board/a733-cubie-a7z/openvela-overlay'
p=o/'apps/system/aipet'
with tempfile.TemporaryDirectory(prefix='voice214-') as d:
    source=(p/'audio_app.cxx').read_text();start=source.index('int audio_decimate(')
    end=source.index('\n}',start)+2
    oracle=Path(d)/'oracle.cxx'
    oracle.write_text('#include <cstddef>\n#include <cerrno>\n#include <cmath>\n#include <cstdint>\nnamespace aipet {\n'+source[start:end]+'\n}\n')
    exe=Path(d)/'dialogue'
    subprocess.run(['g++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror',
      '-fsanitize=address,undefined','-pthread','-DAIPET_TEST_VOICE_MONITOR',
      '-I'+str(p),'-I'+str(o/'apps/include'),str(r/'tools/test-dialogue-capture-v214.cxx'),
      str(p/'audio_capture_live.cxx'),str(oracle),'-Wl,--wrap=pthread_join','-lm','-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=45)
    exe=Path(d)/'session'
    subprocess.run(['g++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror',
      '-fsanitize=address,undefined','-pthread','-DAIPET_TEST_RESIDENT','-DCONFIG_A733_CPU_ACOUSTIC=1',
      '-I'+str(p),'-I'+str(o/'apps/include'),str(r/'tools/test-voice-session-v190.cxx'),
      str(p/'voice_general_provider.cxx'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=30)
    # Same production application, but external engines are explicitly mocks.
    test=(r/'tools/test-aipet-audio.sh').read_text()
    test=test.replace('repo=$(cd "$(dirname "$0")/.." && pwd)',"repo='"+str(r)+"'")
    test=test.replace('-DAIPET_AUDIO_DIR=', '-DAIPET_TEST_RESIDENT -Wl,--wrap=posix_spawn -Wl,--wrap=posix_spawn_file_actions_addopen -Wl,--wrap=getpid -DAIPET_AUDIO_DIR=',1)
    test=test[:test.index('\ng++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \\\n')]
    subprocess.run(['bash','-c',test],check=True,timeout=120)
