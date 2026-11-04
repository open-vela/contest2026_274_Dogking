#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""ASan/UBSan actual application test using previously captured Linux tensors."""
import json,re,subprocess,sys,tempfile,os
from pathlib import Path
repo=Path(__file__).resolve().parent.parent
overlay=repo/'board/a733-cubie-a7z/openvela-overlay'
assets=Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='npukws-host-') as tmp:
    root=Path(tmp);mapping=json.loads((assets/'host-native-map.json').read_text())
    with (root/'map.txt').open('w') as stream:
        stream.write(str(len(mapping))+'\n')
        for r in mapping:
            stream.write('%d %d %d\n'%(r['model'],len(r['inputs']),len(r['outputs'])))
            stream.write(' '.join(map(str,r['inputs']))+'\n'+'\n'.join(r['outputs'])+'\n')
    flags=['-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined']
    sdk=Path(os.environ.get('OPENVELA_WORKSPACE',repo/'../../quickly-openvela'))
    for src in [sdk/'nuttx/libs/libc/misc/lib_crc32.c',repo/'tools/tensor-crc-mock.c']:
        subprocess.run(['gcc','-std=c11',*flags,'-I'+str(repo/'tools/tensor-mock'),'-I'+str(overlay/'apps/include'),'-c',str(src),'-o',str(root/(src.name+'.o'))],check=True)
    subprocess.run(['gcc','-std=c11',*flags,'-c',str(overlay/'apps/system/aipet/voice_kws.c'),'-o',str(root/'kws.o')],check=True)
    subprocess.run(['g++','-std=c++17',*flags,'-pthread','-I'+str(overlay/'apps/include'),str(repo/'tools/test-aipet-npukws.cxx'),str(root/'kws.o'),str(root/'lib_crc32.c.o'),str(root/'tensor-crc-mock.c.o'),'-o',str(root/'test')],check=True)
    result=subprocess.run([str(root/'test'),str(root/'map.txt'),str(assets/'voice'),str(root)],capture_output=True,text=True)
    print(result.stdout, end='');print(result.stderr, end='',file=sys.stderr)
    if result.returncode: raise SystemExit(result.returncode)
    cases=json.loads((assets/'voice/kws/cases.json').read_text());expected=[]
    for i,c in enumerate(cases):
        block=result.stdout.split('HOST_CASE_BEGIN=%d\n'%i)[1].split('HOST_CASE_END=%d\n'%i)[0]
        names=re.findall(r'npukws hit: word=\d+ name=(\S+)',block)
        if (c['expected'] and c['expected'] not in names) or (not c['expected'] and names):raise ValueError('Keyword mismatch '+c['tag'])
        match=re.search(r'npukws: status=0 calls=(\d+) hits=(\d+) chain=(\w+)',block)
        expected.append(dict(case=i,tag=c['tag'],calls=int(match[1]),hits=int(match[2]),chain=match[3],names=names))
        dynamic=result.stdout.split('DYNAMIC_KNOWN_BEGIN=%d\n'%i)[1].split('DYNAMIC_KNOWN_END=%d\n'%i)[0]
        d=re.search(r'npukws-input: status=0 calls=(\d+) candidates=(\d+) known=(\d+) unknown=0 chain=(\w+)',dynamic)
        if not d or (int(d[1]),int(d[2]),int(d[3]),d[4])!=(int(match[1]),int(match[2]),int(match[1]),match[3]):raise ValueError('Dynamic known mismatch '+c['tag'])
    (assets/'host-application-expected.json').write_text(json.dumps(dict(host_file_oracle_only=True,cases=expected),ensure_ascii=False,indent=2))
    print('SIX_STRICT_AND_DYNAMIC_KNOWN_FIXTURES_MATCHED; no hardware executed')
