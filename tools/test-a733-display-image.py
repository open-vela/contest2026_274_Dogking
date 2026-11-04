#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Extract the exact production file-render function; mock LCD, not pixels."""
from pathlib import Path
import subprocess,tempfile
repo=Path(__file__).resolve().parents[1]
source=(repo/'board/a733-cubie-a7z/openvela-overlay/apps/system/a733display/a733display_main.c').read_text()
start=source.index('static int display_image_file(')
end=source.index('\n}\n',start)+3
function=source[start:end]
with tempfile.TemporaryDirectory(prefix='a733-display-file-') as tmp:
    root=Path(tmp);c=root/'test.c';exe=root/'test'
    c.write_text('''#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
static unsigned calls;
static int display_prepare(void){++calls;return 0;}
static int a7z_st7735_write_area(unsigned a,unsigned b,unsigned x,unsigned y,const uint16_t *p,unsigned n)
{assert(!a && !b && x==127 && y==159 && n==20480);for(unsigned i=0;i<n;++i)assert(p[i]==0xf800);++calls;return 0;}
'''+function+'''
int main(int argc,char **argv){assert(argc==3);int r=display_image_file(argv[1]);
if(!strcmp(argv[2],"good"))assert(!r && calls==2);else assert(r<0 && !calls);return 0;}
''')
    subprocess.run(['gcc','-std=c11','-Wall','-Wextra','-Werror','-O1','-g','-fno-pie','-no-pie',
                    '-fsanitize=address,undefined',str(c),'-o',str(exe)],check=True)
    for name,data in (('good',bytes([255,0,0])*20480),('short',b'bad'),
                      ('long',bytes([255,0,0])*20480+b'bad')):
        f=root/name;f.write_bytes(data)
        subprocess.run([str(exe),str(f),'good' if name=='good' else 'bad'],check=True)
    subprocess.run([str(exe),str(root/'missing'),'bad'],check=True)
print('PASS: exact production RGB24-to-RGB565 renderer, full-frame transfer and strict short/long/missing rejection before LCD access; MOCK LCD ONLY')
