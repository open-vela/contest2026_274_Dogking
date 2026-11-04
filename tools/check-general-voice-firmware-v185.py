#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Assert the FINAL target ELF actually contains the full candidate + safety gate.
No model execution or hardware. SDK source must already be restored.
"""
import argparse,hashlib,json,subprocess
from pathlib import Path
REPO=Path(__file__).resolve().parents[1]
SDK=(REPO/'../../quickly-openvela').resolve()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('build_run',type=Path);p.add_argument('sdk_build',type=Path)
    p.add_argument('new_output',type=Path);a=p.parse_args()
    run=a.build_run.resolve(strict=True);build=a.sdk_build.resolve(strict=True)
    out=a.new_output.resolve();assert out.is_relative_to(REPO/'out') and not out.exists()
    assert (run/'wrapper-status.txt').read_text().strip()=='0'
    restored=json.loads((run/'sdk-restored.json').read_text());assert restored['result']=='SDK_RESTORED_BYTE_EXACT'
    nm=SDK/'prebuilts/gcc/linux-x86_64/aarch64-none-elf/bin/aarch64-none-elf-nm'
    symbols=subprocess.check_output([str(nm),'-C','--defined-only',str(build/'nuttx')],text=True)
    expected=['aipet::voice_general_asr_open(', 'aipet::voice_general_configure_full(',
              'aipet::voice_tts_engine_synthesize(', 'aipet::voice_acoustic_run(', 'OrtGetApiBase']
    for name in expected:assert name in symbols,('missing real target symbol',name)
    image=(build/'nuttx.bin').read_bytes()
    for message in ('general-voice-v185-candidate','ASR owner retained; no transcript; REBOOT REQUIRED',
                    'worker ownership uncertain; no further audio operations'):
        assert message.encode() in image,('missing final safety code',message)
    config=(build/'.config').read_text().splitlines()
    needed=['CONFIG_A733_CPU_ACOUSTIC=y','CONFIG_A733_NPU_GENERAL_ASR=y',
            'CONFIG_LIBC_MAX_EXITFUNS=0','CONFIG_BUILD_FLAT=y','CONFIG_CXX_MINI_LOCALIZATION=y','CONFIG_CXX_WCHAR=y']
    assert all(v in config for v in needed)
    out.mkdir()
    (out/'summary.json').write_text(json.dumps(dict(result='FINAL_FULL_VOICE_ELF_LINKED_NOT_EXECUTED',
        bytes=len(image),sha256=hashlib.sha256(image).hexdigest(),defined_symbols=expected,
        required_config=needed,sdk_restore=restored,hardware_executed=False,full_voice_chain=False),indent=2))
    print((out/'summary.json').read_text())
if __name__=='__main__':main()
