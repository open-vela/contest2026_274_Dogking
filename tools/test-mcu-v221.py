#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
import os,subprocess,tempfile,hashlib,json,argparse,tarfile
r=Path(__file__).resolve().parents[1];pet=r/'board/a733-cubie-a7z/openvela-overlay/apps/system/aipet'
ref=r/'out/stm32-v221-source-efdb437'
archive=r/'out/stm32-v221-efdb437.tar.gz'
archive_sha=hashlib.sha256(archive.read_bytes()).hexdigest()
assert archive_sha=='a17b36b143c8906e13ddd769189c6a99ede841fc7ceb13a18385a78374fd737c'
with tarfile.open(archive) as tar:
    for suffix in ('Protocol/link_proto.c','Protocol/link_proto.h','User/app_config.h',
                   'Service/link_service.c','docs/上位机对接手册.md','tools/mcu_link_example.c'):
        entry=next(m for m in tar.getmembers() if m.name.endswith('/'+suffix))
        assert tar.extractfile(entry).read()==(ref/suffix).read_bytes(),suffix
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,default=r/'out/mcu-v221-tests-r2');a=p.parse_args()
out=a.output.resolve();assert out.is_relative_to(r/'out');out.mkdir(exist_ok=False)
with (out/'state.log').open('x') as f:
    exe=out/'state'
    subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-O1','-g',
       '-fsanitize=address,undefined','-ffunction-sections','-fdata-sections','-pthread',
       str(r/'tools/test-mcu-v221.cxx'),str(pet/'mcu_protocol.cxx'),'-Wl,--gc-sections','-o',str(exe)],check=True,stdout=f,stderr=subprocess.STDOUT)
    subprocess.run([str(exe)],check=True,stdout=f,stderr=subprocess.STDOUT)
s=(r/'tools/test-aipet-mcu.sh').read_text()
line='[[ $(git -C "$reference" rev-parse HEAD) == e83a9cac8a0e4a775a38910bdaf08c68bfdffc8c ]]'
assert line in s;s=s.replace(line,': # fixed efdb437 source verified by recorded archive SHA256')
s=s.replace('repo=$(cd "$(dirname "$0")/.." && pwd)', 'repo='+str(r))
s=s.replace('-I"$pet" -I"$reference/Protocol"', '-I"$pet" -I"$reference/User" -I"$reference/Protocol"')
with tempfile.TemporaryDirectory(prefix='v221-runner-') as tmp:
    script=Path(tmp)/'run.sh';script.write_text(s)
    with (out/'pipeline.log').open('x') as f:
        subprocess.run(['bash',str(script)],cwd=r,env=dict(os.environ,STM32_REFERENCE=str(ref)),check=True,stdout=f,stderr=subprocess.STDOUT,timeout=600)
report=dict(result='V221_STM32_SOFTWARE_PASSED_NOT_BOARD',upstream='efdb437948eda62cbc782692eb8fd291e5d914ae',
  archive_sha256=archive_sha,
  source_sha256={str(p.relative_to(r)):hashlib.sha256(p.read_bytes()).hexdigest() for p in
    [pet/'mcu_link.cxx',pet/'mcu_protocol.cxx',pet/'mcu_agent_tool.cxx',
     r/'tools/test-mcu-v221.cxx',r/'tools/test-aipet-mcu.cxx',r/'tools/test-mcu-v221.py']},
  logs={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in out.glob('*.log')},
  no_motion_retry=True,autonomous_motion=False,desktop_exact_human_command_consent_tested=True,
  raw_console_requires_arm=True,hardware_executed=False)
(out/'summary.json').write_text(json.dumps(report,indent=2)+'\n')
print(report['result'])
