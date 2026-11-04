#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Record independently compared REAL GGUF inference, not A7Z performance."""
from pathlib import Path
import argparse,hashlib,json,re,shutil,subprocess
r=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('log',type=Path);p.add_argument('model',type=Path);p.add_argument('output',type=Path)
p.add_argument('--firmware-commit',required=True);a=p.parse_args()
out=a.output.resolve();assert out.is_relative_to(r/'out') and out.is_dir()
def sha(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for data in iter(lambda:f.read(8*1024*1024),b''):h.update(data)
    return h.hexdigest()
log=a.log.read_text();assert 'PASS: real three-pair history/latest-user' in log
rows=re.findall(r'QUALITY_ORACLE user=(.*?) mode=(\d) tokens=(\d+) top1=(\d+)/(\d+) maxabs=(\S+) rmse=(\S+)',log)
assert len(rows)==6 and all(x[3]==x[4] and float(x[5])<=1e-5 and float(x[6])<=1e-6 for x in rows)
samples=re.findall(r'QUALITY_SAMPLE user=(.*?) reply=(.*?) tokens=(\d+) finish=(\w+)',log)
assert len(samples)==4 and all(x[3]=='eos' and int(x[2])<128 for x in samples)
contexts=re.findall(r'QUALITY_CONTEXT user=(.*?) reply=(.*?) used=(\d+) tokens=(\d+) finish=eos',log)
assert len(contexts)==2 and all(x[2]=='3' and int(x[3])<128 for x in contexts)
sources={}
base='board/a733-cubie-a7z/openvela-overlay/apps/system/'
for name in (base+'aipet/pet_persona.hxx',*[base+'aipetllm/'+n for n in
    ('aipetllm_modelcheck.c','aipetllm_prefill.inc','aipetllm_api.cxx','aipetllm_api.h',
     'aipetllm_sequence.h','aipetllm_bpe.cxx')],'tools/test-llm-quality-v216.cxx'):
    data=(r/name).read_bytes();committed=subprocess.check_output(['git','show',a.firmware_commit+':'+name],cwd=r)
    assert data.replace(b'\r\n',b'\n')==committed.replace(b'\r\n',b'\n')
    sources[name]=sha(r/name)
if a.log.resolve()!=(out/'run.log').resolve():shutil.copyfile(a.log,out/'run.log')
report=dict(result='V216_REAL_GGUF_REFERENCE_PASSED_NOT_BOARD',firmware_commit=a.firmware_commit,
    log_sha256=sha(out/'run.log'),model_bytes=a.model.stat().st_size,model_sha256=sha(a.model),
    executable_sha256=sha(out/'test'),source_sha256=sources,oracle_rows=rows,samples=samples,context_samples=contexts,
    history_pairs=3,cold_warm_batch_reference=True,hardware_executed=False,
    physical_audio=False,board_latency=False,semantic_accuracy_score=False,
    raw_model_identity_still_requires_application_persona=True)
(out/'summary.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
print(report['result'])
