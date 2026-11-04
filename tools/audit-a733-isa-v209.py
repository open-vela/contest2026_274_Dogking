#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Audit actual ELF: preserve v208 ISA/O2 checks plus real yield/MRS code."""
from pathlib import Path
import hashlib,json,re,subprocess,sys
assert len(sys.argv)==3
r=Path(__file__).resolve().parents[1];build=Path(sys.argv[1]);out=Path(sys.argv[2])
subprocess.run([sys.executable,str(r/'tools/audit-a733-isa-v208.py'),str(build),str(out)],check=True)
report=json.loads(out.read_text())
db=json.loads((build/'compile_commands.json').read_text())
import shlex
cmd=shlex.split(next(x for x in db if x['file'].endswith('/a733_camera_live.c'))['command'])
cc=Path(cmd[0]);objdump=cc.with_name('aarch64-none-elf-objdump')
assembly=subprocess.check_output([str(objdump),'-d',str(build/'nuttx')],text=True)
def function(name):
    match=re.search(r'^[0-9a-f]+ <'+name+r'(?:[.][^>]*)?>:\n(.*?)(?=^\n|^[0-9a-f]+ <|\Z)',assembly,re.M|re.S)
    assert match,name+' missing from final ELF'
    return match[0]
wait=function('live_cpu_bind');identity=function('cpu_identity')
# GCC -Os devirtualizes cpu_ops and inlines the tiny cpu_wait wrapper. Verify
# the actual usleep call in its migration caller, not a removable symbol.
assert re.search(r'<[^>]*usleep[^>]*>',wait),wait
assert re.search(r'<cpu_identity(?:[.][^>]*)?>',wait),wait
assert len(re.findall(r'\bmrs\b[^\n]*\bmidr_el1\b',identity))==1
assert len(re.findall(r'\bmrs\b[^\n]*\bmpidr_el1\b',identity))==2
report.update(migration_yield_verified=True,identity_bracket_verified=True,
 cpu_wait_disassembly=wait,cpu_identity_disassembly=identity,
 cpu_wait_sha256=hashlib.sha256(wait.encode()).hexdigest(),
 hardware_verified=False)
out.write_text(json.dumps(report,indent=2)+'\n')
print('V209_FINAL_ELF_BLOCKING_YIELD_AND_BRACKETED_MRS_VERIFIED_NOT_BOARD_TESTED')
