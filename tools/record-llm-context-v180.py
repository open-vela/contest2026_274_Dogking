#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Seal local software evidence. No image/board/network or original Git writes."""
import argparse,hashlib,json,re,shutil
from pathlib import Path
repo=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('build',type=Path);p.add_argument('output',type=Path)
a=p.parse_args();build=a.build.resolve();output=a.output.resolve()
sdk=(repo/'../../quickly-openvela').resolve()
native=sdk/'cmake_out/cubie-a7z_nsh_v180_llm_context'
overlay=repo/'board/a733-cubie-a7z/openvela-overlay'
def digest(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        while data:=f.read(1024*1024):h.update(data)
    return h.hexdigest()
staged=json.loads((build/'staged.json').read_text())
for name,value in staged['files'].items():
    if digest(overlay/name)!=value:raise ValueError('Changed after staging: '+name)
restored=json.loads((build/'sdk-restored.json').read_text())
assert restored['result']=='SDK_RESTORED_BYTE_EXACT'
symbols=(build/'symbols.txt').read_text()
assert 'aipetllm_infer_chat' in symbols and 'aipetllm_prefix_clear' in symbols
arm=json.loads((repo/'out/llm-arm-v180-r1/summary.json').read_text())
for record in arm.values():assert digest(repo/record['source'])==record['sha256']
real={}
for name in ('minicpm','qwen'):
    text=(repo/f'out/v180-{name}-context-r1.log').read_text()
    values=re.findall(r'LOGITS prompt=.*?maxabs=([0-9.e+-]+) rmse=([0-9.e+-]+)',text)
    assert len(values)==13 and all(float(x)<=1e-5 and float(y)<=1e-6 for x,y in values)
    assert 'warm-cold=exact PASS' in text and 'MULTITURN_TEMPLATE exact-reference-tokens' in text
    real[name]={'full_logit_comparisons':len(values),'maxabs':max(float(x) for x,_ in values),
                'log_sha256':digest(repo/f'out/v180-{name}-context-r1.log')}
assert 'PASS: atomic config, bounded RAM dialogue' in (repo/'out/v180-llm-regression-r1.log').read_text()
sources={'nuttx.bin':'Image','nuttx':'nuttx.elf','.config':'build.config'}
for name in sources:
    if not (native/name).is_file():raise ValueError('Missing build artifact: '+name)
output.mkdir()
artifacts={}
for name,target in sources.items():
    src=native/name;dest=output/target
    shutil.copyfile(src,dest)
    artifacts[dest.name]={'bytes':dest.stat().st_size,'sha256':digest(dest)}
record={'result':'V180_CONTEXT_SOFTWARE_LINKED_NOT_BOARD_TESTED','staged_sources':staged['files'],
        'sdk_restored':restored,'arm64_units':arm,'real_models':real,'artifacts':artifacts,
        'new_sd_image':False,'board_speed_verified':False,'board_multiturn_verified':False,
        'full_offline_speech_complete':False,'shared_physical_device_session':True}
(output/'manifest.json').write_text(json.dumps(record,indent=2))
print(json.dumps(record,indent=2))
