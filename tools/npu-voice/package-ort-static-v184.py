#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Collect REAL NuttX/AArch64 runtime archives, never Linux shared objects.

Private out artifact. Every archive member must be little-endian ELF64 AArch64.
The final firmware link remains a separate gate; static compilation is not it.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct

REPO=Path(__file__).resolve().parents[2]


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()


def archive_check(p):
    data=p.read_bytes();assert data[:8]==b'!<arch>\n',p
    pos=8;objects=0
    while pos<len(data):
        header=data[pos:pos+60];assert len(header)==60 and header[58:]==b'`\n'
        name=header[:16].decode('ascii').strip();size=int(header[48:58]);pos+=60
        body=data[pos:pos+size];assert len(body)==size
        pos+=size+(size&1)
        if name in ('/','//','/SYM64/'):continue
        if name.startswith('#1/'):
            body=body[int(name[3:]):]
        assert body[:6]==b'\x7fELF\x02\x01' and struct.unpack_from('<H',body,18)[0]==183,(p,name)
        objects+=1
    assert pos==len(data) and objects>0
    return objects


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('work',type=Path);p.add_argument('successful_build_report',type=Path)
    p.add_argument('new_output',type=Path)
    p.add_argument('--nuttx-compat',type=Path);a=p.parse_args()
    work=a.work.resolve(strict=True);out=a.new_output.resolve()
    assert out.is_relative_to(REPO/'out')
    report=json.loads(a.successful_build_report.read_text())
    assert report['phase']=='build' and all(s['exit']==0 for s in report['steps'])
    assert '--target' in report['steps'][0]['command'] and 'onnxruntime_session' in report['steps'][0]['command']
    out.mkdir();(out/'lib').mkdir()
    libraries=[];names=set()
    sources=sorted((work/'build').rglob('*.a'))
    compat_report=None
    if a.nuttx_compat:
        compat=a.nuttx_compat.resolve(strict=True)
        compat_report=json.loads((compat/'summary.json').read_text())
        assert compat_report['result']=='REAL_NUTTX_COMPAT_COMPILED_NOT_LINKED'
        sources.append(compat/'liba733_ort_nuttx_compat.a')
    for source in sources:
        assert source.name not in names,'archive basename collision'
        names.add(source.name);objects=archive_check(source)
        dest=out/'lib'/source.name;shutil.copyfile(source,dest)
        libraries.append(dict(path='lib/'+dest.name,bytes=dest.stat().st_size,sha256=sha(dest),objects=objects))
    assert {'libonnxruntime_session.a','libonnxruntime_providers.a','libonnxruntime_mlas.a'}<=names
    source=work/'source'
    for name in ('LICENSE','ThirdPartyNotices.txt'):
        shutil.copyfile(source/name,out/name)
    patch_files=sorted((REPO/'patches').glob('*nuttx*.patch'))
    result=dict(result='STATIC_AARCH64_LIBRARIES_COMPILED_NOT_FIRMWARE_LINKED',schema=184,
        ort_version='1.23.2',commit='a83fc4d58cb48eb68890dd689f94f28288cf2278',
        architecture='ELF64-little-AArch64',libc='NuttX',libcxx='17.0.6',
        required_abi=dict(CONFIG_LIBCXX='y',CONFIG_CXX_STANDARD='gnu++17',CONFIG_CXX_MINI_LOCALIZATION='y',CONFIG_CXX_WCHAR='y',
                          CONFIG_CXX_EXCEPTION='n',CONFIG_CXX_RTTI='n',CONFIG_BUILD_FLAT='y'),
        libraries=libraries,toolchain_sha256=sha(work/'nuttx-toolchain.cmake'),
        patches={p.name:sha(p) for p in patch_files},
        build_report_sha256=sha(a.successful_build_report),
        nuttx_compat=compat_report,
        firmware_linked=False,hardware_executed=False,models_included=False,linux_shared_libraries=False)
    (out/'manifest.json').write_text(json.dumps(result,indent=2))
    print('REAL AArch64 static archives:',len(libraries),'bytes',sum(v['bytes'] for v in libraries))


if __name__=='__main__':main()
