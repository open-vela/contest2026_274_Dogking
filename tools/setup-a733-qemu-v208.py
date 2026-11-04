#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Optional Ubuntu host-only QEMU dependencies, project-local, never installed."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess
r=Path(__file__).resolve().parents[1];p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--download',action='store_true');a=p.parse_args()
d=r/'out/qemu-deps-v208';d.mkdir(exist_ok=True,parents=True)
packages=['libasound2t64','libpulse0','libasyncns0','libsndfile1','libflac14',
 'libvorbis0a','libvorbisenc2','libogg0','libopus0','libmpg123-0t64','libmp3lame0',
 'libxcb1','libdbus-1-3','libxau6','libxdmcp6','libbsd0','libmd0']
if a.download:subprocess.run(['apt-get','download',*packages],cwd=d,check=True,timeout=120)
for package in d.glob('*.deb'):subprocess.run(['dpkg-deb','-x',str(package),str(d/'root')],check=True)
lib=d/'root/usr/lib/x86_64-linux-gnu';env={**os.environ,'LD_LIBRARY_PATH':str(lib)+':'+str(lib/'pulseaudio')}
exe=(r/'../../quickly-openvela/prebuilts/qemu/linux-x86_64/bin/qemu-system-aarch64').resolve()
version=subprocess.check_output([str(exe),'--version'],text=True,env=env,timeout=10)
record=dict(result='PROJECT_LOCAL_QEMU_HOST_LIBRARIES_ONLY',qemu=version.strip(),
 packages={x.name:hashlib.sha256(x.read_bytes()).hexdigest() for x in d.glob('*.deb')},
 system_packages_installed=False,hardware_verified=False)
(d/'manifest.json').write_text(json.dumps(record,indent=2)+'\n');print(version.strip())
