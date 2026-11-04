/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "../board/a733-cubie-a7z/openvela-overlay/apps/system/a733camera/a733_camera_image.c"
int main(void)
{
  assert(gamma22[0]==0 && gamma22[1023]==255);
  for(unsigned i=1;i<1024;++i)assert(gamma22[i]>=gamma22[i-1]);
  uint8_t raw[32],a[4*5*3],b[4*5*3];
  for(unsigned i=0;i<16;++i){unsigned v=64+i*43;raw[i*2]=v&255;raw[i*2+1]=v>>8;}
  struct a733_camera_image s={.data=raw,.bytes=32,.width=4,.height=4,.stride=8,
    .pixel=A733_CAMERA_RAW10_LE16,.bayer=A733_CAMERA_BGGR};
  struct a733_camera_tone t={64,512,512,256,256,0};
  struct a733_camera_transform ta,tb;
  assert(!a733_camera_letterbox_tone(&s,a,sizeof(a),4,5,114,&t,&ta));
  t.rotation=180;assert(!a733_camera_letterbox_tone(&s,b,sizeof(b),4,5,114,&t,&tb));
  for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x)
    assert(!memcmp(a+(y*4+x)*3,b+((3-y)*4+3-x)*3,3));
  for(unsigned i=48;i<60;++i)assert(a[i]==114 && b[i]==114);
  memset(b,0x85,sizeof(b));t.black=256;
  assert(a733_camera_letterbox_tone(&s,b,sizeof(b),4,5,114,&t,&tb)==-EINVAL);
  for(unsigned i=0;i<sizeof(b);++i)assert(b[i]==0x85);
  t.black=64;t.rotation=90;assert(a733_camera_tone_valid(&t)==-EINVAL);
  t.rotation=0;t.gain_q8=0;assert(a733_camera_tone_valid(&t)==-ERANGE);
  puts("PASS v203 tone: gamma monotonic/endpoints; exact180 geometry incl odd padding; invalid options no output mutation");
}
