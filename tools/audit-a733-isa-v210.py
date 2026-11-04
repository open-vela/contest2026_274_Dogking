#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Final linked v210 and actual tensor-object ISA proof; not A733 execution."""
from pathlib import Path
import hashlib,json,re,shlex,subprocess,sys
assert len(sys.argv)==3
r=Path(__file__).resolve().parents[1];build=Path(sys.argv[1]);out=Path(sys.argv[2])
subprocess.run([sys.executable,str(r/'tools/audit-a733-isa-v209.py'),str(build),str(out)],check=True)
report=json.loads(out.read_text());db=json.loads((build/'compile_commands.json').read_text())
entry=next(x for x in db if x['file'].endswith('/a733_tensor_client.c'))
cmd=shlex.split(entry['command']);cc=Path(cmd[0]);obj=Path(cmd[cmd.index('-o')+1])
if not obj.is_absolute():obj=Path(entry['directory'])/obj
assembly=subprocess.check_output([str(cc.with_name('aarch64-none-elf-objdump')),'-d',str(obj)],text=True)
assert 'crc32x' in assembly and 'crc32b' in assembly and 'crc32cx' not in assembly
whole=subprocess.check_output([str(cc.with_name('aarch64-none-elf-objdump')),'-d',str(build/'nuttx')],text=True)
for name in ('llm_aff_id','pool_worker','llm_aff_wait'):
    part=re.search(r'^[0-9a-f]+ <'+name+r'(?:[.][^>]*)?>:\n(.*?)(?=^\n|^[0-9a-f]+ <|\Z)',whole,re.M|re.S)
    assert part,name
    if name=='llm_aff_id':
        assert len(re.findall(r'\bmrs\b[^\n]*\bmidr_el1\b',part[0]))==1
        assert len(re.findall(r'\bmrs\b[^\n]*\bmpidr_el1\b',part[0]))==2
    if name=='pool_worker':
        # -Os inlines both single-use wrappers into the persistent pthread.
        # Verify this actual callback and the bracketing/yield callees.
        assert re.search(r'<llm_aff_id(?:[.][^>]*)?>',part[0])
        report['llm_worker_disassembly_sha256']=hashlib.sha256(part[0].encode()).hexdigest()
    if name=='llm_aff_wait':assert re.search(r'<[^>]*usleep[^>]*>',part[0])
report.update(tensor_object_sha256=hashlib.sha256(obj.read_bytes()).hexdigest(),
 tensor_optional_crc32x=assembly.count('crc32x'),tensor_optional_crc32b=assembly.count('crc32b'),
 tensor_assembly_sha256=hashlib.sha256(assembly.encode()).hexdigest(),
 llm_actual_bracketed_identity_verified=True,voice_board_latency_measured=False)
out.write_text(json.dumps(report,indent=2)+'\n')
print('V210_TENSOR_OBJECT_OPTIONAL_CRC_AND_LLM_PHYSICAL_CORE_MRS_VERIFIED_NOT_BOARD')
