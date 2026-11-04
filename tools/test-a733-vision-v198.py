#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Actual production loader/producer/consumer with mocked LVGL/MQ; no NPU/LCD."""
from pathlib import Path
import subprocess,tempfile,zlib
repo=Path(__file__).resolve().parents[1]
apps=repo/'board/a733-cubie-a7z/openvela-overlay/apps'
display=(apps/'system/a733display/a733display_main.c').read_text()
npu=(apps/'system/a733npu/a733npu_main.c').read_text()
def function(source,name):
    # Locate the precise definition, not a caller.
    start=source.rfind('static ',0,source.index(name+'('))
    return source[start:source.index('\n}\n',start)+3]
with tempfile.TemporaryDirectory(prefix='a733-vision198-') as tmp:
    root=Path(tmp);c=root/'loader.c';exe=root/'loader'
    loader=display[display.index('struct vision_background_s {'):display.index('\n}\n',display.index('static int vision_image_load('))+3]
    c.write_text('''#include <system/a733_vision.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
'''+loader+'''
int main(int argc,char **argv){assert(argc==3);struct vision_background_s bg;
int r=vision_image_load(argv[1],&bg);
if(!strcmp(argv[2],"bad")){assert(r<0 && !bg.pixels);return 0;}
assert(!r && bg.pixels && bg.crc==strtoul(argv[2],NULL,16));
for(unsigned y=0;y<128;++y)for(unsigned x=0;x<128;++x){unsigned sx=5*x+2,sy=5*y+2;
unsigned a=sx%256,b=sy%256,d=(sx+sy)%256;
assert(bg.pixels[y*128+x]==((a>>3)<<11 | (b>>2)<<5 | d>>3));}
free(bg.pixels);return 0;}
''')
    flags=['gcc','-std=c11','-D_DEFAULT_SOURCE','-O1','-g','-Wall','-Wextra','-Werror',
           '-fno-pie','-no-pie','-fsanitize=address,undefined','-I'+str(apps/'include')]
    subprocess.run([*flags,str(c),'-o',str(exe)],check=True)
    data=bytes(v for y in range(640) for x in range(640) for v in (x%256,y%256,(x+y)%256))
    rawcrc=zlib.crc32(data,0xffffffff)^0xffffffff
    good=root/'model.bin';good.write_bytes(data)
    subprocess.run([str(exe),str(good),f'{rawcrc:08x}'],check=True)
    for name,payload in [('short',data[:-1]),('long',data+b'x')]:
        p=root/name;p.write_bytes(payload);subprocess.run([str(exe),str(p),'bad'],check=True)
    subprocess.run([str(exe),str(root/'missing'),'bad'],check=True)
    c=root/'pipeline.c';exe=root/'pipeline'
    body='''#include <system/a733_vision.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <mqueue.h>
typedef struct {bool hidden;unsigned color;int x,y,w,h;} lv_obj_t;
typedef int lv_image_dsc_t;
#define LV_OBJ_FLAG_HIDDEN 1
static void lv_obj_add_flag(lv_obj_t*p,int f){(void)f;p->hidden=true;}
static void lv_obj_remove_flag(lv_obj_t*p,int f){(void)f;p->hidden=false;}
static unsigned lv_color_hex(unsigned c){return c;}
static void lv_obj_set_style_bg_color(lv_obj_t*p,unsigned c,int f){(void)f;p->color=c;}
static void lv_obj_set_pos(lv_obj_t*p,int x,int y){p->x=x;p->y=y;}
static void lv_obj_set_size(lv_obj_t*p,int w,int h){p->w=w;p->h=h;}
static struct a733_vision_message_s pending[8];static unsigned count,read_index,send_limit=8;
static mqd_t fake_open(const char*p,int f,...){(void)p;(void)f;return 7;}
static int fake_close(mqd_t q){(void)q;return 0;}
static int fake_send(mqd_t q,const char*p,size_t n,unsigned prio){(void)q;(void)prio;
if(count==send_limit){errno=EAGAIN;return -1;}assert(n==32);memcpy(&pending[count++],p,n);return 0;}
static ssize_t fake_receive(mqd_t q,char*p,size_t n,unsigned*prio){(void)q;(void)prio;
if(read_index==count){errno=EAGAIN;return -1;}assert(n==32);memcpy(p,&pending[read_index++],n);return n;}
#define mq_open fake_open
#define mq_close fake_close
#define mq_send fake_send
#define mq_receive fake_receive
struct a733_npu_detection_s {uint16_t class_id,score_milli;int32_t x1_centi,y1_centi,x2_centi,y2_centi;};
struct a733_npu_run_s {unsigned detection_count;uint32_t input_crc32;struct a733_npu_detection_s detections[8];};
static uint32_t g_vision_sequence;
'''
    start=display.index('struct vision_scene_s\n')
    body+=display[start:display.index('\n};',start)+4]
    for name in ('vision_hide_box','vision_map_x','vision_map_y','vision_apply_detection','vision_queue_drain'):
        body+=function(display,name)
    for name in ('npu_vision_coord','npu_publish_vision'):body+=function(npu,name)
    body+='''
int main(void){_Static_assert(sizeof(struct a733_vision_message_s)==32,"queue ABI");
assert(a733_vision_crc32((const uint8_t*)"123456789",9,0)==0x2dfd2d88);
lv_obj_t edges[7][4]={0},band={0};struct vision_scene_s scene={0};scene.band=&band;
scene.batch.require_crc=true;scene.batch.required_crc=0x01234567;
for(unsigned i=0;i<7;++i)for(unsigned j=0;j<4;++j)scene.edge[i][j]=&edges[i][j];
struct a733_npu_run_s run={.detection_count=3,.input_crc32=0x01234567};
for(unsigned i=0;i<3;++i)run.detections[i]=(struct a733_npu_detection_s){16,910,10000,20000,40000,50000};
npu_publish_vision(&run,1234);assert(count==4 && pending[0].version==2);
unsigned save=count;count=2;vision_queue_drain(7,&scene);assert(!scene.batch.complete);
for(unsigned i=0;i<7;++i)for(unsigned j=0;j<4;++j)assert(edges[i][j].hidden);
count=save;vision_queue_drain(7,&scene);assert(scene.batch.complete && band.color==0x25a65a);
for(unsigned i=0;i<3;++i)for(unsigned j=0;j<4;++j)assert(!edges[i][j].hidden);
count=read_index=0;run.input_crc32^=1;npu_publish_vision(&run,1);vision_queue_drain(7,&scene);
assert(!scene.batch.complete && !scene.batch.accepting && band.color==0xff6600);
for(unsigned i=0;i<7;++i)for(unsigned j=0;j<4;++j)assert(edges[i][j].hidden);
count=read_index=0;run.input_crc32^=1;send_limit=2;npu_publish_vision(&run,1);vision_queue_drain(7,&scene);
assert(!scene.batch.complete);send_limit=8;
count=read_index=0;npu_publish_vision(&run,1);pending[1].value^=1;vision_queue_drain(7,&scene);assert(!scene.batch.complete);
count=read_index=0;run.detection_count=0;npu_publish_vision(&run,1);vision_queue_drain(7,&scene);
assert(scene.batch.complete && !scene.batch.count);
struct a733_vision_batch_s b={0};struct a733_vision_message_s m=pending[0];m.sequence=0xffffffff;
assert(a733_vision_batch_feed(&b,&m)==A733_VISION_COMPLETE);m.sequence=1;
assert(a733_vision_batch_feed(&b,&m)==A733_VISION_COMPLETE);
assert(a733_vision_batch_feed(&b,&m)==A733_VISION_IGNORED);m.version=1;
assert(a733_vision_batch_feed(&b,&m)==A733_VISION_INVALID);
count=read_index=0;g_vision_sequence=0xffffffff;npu_publish_vision(&run,1);assert(pending[0].sequence==1);
assert(vision_map_x(640)==127 && vision_map_y(0)==24 && vision_map_y(640)==151);
puts("PASS: actual producer/MQ consumer, partial/complete/zero/mismatched CRC/cross-input batch, stale/version/wrap, square geometry; MOCK LVGL/NPU ONLY");}
'''
    c.write_text(body);subprocess.run([*flags,str(c),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print('PASS: exact production streaming model-image loader, RGB565 centre sampling and independent raw-NuttX CRC; short/long/missing rejection before LCD')
