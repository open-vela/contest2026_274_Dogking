/* SPDX-License-Identifier: Apache-2.0 */
/* Synthetic CPU fixtures, NOT real camera/ISP/NPU acceptance. */
#include <system/a733_camera_image.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned colour(unsigned order, unsigned x, unsigned y)
{
  static const unsigned table[4][4] =
  {{0,1,1,2},{1,0,2,1},{1,2,0,1},{2,1,1,0}};
  return table[order - 1][(y & 1) * 2 + (x & 1)];
}

static void raw_fixtures(void)
{
  for (unsigned order = 1; order <= 4; ++order)
    for (unsigned w = 4; w <= 12; w += 4)
      for (unsigned h = 2; h <= 8; h += 2)
        for (unsigned packed = 0; packed < 2; ++packed)
          {
            unsigned stride = (packed ? w / 4 * 5 : w * 2) + 7;
            uint8_t *data = calloc(stride, h);
            uint8_t output[12 * 8 * 3 + 2];
            struct a733_camera_transform t;
            unsigned values[] = {1023, 512, 128};
            for (unsigned y = 0; y < h; ++y)
              for (unsigned x = 0; x < w; ++x)
                {
                  unsigned v = values[colour(order,x,y)];
                  unsigned offset = y * stride + (packed ? x/4*5 : x*2);
                  if (packed)
                    {
                      data[offset+x%4] = v/4;
                      data[offset+4] |= (v%4) << (x%4*2);
                    }
                  else
                    {data[offset] = v%256; data[offset+1] = v/256;}
                }
            struct a733_camera_image s =
            {.data=data,.bytes=stride*h,.width=w,.height=h,.stride=stride,
             .pixel=packed?A733_CAMERA_RAW10_PACKED:A733_CAMERA_RAW10_LE16,
             .bayer=(enum a733_camera_bayer)order,.sequence=42,
             .timestamp_us=123456789};
            memset(output, 0xcd, sizeof(output));
            assert(a733_camera_letterbox(&s,output+1,w*h*3,w,h,114,&t)==0);
            assert(output[0]==0xcd && output[w*h*3+1]==0xcd);
            for (unsigned i=0;i<w*h;++i)
              {assert(output[1+i*3]==255);assert(output[2+i*3]==128);
               assert(output[3+i*3]==32);}
            assert(t.sequence==42 && t.timestamp_us==123456789);
            free(data);
          }
  /* Exercise the two low bits explicitly, including byte-boundary grouping. */
  uint8_t data[]={0,0,0,0,255,0,0,0,0,255};
  struct a733_camera_image s={.data=data,.bytes=10,.width=4,.height=2,
    .stride=5,.pixel=A733_CAMERA_RAW10_PACKED,.bayer=A733_CAMERA_RGGB};
  uint8_t output[24]; struct a733_camera_transform t;
  assert(a733_camera_letterbox(&s,output,24,4,2,114,&t)==0);
  for (unsigned i=0;i<24;++i) assert(output[i]==1);
}

static void yuv_fixtures(void)
{
  struct a733_camera_transform t;
  uint8_t rgb[12];
  uint8_t red601[]={81,81,81,81,90,240};
  struct a733_camera_image s={.data=red601,.bytes=6,.width=2,.height=2,
    .stride=2,.pixel=A733_CAMERA_NV12,.yuv=A733_CAMERA_BT601_LIMITED};
  assert(a733_camera_letterbox(&s,rgb,12,2,2,114,&t)==0);
  for(unsigned i=0;i<4;++i)
    {assert(rgb[i*3]>=253);assert(rgb[i*3+1]<=1);assert(rgb[i*3+2]<=1);}
  uint8_t red709[]={63,63,63,63,102,240}; s.data=red709;
  s.yuv=A733_CAMERA_BT709_LIMITED;
  assert(a733_camera_letterbox(&s,rgb,12,2,2,114,&t)==0);
  assert(rgb[0]>=253 && rgb[1]<=2 && rgb[2]<=2);
  uint8_t padded[]={16,235,99,99,16,235,99,99,128,128};
  s.data=padded;s.stride=4;s.bytes=10;s.yuv=A733_CAMERA_BT601_LIMITED;
  assert(a733_camera_letterbox(&s,rgb,12,2,2,114,&t)==0);
  for(unsigned i=0;i<3;++i) {assert(rgb[i]==0);assert(rgb[3+i]==255);}
  for(unsigned matrix=A733_CAMERA_BT601_FULL;
      matrix<=A733_CAMERA_BT709_FULL;++matrix)
    {
      uint8_t neutral[]={0,255,0,255,128,128};
      s.data=neutral;s.stride=2;s.bytes=6;s.yuv=(enum a733_camera_yuv)matrix;
      assert(a733_camera_letterbox(&s,rgb,12,2,2,114,&t)==0);
      for(unsigned i=0;i<3;++i){assert(rgb[i]==0);assert(rgb[3+i]==255);}
    }
}

static void geometry(void)
{
  size_t n=1920u*1080u*3u;
  uint8_t *data=malloc(n), *output=malloc(640u*640u*3u);
  assert(data && output);
  memset(data,50,n);
  struct a733_camera_image s={.data=data,.bytes=n,.width=1920,.height=1080,
    .stride=1920*3,.pixel=A733_CAMERA_RGB24,.sequence=7};
  struct a733_camera_transform t;
  assert(a733_camera_letterbox(&s,output,640u*640u*3u,640,640,114,&t)==0);
  assert(t.image_width==640 && t.image_height==360 && t.pad_x==0 && t.pad_y==140);
  assert(output[0]==114 && output[140*640*3]==50 && output[500*640*3]==114);
  struct a733_camera_box in={0,14000,64000,50000}, out={-1,-1,-1,-1};
  assert(a733_camera_unletterbox(&t,7,&in,&out)==0);
  assert(out.x1==0 && out.y1==0 && out.x2==192000 && out.y2==108000);
  in=(struct a733_camera_box){16000,23000,48000,41000};
  assert(a733_camera_unletterbox(&t,7,&in,&out)==0);
  assert(out.x1==48000 && out.y1==27000 && out.x2==144000 && out.y2==81000);
  assert(a733_camera_unletterbox(&t,8,&in,&out)==-ESTALE);
  in=(struct a733_camera_box){0,0,64000,10000};
  assert(a733_camera_unletterbox(&t,7,&in,&out)==-ENOENT);
  in=(struct a733_camera_box){INT32_MIN,INT32_MIN,INT32_MAX,INT32_MAX};
  assert(a733_camera_unletterbox(&t,7,&in,&out)==0);
  assert(out.x2==192000 && out.y2==108000);
  /* In-place box input/output is supported, even at clipped boundaries. */
  assert(a733_camera_unletterbox(&t,7,&in,&in)==0);
  assert(in.x2==192000);
  t.pad_x=UINT32_MAX;
  assert(a733_camera_unletterbox(&t,7,&in,&out)==-EINVAL);
  free(output); free(data);
}

static void rejects(void)
{
  uint8_t data[128]={0}, output[128], saved[128];
  struct a733_camera_transform t, ts;
  struct a733_camera_image s={.data=data,.bytes=12,.width=2,.height=2,
    .stride=6,.pixel=A733_CAMERA_RGB24};
  memset(output,0xcd,sizeof(output));memcpy(saved,output,sizeof(output));
  memset(&t,0xcd,sizeof(t)); memcpy(&ts,&t,sizeof(t));
  s.bytes=11;
  assert(a733_camera_letterbox(&s,output,12,2,2,114,&t)==-EMSGSIZE);
  assert(memcmp(saved,output,sizeof(output))==0 && memcmp(&t,&ts,sizeof(t))==0);
  s.bytes=12;
  assert(a733_camera_letterbox(&s,data,12,2,2,114,&t)==-EINVAL);
  assert(a733_camera_letterbox(&s,output,11,2,2,114,&t)==-EMSGSIZE);
  assert(a733_camera_letterbox(&s,output,128,0,2,114,&t)==-EINVAL);
  assert(a733_camera_letterbox(&s,output,128,641,2,114,&t)==-EINVAL);
  s.pixel=A733_CAMERA_NV12;s.stride=2;s.bytes=6;
  assert(a733_camera_letterbox(&s,output,12,2,2,114,&t)==-EINVAL);
  s.pixel=A733_CAMERA_RAW10_PACKED;s.bayer=A733_CAMERA_RGGB;
  assert(a733_camera_letterbox(&s,output,12,2,2,114,&t)==-EINVAL);
  s.pixel=A733_CAMERA_RAW10_LE16;s.stride=4;s.bytes=8;s.bayer=A733_CAMERA_BAYER_UNKNOWN;
  assert(a733_camera_letterbox(&s,output,12,2,2,114,&t)==-EINVAL);
  s.pixel=999;
  assert(a733_camera_letterbox(&s,output,12,2,2,114,&t)==-ENOTSUP);
}

int main(void)
{
  raw_fixtures();yuv_fixtures();geometry();rejects();
  puts("PASS: camera CPU pixels/stride/Bayer/YUV/letterbox/sequence/bounds; SYNTHETIC, no HW acceptance");
  return 0;
}
