#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Real provider + unmodified production prepare: prove old EBUSY, then fix.

Only ASR/TTS/device execution is mocked. No readiness/success claims for board.
"""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1]
app=r/'board/a733-cubie-a7z/openvela-overlay/apps/system/aipet'
original=(app/'audio_product.inc').read_text()
prepare=original[original.index('int audio_product_prepare_local()'):original.index('int audio_i2s_cloud_speak(')]
fixture='''
#include "voice_general_provider.hxx"
#include "voice_load_log.hxx"
#include "voice_model_storage.hxx"
namespace aipet {
static int local_cancel(void *) {return 0;}
static int voice_storage_probe(const char *) {return -ENOENT;}
'''+prepare+'''
}
int product_prepare_regression(){return aipet::audio_product_prepare_local();}
'''
with tempfile.TemporaryDirectory(prefix='resident-lease-v219-') as d:
    root=Path(d);bridge=root/'prepare.cxx';bridge.write_text(fixture)
    old=root/'baseline.cxx'
    old.write_bytes(subprocess.check_output(['git','show','8c5a7b3b267128b7859c37d1e1000ec9054732f4:'+str((app/'voice_general_provider.cxx').relative_to(r))],cwd=r))
    for label,provider,expected in [('baseline-v217',old,86),('candidate-v219',app/'voice_general_provider.cxx',0)]:
        exe=root/label
        subprocess.run(['g++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror',
          '-fsanitize=address,undefined','-pthread','-DAIPET_TEST_RESIDENT',
          '-DCONFIG_A733_CPU_ACOUSTIC=1','-DAIPET_TEST_PRODUCT_PREPARE',
          '-I'+str(app),str(r/'tools/test-voice-session-v190.cxx'),str(bridge),str(provider),'-o',str(exe)],check=True)
        run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30)
        print(label,run.returncode,flush=True);print(run.stdout,end='');print(run.stderr,end='')
        assert run.returncode==expected,(label,run.returncode)
        if expected:assert 'REPRO v1.0 resident -> production prepare = -16' in run.stdout
    print('PASS v219: reproduced board EBUSY against frozen source; owner prepares/replies repeatedly, foreign access blocked, stop/close/reopen and quarantine tested. MOCK engines, NOT hardware.')
