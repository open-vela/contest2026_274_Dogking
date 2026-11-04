/* SPDX-License-Identifier: Apache-2.0 */
#include <system/a733_camera_image.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define SOURCE_LIMIT 8192u
#define OUTPUT_LIMIT 640u

static bool overlap(const void *a, size_t an, const void *b, size_t bn)
{
  uintptr_t ap = (uintptr_t)a;
  uintptr_t bp = (uintptr_t)b;
  if (an > UINTPTR_MAX - ap || bn > UINTPTR_MAX - bp)
    return true;
  return ap < bp + bn && bp < ap + an;
}

static uint8_t clip8(int value)
{
  return value < 0 ? 0 : value > 255 ? 255 : (uint8_t)value;
}

/* Explicit division, not a right shift of negative signed values. */
static int rounded256(int value)
{
  return value >= 0 ? (value + 128) / 256 : -((-value + 128) / 256);
}

static int validate(const struct a733_camera_image *s, size_t *required)
{
  uint64_t row;
  uint64_t size;
  if (!s || !s->data || !s->width || !s->height ||
      s->width > SOURCE_LIMIT || s->height > SOURCE_LIMIT)
    return -EINVAL;
  switch (s->pixel)
    {
      case A733_CAMERA_RGB24:
        row = (uint64_t)s->width * 3;
        break;
      case A733_CAMERA_NV12:
        if ((s->width | s->height) & 1u ||
            s->yuv < A733_CAMERA_BT601_LIMITED ||
            s->yuv > A733_CAMERA_BT709_FULL)
          return -EINVAL;
        row = s->width;
        break;
      case A733_CAMERA_RAW10_PACKED:
      case A733_CAMERA_RAW10_LE16:
        if (s->width < 2 || s->height < 2 ||
            ((s->width | s->height) & 1u) ||
            s->bayer < A733_CAMERA_RGGB || s->bayer > A733_CAMERA_BGGR)
          return -EINVAL;
        if (s->pixel == A733_CAMERA_RAW10_PACKED && s->width % 4)
          return -EINVAL;
        row = s->pixel == A733_CAMERA_RAW10_PACKED ?
              (uint64_t)s->width / 4 * 5 : (uint64_t)s->width * 2;
        break;
      default:
        return -ENOTSUP;
    }
  if (s->stride < row)
    return -EINVAL;
  size = (uint64_t)(s->height - 1) * s->stride + row;
  if (s->pixel == A733_CAMERA_NV12)
    size = (uint64_t)s->height * s->stride +
           (uint64_t)(s->height / 2 - 1) * s->stride + row;
  if (size > SIZE_MAX || size > s->bytes)
    return -EMSGSIZE;
  *required = (size_t)size;
  return 0;
}


static unsigned raw10(const struct a733_camera_image *s,
                       unsigned x, unsigned y)
{
  const uint8_t *row = s->data + (size_t)y * s->stride;
  if (s->pixel == A733_CAMERA_RAW10_LE16)
    return (row[x * 2] | (unsigned)row[x * 2 + 1] << 8) & 1023u;
  unsigned group = x / 4 * 5;
  unsigned within = x % 4;
  return (unsigned)row[group + within] << 2 |
         ((row[group + 4] >> (within * 2)) & 3u);
}

/* R=0 G=1 B=2. Account for actual orientation/negotiated Bayer order,
 * rather than assuming a sensor name uniquely fixes its memory format.
 */
static unsigned colour(enum a733_camera_bayer order, unsigned x, unsigned y)
{
  static const uint8_t patterns[4][4] =
  {
    {0, 1, 1, 2}, {1, 0, 2, 1}, {1, 2, 0, 1}, {2, 1, 1, 0}
  };
  return patterns[order - A733_CAMERA_RGGB][(y % 2) * 2 + x % 2];
}

static void pixel(const struct a733_camera_image *s, unsigned x, unsigned y,
                   uint8_t rgb[3])
{
  const uint8_t *row = s->data + (size_t)y * s->stride;
  if (s->pixel == A733_CAMERA_RGB24)
    {
      memcpy(rgb, row + (size_t)x * 3, 3);
      return;
    }
  if (s->pixel == A733_CAMERA_NV12)
    {
      const uint8_t *uv = s->data + (size_t)s->height * s->stride +
                          (size_t)(y / 2) * s->stride + (x & ~1u);
      int u = (int)uv[0] - 128;
      int v = (int)uv[1] - 128;
      int yy = row[x];
      int r, g, b;
      switch (s->yuv)
        {
          case A733_CAMERA_BT601_LIMITED:
            yy = 298 * (yy - 16);
            r = yy + 409 * v; g = yy - 100 * u - 208 * v;
            b = yy + 516 * u;
            break;
          case A733_CAMERA_BT709_LIMITED:
            yy = 298 * (yy - 16);
            r = yy + 459 * v; g = yy - 55 * u - 136 * v;
            b = yy + 541 * u;
            break;
          case A733_CAMERA_BT601_FULL:
            yy *= 256;
            r = yy + 359 * v; g = yy - 88 * u - 183 * v;
            b = yy + 454 * u;
            break;
          default: /* validated BT709 full range */
            yy *= 256;
            r = yy + 403 * v; g = yy - 48 * u - 120 * v;
            b = yy + 475 * u;
            break;
        }
      rgb[0] = clip8(rounded256(r));
      rgb[1] = clip8(rounded256(g));
      rgb[2] = clip8(rounded256(b));
      return;
    }

  unsigned own = colour(s->bayer, x, y);
  for (unsigned c = 0; c < 3; ++c)
    {
      unsigned sum = 0, count = 0;
      if (c == own)
        {
          sum = raw10(s, x, y);
          count = 1;
        }
      else
        {
          for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
              {
                int xx = (int)x + dx, yy = (int)y + dy;
                if (xx >= 0 && yy >= 0 && xx < (int)s->width &&
                    yy < (int)s->height &&
                    colour(s->bayer, (unsigned)xx, (unsigned)yy) == c)
                  {
                    sum += raw10(s, (unsigned)xx, (unsigned)yy);
                    ++count;
                  }
              }
        }
      /* A complete even 2x2 neighbourhood always supplies every colour. */
      rgb[c] = (uint8_t)(((sum / count) * 255u + 511u) / 1023u);
    }
}

static int geometry(const struct a733_camera_image *source,
                          uint8_t *output, size_t output_bytes,
                          uint32_t width, uint32_t height,
                          struct a733_camera_transform *transform)
{
  struct a733_camera_transform t = {0};
  size_t source_bytes;
  int ret = validate(source, &source_bytes);
  if (ret)
    return ret;
  if (!output || !transform || !width || !height ||
      width > OUTPUT_LIMIT || height > OUTPUT_LIMIT)
    return -EINVAL;
  size_t needed = (size_t)width * height * 3;
  if (needed > output_bytes)
    return -EMSGSIZE;
  if (overlap(source->data, source_bytes, output, needed) ||
      overlap(source, sizeof(*source), output, needed) ||
      overlap(transform, sizeof(*transform), output, needed) ||
      overlap(transform, sizeof(*transform), source->data, source_bytes) ||
      overlap(transform, sizeof(*transform), source, sizeof(*source)))
    return -EINVAL;

  t.source_width = source->width; t.source_height = source->height;
  t.output_width = width; t.output_height = height;
  if ((uint64_t)width * source->height <=
      (uint64_t)height * source->width)
    {
      t.image_width = width;
      t.image_height = ((uint64_t)source->height * width +
                        source->width / 2) / source->width;
      if (!t.image_height) t.image_height = 1;
    }
  else
    {
      t.image_height = height;
      t.image_width = ((uint64_t)source->width * height +
                       source->height / 2) / source->height;
      if (!t.image_width) t.image_width = 1;
    }
  t.pad_x = (width - t.image_width) / 2;
  t.pad_y = (height - t.image_height) / 2;
  t.sequence = source->sequence; t.timestamp_us = source->timestamp_us;
  *transform=t;
  return 0;
}
int a733_camera_letterbox(const struct a733_camera_image *source,
                          uint8_t *output,size_t output_bytes,
                          uint32_t width,uint32_t height,uint8_t padding,
                          struct a733_camera_transform *transform)
{
  int ret=geometry(source,output,output_bytes,width,height,transform);
  if(ret)return ret;
  struct a733_camera_transform t=*transform;
  size_t needed=(size_t)width*height*3;
  memset(output, padding, needed);
  for (uint32_t y = 0; y < t.image_height; ++y)
    for (uint32_t x = 0; x < t.image_width; ++x)
      {
        unsigned sx = ((uint64_t)(2 * x + 1) * source->width) /
                      (2 * t.image_width);
        unsigned sy = ((uint64_t)(2 * y + 1) * source->height) /
                      (2 * t.image_height);
        pixel(source, sx, sy,
              output + ((size_t)(y + t.pad_y) * width + x + t.pad_x) * 3);
      }
  *transform = t;
  return 0;
}

static int32_t bound(int32_t x, int32_t low, int32_t high)
{
  return x < low ? low : x > high ? high : x;
}

int a733_camera_unletterbox(const struct a733_camera_transform *t,
                            uint32_t sequence,
                            const struct a733_camera_box *model_box,
                            struct a733_camera_box *source_box)
{
  if (!t || !model_box || !source_box || !t->image_width ||
      !t->image_height || !t->source_width || !t->source_height ||
      t->source_width > SOURCE_LIMIT || t->source_height > SOURCE_LIMIT ||
      !t->output_width || !t->output_height ||
      t->output_width > OUTPUT_LIMIT || t->output_height > OUTPUT_LIMIT ||
      t->image_width > t->output_width || t->image_height > t->output_height ||
      t->pad_x > t->output_width - t->image_width ||
      t->pad_y > t->output_height - t->image_height)
    return -EINVAL;
  if (sequence != t->sequence)
    return -ESTALE;
  if (model_box->x1 >= model_box->x2 || model_box->y1 >= model_box->y2)
    return -EINVAL;
  int32_t left = (int32_t)t->pad_x * 100;
  int32_t top = (int32_t)t->pad_y * 100;
  int32_t right = left + (int32_t)t->image_width * 100;
  int32_t bottom = top + (int32_t)t->image_height * 100;
  struct a733_camera_box b =
  {
    bound(model_box->x1, left, right), bound(model_box->y1, top, bottom),
    bound(model_box->x2, left, right), bound(model_box->y2, top, bottom)
  };
  if (b.x1 >= b.x2 || b.y1 >= b.y2)
    return -ENOENT;
  b.x1 = (int64_t)(b.x1 - left) * t->source_width / t->image_width;
  b.x2 = (int64_t)(b.x2 - left) * t->source_width / t->image_width;
  b.y1 = (int64_t)(b.y1 - top) * t->source_height / t->image_height;
  b.y2 = (int64_t)(b.y2 - top) * t->source_height / t->image_height;
  *source_box = b;
  return 0;
}

/* v203 preview-only numerical tone curve; NOT sensor exposure control. */
static const uint8_t gamma22[1024] = {
  0,11,15,18,21,23,25,26,28,30,31,32,34,35,36,37,
  39,40,41,42,43,44,45,45,46,47,48,49,50,50,51,52,
  53,54,54,55,56,56,57,58,58,59,60,60,61,62,62,63,
  63,64,65,65,66,66,67,68,68,69,69,70,70,71,71,72,
  72,73,73,74,74,75,75,76,76,77,77,78,78,79,79,80,
  80,81,81,81,82,82,83,83,84,84,84,85,85,86,86,87,
  87,87,88,88,89,89,89,90,90,91,91,91,92,92,93,93,
  93,94,94,94,95,95,96,96,96,97,97,97,98,98,98,99,
  99,99,100,100,101,101,101,102,102,102,103,103,103,104,104,104,
  105,105,105,106,106,106,107,107,107,108,108,108,108,109,109,109,
  110,110,110,111,111,111,112,112,112,112,113,113,113,114,114,114,
  115,115,115,115,116,116,116,117,117,117,117,118,118,118,119,119,
  119,119,120,120,120,121,121,121,121,122,122,122,123,123,123,123,
  124,124,124,124,125,125,125,125,126,126,126,127,127,127,127,128,
  128,128,128,129,129,129,129,130,130,130,130,131,131,131,131,132,
  132,132,132,133,133,133,133,134,134,134,134,135,135,135,135,136,
  136,136,136,137,137,137,137,138,138,138,138,138,139,139,139,139,
  140,140,140,140,141,141,141,141,142,142,142,142,142,143,143,143,
  143,144,144,144,144,144,145,145,145,145,146,146,146,146,146,147,
  147,147,147,148,148,148,148,148,149,149,149,149,149,150,150,150,
  150,151,151,151,151,151,152,152,152,152,152,153,153,153,153,154,
  154,154,154,154,155,155,155,155,155,156,156,156,156,156,157,157,
  157,157,157,158,158,158,158,158,159,159,159,159,159,160,160,160,
  160,160,161,161,161,161,161,162,162,162,162,162,163,163,163,163,
  163,164,164,164,164,164,165,165,165,165,165,165,166,166,166,166,
  166,167,167,167,167,167,168,168,168,168,168,168,169,169,169,169,
  169,170,170,170,170,170,171,171,171,171,171,171,172,172,172,172,
  172,173,173,173,173,173,173,174,174,174,174,174,174,175,175,175,
  175,175,176,176,176,176,176,176,177,177,177,177,177,177,178,178,
  178,178,178,179,179,179,179,179,179,180,180,180,180,180,180,181,
  181,181,181,181,181,182,182,182,182,182,182,183,183,183,183,183,
  183,184,184,184,184,184,185,185,185,185,185,185,186,186,186,186,
  186,186,186,187,187,187,187,187,187,188,188,188,188,188,188,189,
  189,189,189,189,189,190,190,190,190,190,190,191,191,191,191,191,
  191,192,192,192,192,192,192,192,193,193,193,193,193,193,194,194,
  194,194,194,194,195,195,195,195,195,195,195,196,196,196,196,196,
  196,197,197,197,197,197,197,197,198,198,198,198,198,198,199,199,
  199,199,199,199,199,200,200,200,200,200,200,201,201,201,201,201,
  201,201,202,202,202,202,202,202,202,203,203,203,203,203,203,204,
  204,204,204,204,204,204,205,205,205,205,205,205,205,206,206,206,
  206,206,206,206,207,207,207,207,207,207,207,208,208,208,208,208,
  208,209,209,209,209,209,209,209,210,210,210,210,210,210,210,211,
  211,211,211,211,211,211,212,212,212,212,212,212,212,213,213,213,
  213,213,213,213,213,214,214,214,214,214,214,214,215,215,215,215,
  215,215,215,216,216,216,216,216,216,216,217,217,217,217,217,217,
  217,218,218,218,218,218,218,218,218,219,219,219,219,219,219,219,
  220,220,220,220,220,220,220,221,221,221,221,221,221,221,221,222,
  222,222,222,222,222,222,223,223,223,223,223,223,223,223,224,224,
  224,224,224,224,224,225,225,225,225,225,225,225,225,226,226,226,
  226,226,226,226,226,227,227,227,227,227,227,227,228,228,228,228,
  228,228,228,228,229,229,229,229,229,229,229,229,230,230,230,230,
  230,230,230,230,231,231,231,231,231,231,231,232,232,232,232,232,
  232,232,232,233,233,233,233,233,233,233,233,234,234,234,234,234,
  234,234,234,235,235,235,235,235,235,235,235,236,236,236,236,236,
  236,236,236,237,237,237,237,237,237,237,237,238,238,238,238,238,
  238,238,238,238,239,239,239,239,239,239,239,239,240,240,240,240,
  240,240,240,240,241,241,241,241,241,241,241,241,242,242,242,242,
  242,242,242,242,243,243,243,243,243,243,243,243,243,244,244,244,
  244,244,244,244,244,245,245,245,245,245,245,245,245,245,246,246,
  246,246,246,246,246,246,247,247,247,247,247,247,247,247,248,248,
  248,248,248,248,248,248,248,249,249,249,249,249,249,249,249,249,
  250,250,250,250,250,250,250,250,251,251,251,251,251,251,251,251,
  251,252,252,252,252,252,252,252,252,252,253,253,253,253,253,253,
  253,253,254,254,254,254,254,254,254,254,254,255,255,255,255,255,
};
int a733_camera_tone_valid(const struct a733_camera_tone *t)
{
  if(!t || t->black>255 || (t->rotation!=0 && t->rotation!=180))return -EINVAL;
  const uint32_t gains[4]={t->gain_q8,t->red_q8,t->green_q8,t->blue_q8};
  for(unsigned i=0;i<4;++i)if(gains[i]<64 || gains[i]>2048)return -ERANGE;
  return 0;
}
int a733_camera_letterbox_tone(const struct a733_camera_image *s,
  uint8_t *out,size_t bytes,uint32_t width,uint32_t height,uint8_t padding,
  const struct a733_camera_tone *t,struct a733_camera_transform *transform)
{
  int ret=a733_camera_tone_valid(t);if(ret)return ret;
  struct a733_camera_tone owned=*t;t=&owned;
  if(!s || (s->pixel!=A733_CAMERA_RAW10_LE16 && s->pixel!=A733_CAMERA_RAW10_PACKED))return -ENOTSUP;
  /* Shared validation/geometry, but no throwaway linear demosaic pass.
   * v203 pixel output stays byte-exact; live conversion performs one pass. */
  ret=geometry(s,out,bytes,width,height,transform);
  if(ret)return ret;
  memset(out,padding,(size_t)width*height*3);
  const uint32_t gains[3]={t->red_q8,t->green_q8,t->blue_q8};
  for(unsigned y=0;y<transform->image_height;++y)
    for(unsigned x=0;x<transform->image_width;++x)
      {
        unsigned sx=((uint64_t)(2*x+1)*s->width)/(2*transform->image_width);
        unsigned sy=((uint64_t)(2*y+1)*s->height)/(2*transform->image_height);
        unsigned own=colour(s->bayer,sx,sy);
        size_t dest=((size_t)(y+transform->pad_y)*width+x+transform->pad_x);
        if(t->rotation==180)dest=((size_t)(transform->image_height-1-y+transform->pad_y)*width+
                                  transform->image_width-1-x+transform->pad_x);
        for(unsigned c=0;c<3;++c)
          {
            unsigned sum=0,count=0;
            if(c==own){sum=raw10(s,sx,sy);count=1;}
            else for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)
              {
                int xx=(int)sx+dx,yy=(int)sy+dy;
                if(xx>=0 && yy>=0 && xx<(int)s->width && yy<(int)s->height &&
                   colour(s->bayer,(unsigned)xx,(unsigned)yy)==c)
                  {sum+=raw10(s,(unsigned)xx,(unsigned)yy);++count;}
              }
            unsigned value=sum/count;
            value=value>t->black?(value-t->black)*1023/(1023-t->black):0;
            uint64_t scaled=(uint64_t)value*t->gain_q8*gains[c]+32768;
            value=(unsigned)(scaled/65536);if(value>1023)value=1023;
            out[dest*3+c]=gamma22[value];
          }
      }
  return 0;
}

struct a733_camera_tone_plan
{
  struct a733_camera_image source;
  struct a733_camera_transform transform;
  uint8_t *output,padding,lut[3][1024];
  uint16_t x[OUTPUT_LIMIT],y[OUTPUT_LIMIT];
  uint8_t own[2][OUTPUT_LIMIT],horizontal[2][OUTPUT_LIMIT];
  bool rotated;
};

int a733_camera_tone_plan_create(const struct a733_camera_image *s,uint8_t *out,
  size_t bytes,uint32_t width,uint32_t height,uint8_t padding,
  const struct a733_camera_tone *tone,struct a733_camera_tone_plan **result)
{
  if(!result)return -EINVAL;
  *result=NULL;
  int ret=a733_camera_tone_valid(tone);if(ret)return ret;
  if(!s || (s->pixel!=A733_CAMERA_RAW10_LE16 && s->pixel!=A733_CAMERA_RAW10_PACKED))return -ENOTSUP;
  struct a733_camera_transform t;
  ret=geometry(s,out,bytes,width,height,&t);if(ret)return ret;
  struct a733_camera_tone_plan *p=calloc(1,sizeof(*p));if(!p)return -ENOMEM;
  p->source=*s;p->transform=t;p->output=out;p->padding=padding;p->rotated=tone->rotation==180;
  const uint32_t gains[3]={tone->red_q8,tone->green_q8,tone->blue_q8};
  for(unsigned c=0;c<3;++c)for(unsigned raw=0;raw<1024;++raw)
    {
      unsigned v=raw>tone->black?(raw-tone->black)*1023/(1023-tone->black):0;
      uint64_t scaled=(uint64_t)v*tone->gain_q8*gains[c]+32768;
      v=(unsigned)(scaled/65536);if(v>1023)v=1023;
      p->lut[c][raw]=gamma22[v];
    }
  for(unsigned x=0;x<t.image_width;++x)
    {
      p->x[x]=(uint16_t)(((uint64_t)(2*x+1)*s->width)/(2*t.image_width));
      for(unsigned parity=0;parity<2;++parity)
        {
          p->own[parity][x]=(uint8_t)colour(s->bayer,p->x[x],parity);
          p->horizontal[parity][x]=(uint8_t)colour(s->bayer,p->x[x]^1u,parity);
        }
    }
  for(unsigned y=0;y<t.image_height;++y)p->y[y]=(uint16_t)(((uint64_t)(2*y+1)*s->height)/(2*t.image_height));
  *result=p;return 0;
}

/* Same truncated bilinear average as the reference, including edge counts.
 * Interior Bayer sites have fixed 2/4-neighbour cases, not 3x3 colour loops. */
static void tone_site(const struct a733_camera_image *s,unsigned x,unsigned y,
                      unsigned value[3])
{
  unsigned own=colour(s->bayer,x,y);value[own]=raw10(s,x,y);
  if(x && y && x+1<s->width && y+1<s->height)
    {
      if(own==1)
        {
          unsigned horizontal=colour(s->bayer,x^1u,y);
          value[horizontal]=(raw10(s,x-1,y)+raw10(s,x+1,y))/2;
          value[2-horizontal]=(raw10(s,x,y-1)+raw10(s,x,y+1))/2;
        }
      else
        {
          value[1]=(raw10(s,x-1,y)+raw10(s,x+1,y)+raw10(s,x,y-1)+raw10(s,x,y+1))/4;
          value[2-own]=(raw10(s,x-1,y-1)+raw10(s,x+1,y-1)+raw10(s,x-1,y+1)+raw10(s,x+1,y+1))/4;
        }
      return;
    }
  for(unsigned c=0;c<3;++c)if(c!=own)
    {
      unsigned sum=0,count=0;
      for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)
        {
          int xx=(int)x+dx,yy=(int)y+dy;
          if(xx>=0 && yy>=0 && xx<(int)s->width && yy<(int)s->height &&
             colour(s->bayer,(unsigned)xx,(unsigned)yy)==c)
            {sum+=raw10(s,(unsigned)xx,(unsigned)yy);++count;}
        }
      value[c]=sum/count;
    }
}

static inline __attribute__((always_inline)) unsigned tone_word(const uint8_t *p)
{
  /* memcpy is safe on odd strides/unaligned sources and avoids aliasing UB.
   * RAW10 is little-endian even on a big-endian host. Same high-bit mask. */
  uint16_t v;__builtin_memcpy(&v,p,sizeof(v));
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
  v=__builtin_bswap16(v);
#endif
  return v&1023u;
}
int a733_camera_tone_plan_render(struct a733_camera_tone_plan *p,
  const struct a733_camera_image *s,struct a733_camera_transform *t)
{
  if(!p || !s || !t)return -EINVAL;
  const struct a733_camera_image *a=&p->source;
  if(s->data!=a->data || s->bytes!=a->bytes || s->width!=a->width ||
     s->height!=a->height || s->stride!=a->stride || s->pixel!=a->pixel ||
     s->bayer!=a->bayer || s->yuv!=a->yuv)return -ESTALE;
  size_t bytes=(size_t)p->transform.output_width*p->transform.output_height*3;
  if(overlap(t,sizeof(*t),s->data,s->bytes) || overlap(t,sizeof(*t),p->output,bytes) ||
     overlap(t,sizeof(*t),p,sizeof(*p)))return -EINVAL;
  struct a733_camera_transform shape=p->transform;
  shape.sequence=s->sequence;shape.timestamp_us=s->timestamp_us;
  /* Every active image pixel is overwritten below. Fill ONLY letterbox
   * padding, including unequal odd right/bottom bars. No stale bytes. */
  size_t pitch=(size_t)shape.output_width*3;
  memset(p->output,p->padding,(size_t)shape.pad_y*pitch);
  for(unsigned y=shape.pad_y;y<shape.pad_y+shape.image_height;++y)
    {
      uint8_t *line=p->output+(size_t)y*pitch;
      memset(line,p->padding,(size_t)shape.pad_x*3);
      size_t end=(size_t)(shape.pad_x+shape.image_width)*3;
      memset(line+end,p->padding,pitch-end);
    }
  size_t bottom=(size_t)(shape.pad_y+shape.image_height)*pitch;
  memset(p->output+bottom,p->padding,bytes-bottom);
  for(unsigned y=0;y<shape.image_height;++y)
    {
      unsigned sy=p->y[y];
      unsigned oy=p->rotated?shape.image_height-1-y:y;
      uint8_t *dst=p->output+((size_t)(oy+shape.pad_y)*shape.output_width+shape.pad_x)*3;
      const uint8_t *row=s->data+(size_t)sy*s->stride;
      for(unsigned x=0;x<shape.image_width;++x)
        {
          unsigned sx=p->x[x],v[3];
          if(s->pixel==A733_CAMERA_RAW10_LE16 && sx && sy && sx+1<s->width && sy+1<s->height)
            {
              const uint8_t *m=row+2*sx,*u=m-s->stride,*d=m+s->stride;
              unsigned own=p->own[sy&1u][x];v[own]=tone_word(m);
              if(own==1)
                {
                  unsigned h=p->horizontal[sy&1u][x];
                  v[h]=(tone_word(m-2)+tone_word(m+2))/2;
                  v[2-h]=(tone_word(u)+tone_word(d))/2;
                }
              else
                {
                  v[1]=(tone_word(m-2)+tone_word(m+2)+tone_word(u)+tone_word(d))/4;
                  v[2-own]=(tone_word(u-2)+tone_word(u+2)+tone_word(d-2)+tone_word(d+2))/4;
                }
            }
          else tone_site(s,sx,sy,v); /* Keep reference edge/packed semantics. */
          unsigned ox=p->rotated?shape.image_width-1-x:x;
          dst[ox*3]=p->lut[0][v[0]];dst[ox*3+1]=p->lut[1][v[1]];dst[ox*3+2]=p->lut[2][v[2]];
        }
    }
  *t=shape;return 0;
}
void a733_camera_tone_plan_destroy(struct a733_camera_tone_plan *p){free(p);}
