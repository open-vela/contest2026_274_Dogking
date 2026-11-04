#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Actual storage selector/path allowlist and extracted CRC; host not hardware."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1]
o=r/'board/a733-cubie-a7z/openvela-overlay';p=o/'apps/system/aipet'
source=(p/'voice_acoustic.cxx').read_text()
app=(p/'audio_app.cxx').read_text()
config=(o/'vendor/allwinnertech/boards/a733/cubie-a7z/configs/nsh/defconfig').read_text()
for fd,stream in ((1,'STDOUT'),(2,'STDERR')):
    assert f'CONFIG_NSH_ALT{stream}="/dev/a7zconsole"' in config
    assert f'addopen(&files,{fd},"/dev/a7zconsole",O_WRONLY,0)' in app
    assert f'addopen(&files,{fd},"/dev/console"' not in app
serial=(o/'vendor/allwinnertech/chips/a733/a733_serial.c').read_text()
start=serial.index('static ssize_t a733_pollconsole_write(')
actual_write=serial[start:serial.index('\nstatic int a733_pollconsole_ioctl(',start)]
actual_crc=source[source.index('constexpr std::array<uint32_t,256> table()'):source.index('\nint status(')]
test=r'''#include "voice_model_storage.hxx"
#include "voice_load_log.hxx"
#include "a733_tensor_ufs_path.h"
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>
#include <algorithm>
#include <string>
#include <sys/types.h>
#define FAR
struct file {};
static std::string serial_bytes;
static void arm64_lowputc(char c){serial_bytes.push_back(c);}
ACTUAL_WRITE
ACTUAL_CRC
static unsigned calls=0;
static int mode=0;
static int probe(const char *path) {
  assert(!strncmp(path,"/ufs/npu/general-",sizeof("/ufs/npu/general-")-1));unsigned n=calls++;
  if(mode==0)return -ENOENT;
  if(mode==1)return 0;
  if(mode==2)return n==3?-ENOENT:0;
  if(mode==3)return -EBADMSG;
  return -EIO;
}
int main() {
  aipet::VoiceModelRoots roots;
  assert(!aipet::voice_model_roots(false,probe,roots) && calls==0);
  assert(!strcmp(roots.asr,"/data/npu/general-asr"));
  for(mode=0;mode<5;++mode) {
    calls=0;int rc=aipet::voice_model_roots(true,probe,roots);
    assert(rc==(mode<2?0:mode==2?-ENODATA:mode==3?-EBADMSG:-EIO));
    if(mode==1)assert(!strcmp(roots.asr,"/ufs/npu/general-asr"));
    if(mode<3)assert(calls==8);
  }
  char no_null[128];memset(no_null,'x',sizeof(no_null));
  assert(!a733_tensor_ufs_voice_path(no_null,sizeof(no_null)));
  assert(!a733_tensor_ufs_voice_path(nullptr,0));
  assert(!a733_tensor_ufs_voice_path("/ufs/npu/general-tts/vocoder.a7tv",8));
  std::vector<unsigned char> data(1048699);uint32_t x=12345;
  for(auto &b:data){x=x*1664525u+1013904223u;b=x>>24;}
  uint32_t oracle=0;
  for(auto b:data){oracle^=b;for(unsigned bit=0;bit<8;++bit)oracle=(oracle>>1)^((oracle&1)?0xedb88320u:0);}
  assert(crc(data.data(),data.size())==oracle);
  for(std::size_t chunk:{1u,3u,127u,65536u,1048576u}) {
    uint32_t c=0;
    for(std::size_t off=0;off<data.size();off+=chunk)c=crc(data.data()+off,std::min(chunk,data.size()-off),c);
    assert(c==oracle);
  }
  aipet::VoiceLoadLog log("host-test-only","no-real-storage");
  log.progress(1,2);assert(log.finish(-ENOENT)==-ENOENT);
  std::string logs(1024*1024,'L');logs+='\n';
  assert(a733_pollconsole_write(nullptr,logs.data(),logs.size())==static_cast<ssize_t>(logs.size()));
  logs.back()='\r';logs+='\n';assert(serial_bytes==logs);
  puts("PASS v215 actual polling console write: >1MiB log bytes, CRLF exact; production spawn node matches NSH defconfig; HW putc mocked");
  puts("PASS v215 storage selection absent/complete/partial/I/O/corrupt; bounded exact UFS whitelist; incremental CRC == bit oracle; load logging (HOST ONLY)");
}
'''.replace('ACTUAL_CRC',actual_crc).replace('ACTUAL_WRITE',actual_write)
with tempfile.TemporaryDirectory(prefix='voice215-') as d:
    src=Path(d)/'test.cxx';exe=Path(d)/'test';src.write_text(test)
    subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=address,undefined',
        '-I'+str(p),'-I'+str(o/'vendor/allwinnertech/chips/a733'),str(src),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=30)
