#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Actual synchronous display frame adapter, mocked LVGL/SPI. No panel."""
from pathlib import Path
import subprocess,tempfile
repo=Path(__file__).resolve().parents[1];apps=repo/'board/a733-cubie-a7z/openvela-overlay/apps'
source=(apps/'system/a733display/a733display_main.c').read_text()
start=source.index('struct live_display_s\n');struct=source[start:source.index('\n};',start)+4]
start=source.index('static uint64_t display_live_now(');end=source.index('\nint a733_display_live_end(',start);frame=source[start:end]
with tempfile.TemporaryDirectory(prefix='live-display-204-') as tmp:
    tmp=Path(tmp);c=tmp/'display.c';exe=tmp/'test'
    c.write_text(r'''
#include <system/a733_camera_live.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
typedef int lv_display_t;
typedef struct {bool hidden;char text[40];int x,y;} lv_obj_t;
struct vision_scene_s {lv_obj_t *screen,*band,*image_object,*edge[7][4];int image;};
static unsigned mutations,flushes;
static lv_obj_t *last_invalidated;
static int g_lv_flush_status;
static void lv_image_cache_drop(void *p){(void)p;++mutations;}
static void lv_obj_add_flag(lv_obj_t *p,int f){(void)f;p->hidden=true;++mutations;}
static void lv_obj_remove_flag(lv_obj_t *p,int f){(void)f;p->hidden=false;++mutations;}
static void lv_obj_set_pos(lv_obj_t *p,int x,int y){p->x=x;p->y=y;++mutations;}
static void lv_label_set_text_fmt(lv_obj_t *p,const char *fmt,...)
{va_list args;va_start(args,fmt);vsnprintf(p->text,sizeof(p->text),fmt,args);va_end(args);++mutations;}
static void lv_obj_set_style_bg_color(lv_obj_t *p,unsigned color,int s){(void)p;(void)color;(void)s;++mutations;}
static unsigned lv_color_hex(unsigned n){return n;}
static void lv_tick_inc(uint32_t n){(void)n;}
static void lv_obj_invalidate(lv_obj_t *p){assert(p);last_invalidated=p;++mutations;}
static void lv_refr_now(lv_display_t *p){(void)p;++flushes;}
static int a733_spibus_last_error(unsigned n){assert(n==1);return 0;}
static uint64_t display_micros(const struct timespec *t){return (uint64_t)t->tv_sec*1000000+t->tv_nsec/1000;}
static int vision_map_x(uint16_t n){int x=n*128/640;return x>127?127:x;}
static int vision_map_y(uint16_t n){int y=24+n*128/640;return y>151?151:y;}
static void vision_hide_box(struct vision_scene_s *s,unsigned n){for(unsigned i=0;i<4;++i)s->edge[n][i]->hidden=true;}
static void vision_apply_detection(struct vision_scene_s *s,const struct a733_vision_message_s *b)
{for(unsigned i=0;i<4;++i)s->edge[b->index][i]->hidden=false;}
#define LV_OBJ_FLAG_HIDDEN 1
'''+struct+'\nstatic struct live_display_s *live_display;\n'+frame+r'''
int main(void)
{
  uint8_t *rgb=calloc(1,A733_LIVE_RGB_BYTES);assert(rgb);
  struct live_display_s live={0};live_display=&live;
  lv_obj_t screen={0},band={0},status={0},image={0},edges[7][4]={0},labels[7]={0};
  live.scene.screen=&screen;live.scene.band=&band;live.scene.image_object=&image;live.status=&status;
  for(unsigned i=0;i<7;++i){live.labels[i]=labels+i;for(unsigned j=0;j<4;++j)live.scene.edge[i][j]=&edges[i][j];}
  struct a733_live_result r={.sequence=1};
  assert(!a733_display_live_frame(rgb,A733_LIVE_RGB_BYTES,&r));
  assert(last_invalidated==&image && last_invalidated!=&screen);
  assert(!strcmp(status.text,"F1 VIEW") && !live.completed);
  r.kind=1;r.count=1;r.boxes[0]=(struct a733_vision_message_s){.magic=A733_VISION_MAGIC,
    .version=A733_VISION_VERSION,.type=A733_VISION_DETECTION,.sequence=1,.count=1,.score_milli=910,
    .class_id=16,.x1=100,.y1=200,.x2=400,.y2=500};
  assert(!a733_display_live_frame(rgb,A733_LIVE_RGB_BYTES,&r));
  assert(live.completed && !labels[0].hidden && !strcmp(labels[0].text,"16:91%"));
  unsigned before=mutations;
  assert(a733_display_live_frame(rgb,A733_LIVE_RGB_BYTES,&r)==-ESTALE && mutations==before);
  memset(rgb,3,A733_LIVE_RGB_BYTES);memset(&r,0,sizeof(r));r.sequence=2;r.input_crc=a733_vision_crc32(rgb,A733_LIVE_RGB_BYTES,0);
  assert(!a733_display_live_frame(rgb,A733_LIVE_RGB_BYTES,&r) && labels[0].hidden && !live.completed);
  before=mutations;r.input_crc^=1;r.kind=1;
  assert(a733_display_live_frame(rgb,A733_LIVE_RGB_BYTES,&r)==-ESTALE && mutations==before);
  r.input_crc^=1;rgb[10]^=1;
  assert(a733_display_live_frame(rgb,A733_LIVE_RGB_BYTES,&r)==-EILSEQ && mutations==before);rgb[10]^=1;
  r.sequence=3;
  assert(a733_display_live_frame(rgb,A733_LIVE_RGB_BYTES,&r)==-ESTALE && mutations==before);r.sequence=2;
  assert(!a733_display_live_frame(rgb,A733_LIVE_RGB_BYTES,&r) && labels[0].hidden);
  for(unsigned i=1;i<100001;++i){r.sequence=i;assert(!a733_live_result_valid(&r));}
  r.kind=2;assert(a733_live_result_valid(&r)==-EINVAL);r.kind=1;r.count=8;assert(a733_live_result_valid(&r)==-EINVAL);
  assert(flushes==4);
  memset(&r,0,sizeof(r));r.sequence=100002;r.kind=1;
  r.input_crc=a733_vision_crc32(rgb,A733_LIVE_RGB_BYTES,0);
  struct a733_live_display_profile profile;
  assert(!a733_display_live_present(rgb,A733_LIVE_RGB_BYTES,&r,&profile));
  assert(flushes==5 && live.completed && labels[0].hidden);
  before=mutations;assert(a733_display_live_present(rgb,A733_LIVE_RGB_BYTES,&r,&profile)==-ESTALE && mutations==before);
  r.sequence++;r.input_crc^=1;
  assert(a733_display_live_present(rgb,A733_LIVE_RGB_BYTES,&r,&profile)==-EILSEQ && mutations==before);
  r.input_crc^=1;
  assert(!a733_display_live_present(rgb,A733_LIVE_RGB_BYTES,&r,&profile) && flushes==6);
  ++r.sequence;r.kind=A733_LIVE_PREVIEW;
  assert(!a733_display_live_present(rgb,A733_LIVE_RGB_BYTES,&r,&profile) && flushes==7 && !live.completed);
  assert(a733_display_live_present(rgb,A733_LIVE_RGB_BYTES,&r,&profile)==-ESTALE && flushes==7);
  free(rgb);
  puts("PASS actual live display frame: exact pixels/CRC/sequence, stale/duplicate/malformed no mutation, zero clears labels;100000 metadata checks; MOCK LVGL ONLY");
}
''')
    subprocess.run(['gcc','-std=c11','-D_DEFAULT_SOURCE','-O1','-g','-Wall','-Wextra','-Werror','-fno-pie','-no-pie',
      '-fsanitize=address,undefined','-I'+str(apps/'include'),str(c),'-o',str(exe)],check=True,timeout=30)
    subprocess.run([str(exe)],check=True,timeout=20)
