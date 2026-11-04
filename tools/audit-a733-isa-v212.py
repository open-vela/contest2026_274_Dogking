#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Inspect real linked call chains; v212 qualifier is no longer single-use/inlined."""
from pathlib import Path
import hashlib,json,re,shlex,subprocess,sys
assert len(sys.argv)==3
r=Path(__file__).resolve().parents[1];build=Path(sys.argv[1]);out=Path(sys.argv[2])
subprocess.run([sys.executable,str(r/'tools/audit-a733-isa-v209.py'),str(build),str(out)],check=True)
report=json.loads(out.read_text());db=json.loads((build/'compile_commands.json').read_text())
entry=next(x for x in db if x['file'].endswith('/a733_tensor_client.c'))
cmd=shlex.split(entry['command']);cc=Path(cmd[0]);obj=Path(cmd[cmd.index('-o')+1])
if not obj.is_absolute():obj=Path(entry['directory'])/obj
dump=cc.with_name('aarch64-none-elf-objdump')
assembly=subprocess.check_output([str(dump),'-d',str(obj)],text=True)
assert 'crc32x' in assembly and 'crc32b' in assembly and 'crc32cx' not in assembly
whole=subprocess.check_output([str(dump),'-d',str(build/'nuttx')],text=True)
def body(name):
    match=re.search(r'^[0-9a-f]+ <'+name+r'(?:[.][^>]*)?>:\n(.*?)(?=^\n|^[0-9a-f]+ <|\Z)',whole,re.M|re.S)
    assert match,name
    return match[0]
identity=body('llm_aff_id');worker=body('pool_worker');qualifier=body('llm_affinity')
assert len(re.findall(r'\bmrs\b[^\n]*\bmidr_el1\b',identity))==1
assert len(re.findall(r'\bmrs\b[^\n]*\bmpidr_el1\b',identity))==2
assert len(re.findall(r'\bbl\b[^\n]*<llm_affinity>',worker))>=3
assert '<usleep>' in worker and '<llm_aff_id>' in qualifier and '<llm_aff_wait>' in qualifier
assert '<usleep>' in body('llm_aff_wait')
service=body('llm_pool_service')
for name in ('pthread_attr_setaffinity_np','pthread_attr_setschedpolicy','pthread_create'):
    assert '<'+name+'>' in service,name
assert '<sem_clockwait>' in body('pool_reply')
report.update(result='V212_REAL_CALL_CHAIN_RR_AFFINITY_AND_DEADLINE_VERIFIED_NOT_BOARD',
 tensor_object_sha256=hashlib.sha256(obj.read_bytes()).hexdigest(),
 llm_worker_disassembly_sha256=hashlib.sha256(worker.encode()).hexdigest(),
 llm_qualifier_disassembly_sha256=hashlib.sha256(qualifier.encode()).hexdigest(),
 llm_actual_bracketed_identity_verified=True,voice_board_latency_measured=False,
 tensor_optional_crc32x=assembly.count('crc32x'),tensor_optional_crc32b=assembly.count('crc32b'))
out.write_text(json.dumps(report,indent=2)+'\n');print(report['result'])
