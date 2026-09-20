/****************************************************************************
 * vendor/rockchip/boards/rv1103/luckfox-pico-mini/src/luckfox_sc3336.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_RV1103_SC3336_CHECKPOINT

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>

#include <nuttx/arch.h>
#include <nuttx/cache.h>
#include <nuttx/clock.h>
#include <nuttx/fs/fs.h>
#include <nuttx/i2c/i2c_master.h>
#include <nuttx/kmalloc.h>
#include <nuttx/mutex.h>
#include <nuttx/signal.h>
#include <nuttx/video/video.h>

#include <sys/video_controls.h>

#include <arch/chip/rv1103_gpio.h>
#include <arch/chip/rv1103_periph.h>

/* Official rv1103-luckfox-pico-ipc.dtsi wiring. */

#define RV1103_CRU_BASE                  0xff3a0000u
#define RV1103_TOPCRU_BASE               0x00010000u
#define RV1103_CLKSEL_CON(n)             \
  (RV1103_CRU_BASE + RV1103_TOPCRU_BASE + 0x300u + (n) * 4u)
#define RV1103_CLKGATE_CON(n)            \
  (RV1103_CRU_BASE + RV1103_TOPCRU_BASE + 0x800u + (n) * 4u)
#define RV1103_MIPI0_OUT_SEL_MASK        (3u << 0)
#define RV1103_MIPI0_OUT_SEL_XIN24M      (2u << 0)
#define RV1103_MIPI0_OUT_GATE            (1u << 6)

#define SC3336_I2C_ADDRESS               0x30
#define SC3336_I2C_FREQUENCY             400000
#define SC3336_REG_CHIP_ID               0x3107
#define SC3336_CHIP_ID                   0xcc41
#define SC3336_REG_CTRL_MODE             0x0100
#define SC3336_REG_GROUP_HOLD            0x3812
#define SC3336_REG_EXPOSURE_H            0x3e00
#define SC3336_REG_EXPOSURE_M            0x3e01
#define SC3336_REG_EXPOSURE_L            0x3e02
#define SC3336_REG_DIG_GAIN              0x3e06
#define SC3336_REG_DIG_FINE_GAIN         0x3e07
#define SC3336_REG_ANA_GAIN              0x3e09
#define SC3336_REG_VTS_H                 0x320e
#define SC3336_REG_VTS_L                 0x320f
#define SC3336_VTS_DEFAULT               0x0550u
#define SC3336_VTS_MAX                   0x7fffu
#define SC3336_GAIN_MIN                  128u
#define SC3336_GAIN_MAX                  99614u
#define SC3336_MCLK_PIN                  20 /* GPIO3_C4 */
#define SC3336_PWDN_PIN                  21 /* GPIO3_C5 */

static char g_sc3336_status[512];

#define RV1103_VICRU_BASE               0xff3b4000u
#define RV1103_RKCIF_BASE               0xffa10000u
#define SC3336_WIDTH                     2304u
#define SC3336_HEIGHT                    1296u
#define SC3336_RAW10_STRIDE              3072u
#define SC3336_FRAME_SIZE                \
  (SC3336_RAW10_STRIDE * SC3336_HEIGHT)
#define SC3336_ISP_Y_SIZE                (SC3336_WIDTH * SC3336_HEIGHT)
#define SC3336_ISP_NV12_SIZE             (SC3336_ISP_Y_SIZE * 3u / 2u)
#define SC3336_ISP_ALLOC_SIZE            (SC3336_ISP_Y_SIZE * 2u + 4096u)

static FAR uint8_t *g_sc3336_frame;
static size_t g_sc3336_frame_size;
static FAR uint8_t *g_sc3336_video_frame;
static FAR uint8_t *g_sc3336_isp_frame;
static size_t g_sc3336_isp_frame_size;
static uint32_t g_sc3336_isp_hash;
static uint32_t g_sc3336_isp_burst_frames;
static uint32_t g_sc3336_isp_burst_msec;
static uint32_t g_sc3336_video_hash;
static uint32_t g_sc3336_video_frames;
static uint32_t g_sc3336_video_msec;
static FAR struct i2c_master_s *g_sc3336_i2c;
static mutex_t g_sc3336_lock = NXMUTEX_INITIALIZER;
static mutex_t g_sc3336_capture_lock = NXMUTEX_INITIALIZER;
static uint32_t g_sc3336_exposure = 0x0548u;
static uint32_t g_sc3336_gain = SC3336_GAIN_MIN;
static uint32_t g_sc3336_vts = SC3336_VTS_DEFAULT;
static bool g_sc3336_streaming;
static uint32_t g_sc3336_isp_frames;
static uint32_t g_sc3336_isp_ris;
static uint32_t g_sc3336_isp_err;

static int sc3336_capture_burst(void);
static int sc3336_isp_input_check(void);
static int sc3336_isp_mi_capture(void);
static int sc3336_write_reg(FAR struct i2c_master_s *i2c, uint16_t reg,
                            uint8_t value);

static int sc3336_sensor_stream(bool enable)
{
  int ret;

  if (g_sc3336_i2c == NULL)
    {
      return -ENODEV;
    }

  ret = sc3336_write_reg(g_sc3336_i2c, SC3336_REG_CTRL_MODE,
                         enable ? 1 : 0);
  if (ret >= 0)
    {
      g_sc3336_streaming = enable;
      nxsig_usleep(enable ? 100000 : 35000);
    }

  return ret;
}

static int sc3336_set_exposure(uint32_t exposure)
{
  int ret;

  if (exposure < 1 || exposure > g_sc3336_vts - 8)
    {
      return -ERANGE;
    }

  ret = sc3336_write_reg(g_sc3336_i2c, SC3336_REG_GROUP_HOLD, 0x00);
  ret |= sc3336_write_reg(g_sc3336_i2c, SC3336_REG_EXPOSURE_H,
                          (exposure >> 12) & 0x0f);
  ret |= sc3336_write_reg(g_sc3336_i2c, SC3336_REG_EXPOSURE_M,
                          (exposure >> 4) & 0xff);
  ret |= sc3336_write_reg(g_sc3336_i2c, SC3336_REG_EXPOSURE_L,
                          (exposure & 0x0f) << 4);
  ret |= sc3336_write_reg(g_sc3336_i2c, SC3336_REG_GROUP_HOLD, 0x30);
  if (ret >= 0)
    {
      g_sc3336_exposure = exposure;
    }

  return ret;
}

static int sc3336_set_gain(uint32_t gain)
{
  uint32_t again;
  uint32_t dgain;
  uint32_t fine;
  uint32_t factor;
  int ret;

  if (gain < SC3336_GAIN_MIN || gain > SC3336_GAIN_MAX)
    {
      return -ERANGE;
    }

  factor = gain * 1000u / 128u;
  if (factor < 1520u)
    {
      again = 0x00; dgain = 0x00; fine = factor * 128u / 1000u;
    }
  else if (factor < 3040u)
    {
      again = 0x40; dgain = 0x00; fine = factor * 128u / 1520u;
    }
  else if (factor < 6080u)
    {
      again = 0x48; dgain = 0x00; fine = factor * 128u / 3040u;
    }
  else if (factor < 12160u)
    {
      again = 0x49; dgain = 0x00; fine = factor * 128u / 6080u;
    }
  else if (factor < 24320u)
    {
      again = 0x4b; dgain = 0x00; fine = factor * 128u / 12160u;
    }
  else if (factor < 48640u)
    {
      again = 0x4f; dgain = 0x00; fine = factor * 128u / 24320u;
    }
  else
    {
      again = 0x5f;
      if (factor < 97280u)
        {
          dgain = 0x00; fine = factor * 128u / 48640u;
        }
      else if (factor < 194560u)
        {
          dgain = 0x01; fine = factor * 128u / 97280u;
        }
      else if (factor < 389120u)
        {
          dgain = 0x03; fine = factor * 128u / 194560u;
        }
      else
        {
          dgain = 0x07; fine = factor * 128u / 389120u;
        }
    }

  if (fine > 0xffu)
    {
      fine = 0xffu;
    }

  ret = sc3336_write_reg(g_sc3336_i2c, SC3336_REG_GROUP_HOLD, 0x00);
  ret |= sc3336_write_reg(g_sc3336_i2c, SC3336_REG_DIG_GAIN, dgain);
  ret |= sc3336_write_reg(g_sc3336_i2c, SC3336_REG_DIG_FINE_GAIN, fine);
  ret |= sc3336_write_reg(g_sc3336_i2c, SC3336_REG_ANA_GAIN, again);
  ret |= sc3336_write_reg(g_sc3336_i2c, SC3336_REG_GROUP_HOLD, 0x30);
  if (ret >= 0)
    {
      g_sc3336_gain = gain;
    }

  return ret;
}

static int sc3336_set_fps(uint32_t fps)
{
  uint32_t vts;
  int ret;

  if (fps < 5 || fps > 30)
    {
      return -ERANGE;
    }

  vts = SC3336_VTS_DEFAULT * 30u / fps;
  if (vts > SC3336_VTS_MAX)
    {
      return -ERANGE;
    }

  ret = sc3336_write_reg(g_sc3336_i2c, SC3336_REG_GROUP_HOLD, 0x00);
  ret |= sc3336_write_reg(g_sc3336_i2c, SC3336_REG_VTS_H, vts >> 8);
  ret |= sc3336_write_reg(g_sc3336_i2c, SC3336_REG_VTS_L, vts & 0xff);
  ret |= sc3336_write_reg(g_sc3336_i2c, SC3336_REG_GROUP_HOLD, 0x30);
  if (ret >= 0)
    {
      g_sc3336_vts = vts;
      if (g_sc3336_exposure > vts - 8)
        {
          ret = sc3336_set_exposure(vts - 8);
        }
    }

  return ret;
}

struct sc3336_reg_s
{
  uint16_t reg;
  uint8_t value;
};

/* Verbatim values from the official Linux sc3336.c 24-MHz/30-fps mode. */
static const struct sc3336_reg_s g_sc3336_30fps[] =
{
  {0x0103,0x01},{0x36e9,0x80},{0x37f9,0x80},{0x301f,0x02},
  {0x30b8,0x33},{0x320e,0x05},{0x320f,0x50},{0x3253,0x10},
  {0x325f,0x20},{0x3301,0x04},{0x3306,0x50},{0x330a,0x00},
  {0x330b,0xd8},{0x3314,0x13},{0x3333,0x10},{0x3334,0x40},
  {0x335e,0x06},{0x335f,0x0a},{0x3364,0x5e},{0x337c,0x02},
  {0x337d,0x0e},{0x3390,0x01},{0x3391,0x03},{0x3392,0x07},
  {0x3393,0x04},{0x3394,0x04},{0x3395,0x04},{0x3396,0x08},
  {0x3397,0x0b},{0x3398,0x1f},{0x3399,0x04},{0x339a,0x0a},
  {0x339b,0x3a},{0x339c,0xc4},{0x33a2,0x04},{0x33ac,0x08},
  {0x33ad,0x1c},{0x33ae,0x10},{0x33af,0x30},{0x33b1,0x80},
  {0x33b3,0x48},{0x33f9,0x60},{0x33fb,0x74},{0x33fc,0x4b},
  {0x33fd,0x5f},{0x349f,0x03},{0x34a6,0x4b},{0x34a7,0x5f},
  {0x34a8,0x20},{0x34a9,0x18},{0x34ab,0xe8},{0x34ac,0x01},
  {0x34ad,0x00},{0x34f8,0x5f},{0x34f9,0x18},{0x3630,0xc0},
  {0x3631,0x84},{0x3632,0x64},{0x3633,0x32},{0x363b,0x03},
  {0x363c,0x08},{0x3641,0x38},{0x3670,0x4e},{0x3674,0xc0},
  {0x3675,0xc0},{0x3676,0xc0},{0x3677,0x84},{0x3678,0x8a},
  {0x3679,0x8c},{0x367c,0x48},{0x367d,0x49},{0x367e,0x4b},
  {0x367f,0x5f},{0x3690,0x33},{0x3691,0x33},{0x3692,0x44},
  {0x369c,0x4b},{0x369d,0x5f},{0x36b0,0x87},{0x36b1,0x90},
  {0x36b2,0xa1},{0x36b3,0xd8},{0x36b4,0x49},{0x36b5,0x4b},
  {0x36b6,0x4f},{0x36ea,0x11},{0x36eb,0x0d},{0x36ec,0x1c},
  {0x36ed,0x26},{0x370f,0x01},{0x3722,0x09},{0x3724,0x41},
  {0x3725,0xc1},{0x3771,0x09},{0x3772,0x09},{0x3773,0x05},
  {0x377a,0x48},{0x377b,0x5f},{0x37fa,0x11},{0x37fb,0x33},
  {0x37fc,0x11},{0x37fd,0x08},{0x3904,0x04},{0x3905,0x8c},
  {0x391d,0x04},{0x3921,0x20},{0x3926,0x21},{0x3933,0x80},
  {0x3934,0x0a},{0x3935,0x00},{0x3936,0x2a},{0x3937,0x6a},
  {0x3938,0x6a},{0x39dc,0x02},{0x3e01,0x54},{0x3e02,0x80},
  {0x3e09,0x00},{0x440e,0x02},{0x4509,0x20},{0x5ae0,0xfe},
  {0x5ae1,0x40},{0x5ae2,0x38},{0x5ae3,0x30},{0x5ae4,0x28},
  {0x5ae5,0x38},{0x5ae6,0x30},{0x5ae7,0x28},{0x5ae8,0x3f},
  {0x5ae9,0x34},{0x5aea,0x2c},{0x5aeb,0x3f},{0x5aec,0x34},
  {0x5aed,0x2c},{0x36e9,0x54},{0x37f9,0x47}
};

static inline void sc3336_putreg(uint32_t value, uintptr_t address)
{
  *(FAR volatile uint32_t *)address = value;
}

static ssize_t sc3336_read(FAR struct file *filep, FAR char *buffer,
                           size_t buflen)
{
  size_t length = strlen(g_sc3336_status);
  size_t offset = filep->f_pos;

  if (offset >= length)
    {
      return 0;
    }

  if (buflen > length - offset)
    {
      buflen = length - offset;
    }

  memcpy(buffer, g_sc3336_status + offset, buflen);
  filep->f_pos += buflen;
  return buflen;
}

static ssize_t sc3336_write(FAR struct file *filep, FAR const char *buffer,
                            size_t buflen)
{
  char command[40];
  size_t written = buflen;
  unsigned long value;
  int ret = -EINVAL;

  (void)filep;
  if (buflen == 0 || buflen >= sizeof(command))
    {
      return -EINVAL;
    }

  memcpy(command, buffer, buflen);
  command[buflen] = '\0';
  while (buflen > 0 && (command[buflen - 1] == '\n' ||
                        command[buflen - 1] == '\r'))
    {
      command[--buflen] = '\0';
    }

  if (strcmp(command, "capture") == 0)
    {
      nxmutex_lock(&g_sc3336_capture_lock);
      if (!g_sc3336_streaming)
        {
          ret = sc3336_sensor_stream(true);
          if (ret < 0)
            {
              nxmutex_unlock(&g_sc3336_capture_lock);
              return ret;
            }
        }

      ret = sc3336_capture_burst();
      nxmutex_unlock(&g_sc3336_capture_lock);
    }
  else if (strcmp(command, "ispcheck") == 0)
    {
      nxmutex_lock(&g_sc3336_capture_lock);
      if (!g_sc3336_streaming)
        {
          ret = sc3336_sensor_stream(true);
        }
      else
        {
          ret = 0;
        }

      if (ret >= 0)
        {
          ret = sc3336_isp_input_check();
        }

      nxmutex_unlock(&g_sc3336_capture_lock);
    }
  else if (strcmp(command, "micapture") == 0)
    {
      nxmutex_lock(&g_sc3336_capture_lock);
      if (!g_sc3336_streaming)
        {
          ret = sc3336_sensor_stream(true);
        }
      else
        {
          ret = 0;
        }

      if (ret >= 0)
        {
          ret = sc3336_isp_mi_capture();
        }

      nxmutex_unlock(&g_sc3336_capture_lock);
    }
  else if (strcmp(command, "start") == 0)
    {
      nxmutex_lock(&g_sc3336_lock);
      ret = sc3336_sensor_stream(true);
      nxmutex_unlock(&g_sc3336_lock);
    }
  else if (strcmp(command, "stop") == 0)
    {
      nxmutex_lock(&g_sc3336_lock);
      ret = sc3336_sensor_stream(false);
      nxmutex_unlock(&g_sc3336_lock);
    }
  else if (sscanf(command, "exposure=%lu", &value) == 1)
    {
      nxmutex_lock(&g_sc3336_lock);
      ret = sc3336_set_exposure(value);
      nxmutex_unlock(&g_sc3336_lock);
    }
  else if (sscanf(command, "gain=%lu", &value) == 1)
    {
      nxmutex_lock(&g_sc3336_lock);
      ret = sc3336_set_gain(value);
      nxmutex_unlock(&g_sc3336_lock);
    }
  else if (sscanf(command, "fps=%lu", &value) == 1)
    {
      nxmutex_lock(&g_sc3336_lock);
      ret = sc3336_set_fps(value);
      nxmutex_unlock(&g_sc3336_lock);
    }

  if (ret < 0)
    {
      return ret;
    }

  syslog(LOG_INFO,
         "CAMERA: control stream=%u exposure=%lu gain=%lu vts=%lu\n",
         g_sc3336_streaming, (unsigned long)g_sc3336_exposure,
         (unsigned long)g_sc3336_gain, (unsigned long)g_sc3336_vts);
  return written;
}

static const struct file_operations g_sc3336_fops =
{
  .read = sc3336_read,
  .write = sc3336_write,
};

static ssize_t sc3336_frame_read(FAR struct file *filep, FAR char *buffer,
                                 size_t buflen)
{
  size_t offset = filep->f_pos;

  if (g_sc3336_frame == NULL || offset >= g_sc3336_frame_size)
    {
      return 0;
    }

  if (buflen > g_sc3336_frame_size - offset)
    {
      buflen = g_sc3336_frame_size - offset;
    }

  memcpy(buffer, g_sc3336_frame + offset, buflen);
  filep->f_pos += buflen;
  return buflen;
}

static const struct file_operations g_sc3336_frame_fops =
{
  .read = sc3336_frame_read,
};

static ssize_t sc3336_isp_read(FAR struct file *filep, FAR char *buffer,
                               size_t buflen)
{
  size_t offset = filep->f_pos;

  if (g_sc3336_isp_frame == NULL || offset >= g_sc3336_isp_frame_size)
    {
      return 0;
    }

  if (buflen > g_sc3336_isp_frame_size - offset)
    {
      buflen = g_sc3336_isp_frame_size - offset;
    }

  memcpy(buffer, g_sc3336_isp_frame + offset, buflen);
  filep->f_pos += buflen;
  return buflen;
}

static const struct file_operations g_sc3336_isp_fops =
{
  .read = sc3336_isp_read,
};

static ssize_t sc3336_video_read(FAR struct file *filep, FAR char *buffer,
                                 size_t buflen)
{
  size_t offset = filep->f_pos;

  nxmutex_lock(&g_sc3336_lock);
  if (g_sc3336_video_frame == NULL || offset >= SC3336_FRAME_SIZE)
    {
      nxmutex_unlock(&g_sc3336_lock);
      return 0;
    }

  if (buflen > SC3336_FRAME_SIZE - offset)
    {
      buflen = SC3336_FRAME_SIZE - offset;
    }

  memcpy(buffer, g_sc3336_video_frame + offset, buflen);
  filep->f_pos += buflen;
  nxmutex_unlock(&g_sc3336_lock);
  return buflen;
}

static int sc3336_video_open(FAR struct file *filep)
{
  /* The frozen burst frame is immutable and shared by all readers.  Reset
   * the per-open position so dd/hexdump always start at the first pixel.
   */

  filep->f_pos = 0;
  return 0;
}

static int sc3336_video_close(FAR struct file *filep)
{
  (void)filep;
  return 0;
}

static int sc3336_video_querycap(FAR struct file *filep,
                                 FAR struct v4l2_capability *cap)
{
  (void)filep;
  memset(cap, 0, sizeof(*cap));
  strlcpy((FAR char *)cap->driver, "rv1103-rkcif", sizeof(cap->driver));
  strlcpy((FAR char *)cap->card, "Luckfox SC3336", sizeof(cap->card));
  strlcpy((FAR char *)cap->bus_info, "platform:ffa10000",
          sizeof(cap->bus_info));
  cap->version = 1;
  cap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_READWRITE |
                      V4L2_CAP_DEVICE_CAPS;
  cap->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_READWRITE;
  return 0;
}

static void sc3336_video_fill_format(FAR struct v4l2_format *fmt)
{
  memset(&fmt->fmt.pix, 0, sizeof(fmt->fmt.pix));
  fmt->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  fmt->fmt.pix.width = SC3336_WIDTH;
  fmt->fmt.pix.height = SC3336_HEIGHT;
  fmt->fmt.pix.pixelformat = V4L2_PIX_FMT_SBGGR10P;
  fmt->fmt.pix.field = V4L2_FIELD_NONE;
  fmt->fmt.pix.bytesperline = SC3336_RAW10_STRIDE;
  fmt->fmt.pix.sizeimage = SC3336_FRAME_SIZE;
}

static int sc3336_video_g_fmt(FAR struct file *filep,
                              FAR struct v4l2_format *fmt)
{
  (void)filep;
  if (fmt->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
    {
      return -EINVAL;
    }

  sc3336_video_fill_format(fmt);
  return 0;
}

static int sc3336_video_enum_fmt(FAR struct file *filep,
                                 FAR struct v4l2_fmtdesc *fmt)
{
  (void)filep;
  if (fmt->index != 0 || fmt->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
    {
      return -EINVAL;
    }

  fmt->pixelformat = V4L2_PIX_FMT_SBGGR10P;
  strlcpy((FAR char *)fmt->description, "SC3336 packed RAW10",
          sizeof(fmt->description));
  return 0;
}

static int sc3336_video_g_parm(FAR struct file *filep,
                               FAR struct v4l2_streamparm *parm)
{
  (void)filep;
  if (parm->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
    {
      return -EINVAL;
    }

  memset(&parm->parm.capture, 0, sizeof(parm->parm.capture));
  parm->parm.capture.timeperframe.numerator = 1;
  parm->parm.capture.timeperframe.denominator =
    SC3336_VTS_DEFAULT * 30u / g_sc3336_vts;
  parm->parm.capture.readbuffers = 1;
  return 0;
}

static int sc3336_video_s_parm(FAR struct file *filep,
                               FAR struct v4l2_streamparm *parm)
{
  uint32_t fps;
  int ret;

  (void)filep;
  if (parm->type != V4L2_BUF_TYPE_VIDEO_CAPTURE ||
      parm->parm.capture.timeperframe.numerator == 0)
    {
      return -EINVAL;
    }

  fps = parm->parm.capture.timeperframe.denominator /
        parm->parm.capture.timeperframe.numerator;
  nxmutex_lock(&g_sc3336_lock);
  ret = sc3336_set_fps(fps);
  nxmutex_unlock(&g_sc3336_lock);
  if (ret >= 0)
    {
      sc3336_video_g_parm(filep, parm);
    }

  return ret;
}

static int sc3336_video_g_ctrl(FAR struct file *filep,
                               FAR struct v4l2_control *ctrl)
{
  (void)filep;
  if (ctrl->id == V4L2_CID_EXPOSURE)
    {
      ctrl->value = g_sc3336_exposure;
    }
  else if (ctrl->id == V4L2_CID_GAIN)
    {
      ctrl->value = g_sc3336_gain;
    }
  else
    {
      return -EINVAL;
    }

  return 0;
}

static int sc3336_video_s_ctrl(FAR struct file *filep,
                               FAR struct v4l2_control *ctrl)
{
  int ret;

  (void)filep;
  nxmutex_lock(&g_sc3336_lock);
  if (ctrl->id == V4L2_CID_EXPOSURE)
    {
      ret = sc3336_set_exposure(ctrl->value);
    }
  else if (ctrl->id == V4L2_CID_GAIN)
    {
      ret = sc3336_set_gain(ctrl->value);
    }
  else
    {
      ret = -EINVAL;
    }

  nxmutex_unlock(&g_sc3336_lock);
  return ret;
}

static int sc3336_video_streamon(FAR struct file *filep,
                                 FAR enum v4l2_buf_type *type)
{
  int ret;

  (void)filep;
  if (*type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
    {
      return -EINVAL;
    }

  nxmutex_lock(&g_sc3336_lock);
  ret = g_sc3336_streaming ? 0 : sc3336_sensor_stream(true);
  nxmutex_unlock(&g_sc3336_lock);
  if (ret < 0)
    {
      return ret;
    }

  nxmutex_lock(&g_sc3336_capture_lock);
  ret = sc3336_capture_burst();
  nxmutex_unlock(&g_sc3336_capture_lock);
  return ret;
}

static int sc3336_video_streamoff(FAR struct file *filep,
                                  FAR enum v4l2_buf_type *type)
{
  int ret;

  (void)filep;
  if (*type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
    {
      return -EINVAL;
    }

  nxmutex_lock(&g_sc3336_lock);
  ret = sc3336_sensor_stream(false);
  nxmutex_unlock(&g_sc3336_lock);
  return ret;
}

static const struct file_operations g_sc3336_video_fops =
{
  .open = sc3336_video_open,
  .close = sc3336_video_close,
  .read = sc3336_video_read,
};

static const struct v4l2_ops_s g_sc3336_video_ops =
{
  .querycap = sc3336_video_querycap,
  .g_fmt = sc3336_video_g_fmt,
  .try_fmt = sc3336_video_g_fmt,
  .g_parm = sc3336_video_g_parm,
  .s_parm = sc3336_video_s_parm,
  .streamon = sc3336_video_streamon,
  .streamoff = sc3336_video_streamoff,
  .g_ctrl = sc3336_video_g_ctrl,
  .s_ctrl = sc3336_video_s_ctrl,
  .enum_fmt = sc3336_video_enum_fmt,
};

static struct v4l2_s g_sc3336_video =
{
  .vops = &g_sc3336_video_ops,
  .fops = &g_sc3336_video_fops,
};

static int sc3336_isp_video_querycap(FAR struct file *filep,
                                     FAR struct v4l2_capability *cap)
{
  (void)filep;
  memset(cap, 0, sizeof(*cap));
  strlcpy((FAR char *)cap->driver, "rv1103-rkisp32",
          sizeof(cap->driver));
  strlcpy((FAR char *)cap->card, "Luckfox SC3336 NV12",
          sizeof(cap->card));
  strlcpy((FAR char *)cap->bus_info, "platform:ffa00000",
          sizeof(cap->bus_info));
  cap->version = 1;
  cap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_READWRITE |
                      V4L2_CAP_STREAMING | V4L2_CAP_DEVICE_CAPS;
  cap->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_READWRITE |
                     V4L2_CAP_STREAMING;
  return 0;
}

static void sc3336_isp_video_fill_format(FAR struct v4l2_format *fmt)
{
  memset(&fmt->fmt.pix, 0, sizeof(fmt->fmt.pix));
  fmt->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  fmt->fmt.pix.width = SC3336_WIDTH;
  fmt->fmt.pix.height = SC3336_HEIGHT;
  fmt->fmt.pix.pixelformat = V4L2_PIX_FMT_NV12;
  fmt->fmt.pix.field = V4L2_FIELD_NONE;
  fmt->fmt.pix.bytesperline = SC3336_WIDTH;
  fmt->fmt.pix.sizeimage = SC3336_ISP_NV12_SIZE;
}

static int sc3336_isp_video_g_fmt(FAR struct file *filep,
                                  FAR struct v4l2_format *fmt)
{
  (void)filep;
  if (fmt->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
    {
      return -EINVAL;
    }

  sc3336_isp_video_fill_format(fmt);
  return 0;
}

static int sc3336_isp_video_enum_fmt(FAR struct file *filep,
                                     FAR struct v4l2_fmtdesc *fmt)
{
  (void)filep;
  if (fmt->index != 0 || fmt->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
    {
      return -EINVAL;
    }

  fmt->pixelformat = V4L2_PIX_FMT_NV12;
  strlcpy((FAR char *)fmt->description, "RKISP32 NV12",
          sizeof(fmt->description));
  return 0;
}

static int sc3336_isp_video_streamon(FAR struct file *filep,
                                     FAR enum v4l2_buf_type *type)
{
  int ret;

  (void)filep;
  if (*type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
    {
      return -EINVAL;
    }

  nxmutex_lock(&g_sc3336_capture_lock);
  ret = sc3336_isp_mi_capture();
  nxmutex_unlock(&g_sc3336_capture_lock);
  return ret;
}

static int sc3336_isp_video_streamoff(FAR struct file *filep,
                                      FAR enum v4l2_buf_type *type)
{
  (void)filep;
  return *type == V4L2_BUF_TYPE_VIDEO_CAPTURE ? 0 : -EINVAL;
}

static const struct file_operations g_sc3336_isp_video_fops =
{
  .open = sc3336_video_open,
  .close = sc3336_video_close,
  .read = sc3336_isp_read,
};

static const struct v4l2_ops_s g_sc3336_isp_video_ops =
{
  .querycap = sc3336_isp_video_querycap,
  .g_fmt = sc3336_isp_video_g_fmt,
  .try_fmt = sc3336_isp_video_g_fmt,
  .g_parm = sc3336_video_g_parm,
  .s_parm = sc3336_video_s_parm,
  .streamon = sc3336_isp_video_streamon,
  .streamoff = sc3336_isp_video_streamoff,
  .g_ctrl = sc3336_video_g_ctrl,
  .s_ctrl = sc3336_video_s_ctrl,
  .enum_fmt = sc3336_isp_video_enum_fmt,
};

static struct v4l2_s g_sc3336_isp_video =
{
  .vops = &g_sc3336_isp_video_ops,
  .fops = &g_sc3336_isp_video_fops,
};

static uint32_t sc3336_frame_hash(FAR const uint8_t *frame)
{
  uint32_t hash = 2166136261u;
  size_t i;

  for (i = 0; i < SC3336_FRAME_SIZE; i++)
    {
      hash = (hash ^ frame[i]) * 16777619u;
    }

  return hash;
}

static int sc3336_capture_burst(void)
{
  FAR uint8_t *frame0;
  FAR uint8_t *frame1;
  FAR uint8_t *oldframe = NULL;
  uintptr_t cif = RV1103_RKCIF_BASE;
  clock_t start;
  clock_t elapsed;
  uint32_t frames = 0;
  uint32_t errors = 0;
  uint32_t status;
  uint32_t hash0;
  uint32_t hash1;
  uint32_t ctrl0;
  int last = -1;

  frame0 = kmm_memalign(64, SC3336_FRAME_SIZE);
  frame1 = kmm_memalign(64, SC3336_FRAME_SIZE);
  if (frame0 == NULL || frame1 == NULL)
    {
      kmm_free(frame0);
      kmm_free(frame1);
      return -ENOMEM;
    }

  memset(frame0, 0xa5, SC3336_FRAME_SIZE);
  memset(frame1, 0x5a, SC3336_FRAME_SIZE);
  up_clean_dcache((uintptr_t)frame0,
                  (uintptr_t)frame0 + SC3336_FRAME_SIZE);
  up_clean_dcache((uintptr_t)frame1,
                  (uintptr_t)frame1 + SC3336_FRAME_SIZE);

  sc3336_putreg(0, cif + 0x100);
  sc3336_putreg(0x1fffffff, cif + 0x178);
  sc3336_putreg((uint32_t)(uintptr_t)frame0, cif + 0x124);
  sc3336_putreg((uint32_t)(uintptr_t)frame1, cif + 0x128);
  sc3336_putreg((3u << 8) | (0x1fu << 16), cif + 0x174);
  ctrl0 = (1u << 28) | (0x2bu << 10) | (1u << 4) |
          (1u << 1) | 1u;
  sc3336_putreg(ctrl0, cif + 0x100);

  start = clock_systime_ticks();
  while (frames < 60)
    {
      status = *(FAR volatile uint32_t *)(cif + 0x178);
      if ((status & (0x1fu << 16)) != 0)
        {
          errors |= status & (0x1fu << 16);
          break;
        }

      if ((status & (1u << 8)) != 0)
        {
          frames++;
          last = 0;
        }

      if ((status & (1u << 9)) != 0)
        {
          frames++;
          last = 1;
        }

      if ((status & 0x00000303u) != 0)
        {
          sc3336_putreg(status & 0x00000303u, cif + 0x178);
        }

      elapsed = clock_systime_ticks() - start;
      if (elapsed > MSEC2TICK(4000))
        {
          break;
        }

      nxsig_usleep(1000);
    }

  sc3336_putreg(0, cif + 0x100);
  sc3336_putreg(0, cif + 0x174);
  elapsed = clock_systime_ticks() - start;
  up_invalidate_dcache((uintptr_t)frame0,
                       (uintptr_t)frame0 + SC3336_FRAME_SIZE);
  up_invalidate_dcache((uintptr_t)frame1,
                       (uintptr_t)frame1 + SC3336_FRAME_SIZE);
  hash0 = sc3336_frame_hash(frame0);
  hash1 = sc3336_frame_hash(frame1);

  if (frames < 30 || errors != 0 || last < 0 || hash0 == hash1)
    {
      syslog(LOG_ERR,
             "CAMERA: RKCIF burst failed frames=%lu err=%08lx "
             "hash=%08lx/%08lx\n", (unsigned long)frames,
             (unsigned long)errors, (unsigned long)hash0,
             (unsigned long)hash1);
      kmm_free(frame0);
      kmm_free(frame1);
      return -EIO;
    }

  if (last == 0)
    {
      nxmutex_lock(&g_sc3336_lock);
      oldframe = g_sc3336_video_frame;
      g_sc3336_video_frame = frame0;
      g_sc3336_video_hash = hash0;
      nxmutex_unlock(&g_sc3336_lock);
      kmm_free(frame1);
    }
  else
    {
      nxmutex_lock(&g_sc3336_lock);
      oldframe = g_sc3336_video_frame;
      g_sc3336_video_frame = frame1;
      g_sc3336_video_hash = hash1;
      nxmutex_unlock(&g_sc3336_lock);
      kmm_free(frame0);
    }

  kmm_free(oldframe);

  g_sc3336_video_frames = frames;
  g_sc3336_video_msec = TICK2MSEC(elapsed);
  syslog(LOG_INFO,
         "CAMERA: RKCIF burst passed frames=%lu time=%lu ms fps=%lu.%01lu "
         "hash=%08lx/%08lx\n", (unsigned long)frames,
         (unsigned long)g_sc3336_video_msec,
         (unsigned long)(frames * 1000u / g_sc3336_video_msec),
         (unsigned long)((frames * 10000u / g_sc3336_video_msec) % 10u),
         (unsigned long)hash0, (unsigned long)hash1);
  return 0;
}

static void sc3336_isp_checkpoint(void)
{
  uintptr_t isp = 0xffa00000u;
  uintptr_t vicru = RV1103_VICRU_BASE;
  uint32_t ctrl0;
  uint32_t ctrl1;

  /* Read-only ISP v3.2 presence checkpoint.  Enabling H/A/core clocks is
   * harmless; do not start ISP processing without the official SC3336 IQ
   * parameter set and validated MI buffer programming.
   */

  sc3336_putreg(((7u << 7) << 16), vicru + 0x800);
  ctrl0 = *(FAR volatile uint32_t *)(isp + 0x0000);
  ctrl1 = *(FAR volatile uint32_t *)(isp + 0x0004);
  syslog(LOG_INFO,
         "CAMERA: RKISP32 checkpoint ctrl=%08lx/%08lx; IQ pipeline gated\n",
         (unsigned long)ctrl0, (unsigned long)ctrl1);
}

static int sc3336_isp_input_check(void)
{
  uintptr_t cif = RV1103_RKCIF_BASE;
  uintptr_t isp = 0xffa00000u;
  uintptr_t vicru = RV1103_VICRU_BASE;
  uint32_t saved_vi_en;
  uint32_t saved_toisp_ctrl;
  uint32_t saved_toisp_size;
  uint32_t saved_toisp_crop;
  uint32_t saved_ctrl;
  uint32_t saved_prop;
  uint32_t saved_acq_h_offs;
  uint32_t saved_acq_v_offs;
  uint32_t saved_acq_h_size;
  uint32_t saved_acq_v_size;
  uint32_t saved_out_h_offs;
  uint32_t saved_out_v_offs;
  uint32_t saved_out_h_size;
  uint32_t saved_out_v_size;
  uint32_t before;
  uint32_t after;
  clock_t start;

  /* This is deliberately an input-only checkpoint.  It proves that live
   * RAW10 frames reach ISP3.2, but leaves MI and the IQ-dependent Bayer to
   * YUV pipeline disabled.  Every modified route/ISP register is restored
   * before returning, so the already verified RKCIF RAW path remains usable.
   */

  saved_vi_en = *(FAR volatile uint32_t *)(isp + 0x0000);
  saved_toisp_ctrl = *(FAR volatile uint32_t *)(cif + 0x780);
  saved_toisp_size = *(FAR volatile uint32_t *)(cif + 0x784);
  saved_toisp_crop = *(FAR volatile uint32_t *)(cif + 0x788);
  saved_ctrl = *(FAR volatile uint32_t *)(isp + 0x400);
  saved_prop = *(FAR volatile uint32_t *)(isp + 0x404);
  saved_acq_h_offs = *(FAR volatile uint32_t *)(isp + 0x408);
  saved_acq_v_offs = *(FAR volatile uint32_t *)(isp + 0x40c);
  saved_acq_h_size = *(FAR volatile uint32_t *)(isp + 0x410);
  saved_acq_v_size = *(FAR volatile uint32_t *)(isp + 0x414);
  saved_out_h_offs = *(FAR volatile uint32_t *)(isp + 0x594);
  saved_out_v_offs = *(FAR volatile uint32_t *)(isp + 0x598);
  saved_out_h_size = *(FAR volatile uint32_t *)(isp + 0x59c);
  saved_out_v_size = *(FAR volatile uint32_t *)(isp + 0x5a0);

  /* Enable ISP HCLK/ACLK/core gates and the VICAP-to-ISP input clock. */

  sc3336_putreg(((7u << 7) << 16), vicru + 0x800);
  sc3336_putreg((1u << (2 + 16)), vicru + 0x804);

  sc3336_putreg(1, cif + 0x780); /* CSI host 0, channel 0, enable */
  sc3336_putreg(SC3336_WIDTH | (SC3336_HEIGHT << 16), cif + 0x784);
  sc3336_putreg(0, cif + 0x788);

  sc3336_putreg(1, isp + 0x0000);
  sc3336_putreg(0, isp + 0x400);
  sc3336_putreg((3u << 3) | (1u << 12), isp + 0x404);
  sc3336_putreg(0, isp + 0x408);
  sc3336_putreg(0, isp + 0x40c);
  sc3336_putreg(SC3336_WIDTH, isp + 0x410);
  sc3336_putreg(SC3336_HEIGHT, isp + 0x414);
  sc3336_putreg(0, isp + 0x594);
  sc3336_putreg(0, isp + 0x598);
  sc3336_putreg(SC3336_WIDTH, isp + 0x59c);
  sc3336_putreg(SC3336_HEIGHT, isp + 0x5a0);
  sc3336_putreg(0xffffffffu, isp + 0x5c8);
  sc3336_putreg(0xffffffffu, isp + 0x640);

  before = *(FAR volatile uint32_t *)(isp + 0x644);
  sc3336_putreg((1u << 9) | (1u << 8) | (1u << 4) | 1u,
                isp + 0x400);

  start = clock_systime_ticks();
  do
    {
      after = *(FAR volatile uint32_t *)(isp + 0x644);
      if (after != before)
        {
          break;
        }

      nxsig_usleep(1000);
    }
  while (clock_systime_ticks() - start < MSEC2TICK(1500));

  g_sc3336_isp_frames = after != before ? 1 : 0;
  g_sc3336_isp_ris = *(FAR volatile uint32_t *)(isp + 0x5c0);
  g_sc3336_isp_err = *(FAR volatile uint32_t *)(isp + 0x63c);

  sc3336_putreg(0, isp + 0x400);
  sc3336_putreg(0, cif + 0x780);

  sc3336_putreg(saved_prop, isp + 0x404);
  sc3336_putreg(saved_acq_h_offs, isp + 0x408);
  sc3336_putreg(saved_acq_v_offs, isp + 0x40c);
  sc3336_putreg(saved_acq_h_size, isp + 0x410);
  sc3336_putreg(saved_acq_v_size, isp + 0x414);
  sc3336_putreg(saved_out_h_offs, isp + 0x594);
  sc3336_putreg(saved_out_v_offs, isp + 0x598);
  sc3336_putreg(saved_out_h_size, isp + 0x59c);
  sc3336_putreg(saved_out_v_size, isp + 0x5a0);
  sc3336_putreg(saved_ctrl, isp + 0x400);
  sc3336_putreg(saved_vi_en, isp + 0x0000);
  sc3336_putreg(saved_toisp_size, cif + 0x784);
  sc3336_putreg(saved_toisp_crop, cif + 0x788);
  sc3336_putreg(saved_toisp_ctrl, cif + 0x780);

  syslog(g_sc3336_isp_frames != 0 && g_sc3336_isp_err == 0 ?
         LOG_INFO : LOG_ERR,
         "CAMERA: RKISP32 input check frames=%lu ris=%08lx err=%08lx; "
         "route restored\n", (unsigned long)g_sc3336_isp_frames,
         (unsigned long)g_sc3336_isp_ris,
         (unsigned long)g_sc3336_isp_err);

  return g_sc3336_isp_frames != 0 && g_sc3336_isp_err == 0 ? 0 : -EIO;
}

static int sc3336_isp_mi_capture(void)
{
  FAR uint8_t *frame;
  FAR uint8_t *raw0;
  FAR uint8_t *raw1;
  FAR uint8_t *oldframe = NULL;
  uintptr_t cif = RV1103_RKCIF_BASE;
  uintptr_t isp = 0xffa00000u;
  uintptr_t vicru = RV1103_VICRU_BASE;
  uint32_t saved[49];
  uint32_t saved_toisp_ctrl;
  uint32_t saved_toisp_size;
  uint32_t saved_toisp_crop;
  uint32_t saved_cif_id0_ctrl0;
  uint32_t saved_cif_id0_ctrl1;
  uint32_t saved_cif_mipi_ctrl;
  uint32_t saved_cif_frm0;
  uint32_t saved_cif_frm1;
  uint32_t saved_cif_frm0_uv;
  uint32_t saved_cif_frm1_uv;
  uint32_t saved_cif_vir_line;
  uint32_t saved_cif_int_en;
  uint32_t cif_status = 0;
  uint32_t ris = 0;
  uint32_t status = 0;
  uint32_t isp_err = 0;
  uint32_t mi_ctrl_shd = 0;
  uint32_t y_base_shd = 0;
  uint32_t isp_debug2 = 0;
  uint32_t mi_bytes = 0;
  uint32_t mi_pixels = 0;
  uint32_t crop_shd = 0;
  uint32_t rsz_shd = 0;
  uint32_t frames_before;
  uint32_t frames_after;
  uint32_t hash;
  uint32_t changed = 0;
  bool guard_ok = true;
  bool was_streaming;
  clock_t start;
  clock_t elapsed;
  size_t i;
  uint32_t mi_frames = 0;
  int sensor_ret = 0;
  int ret = -EIO;

  /* The official Rockchip RT-Thread RV1106 ISP3 driver uses this fixed
   * sequence as its initial NV12 main-path configuration.  Keep it a
   * bounded, one-frame diagnostic until image quality parameters are
   * translated from rkaiq.  The extra chroma-sized area and 4-KiB guard
   * absorb either planar or semiplanar writes without risking heap data.
   */

  frame = kmm_memalign(64, SC3336_ISP_ALLOC_SIZE);
  raw0 = kmm_memalign(64, SC3336_FRAME_SIZE);
  raw1 = kmm_memalign(64, SC3336_FRAME_SIZE);
  if (frame == NULL || raw0 == NULL || raw1 == NULL)
    {
      kmm_free(frame);
      kmm_free(raw0);
      kmm_free(raw1);
      return -ENOMEM;
    }

  memset(frame, 0xa5, SC3336_ISP_ALLOC_SIZE);
  up_clean_dcache((uintptr_t)frame,
                  (uintptr_t)frame + SC3336_ISP_ALLOC_SIZE);
  memset(raw0, 0xa5, SC3336_FRAME_SIZE);
  memset(raw1, 0x5a, SC3336_FRAME_SIZE);
  up_clean_dcache((uintptr_t)raw0,
                  (uintptr_t)raw0 + SC3336_FRAME_SIZE);
  up_clean_dcache((uintptr_t)raw1,
                  (uintptr_t)raw1 + SC3336_FRAME_SIZE);

  /* The reference driver completes ISP, MI, VICAP and CSI setup before it
   * starts the sensor.  Attaching this path to an already active frame only
   * advances the ISP input counter once, while DEBUG2 remains at line zero.
   * Stop the sensor here and generate a clean V_START after every shadow
   * register below has been committed.  Restore the original stream state
   * before returning so /dev/camera0 and /dev/video0 remain usable.
   */

  was_streaming = g_sc3336_streaming;
  if (was_streaming)
    {
      sensor_ret = sc3336_sensor_stream(false);
      if (sensor_ret < 0)
        {
          kmm_free(frame);
          kmm_free(raw0);
          kmm_free(raw1);
          return sensor_ret;
        }
    }

#define ISP_SAVE(n, off) saved[n] = *(FAR volatile uint32_t *)(isp + (off))
  ISP_SAVE(0, 0x0000); ISP_SAVE(1, 0x0018); ISP_SAVE(2, 0x001c);
  ISP_SAVE(32, 0x0004); ISP_SAVE(33, 0x0010);
  ISP_SAVE(3, 0x0400); ISP_SAVE(4, 0x0404);
  ISP_SAVE(5, 0x0408); ISP_SAVE(6, 0x040c);
  ISP_SAVE(7, 0x0410); ISP_SAVE(8, 0x0414);
  ISP_SAVE(9, 0x0594); ISP_SAVE(10, 0x0598);
  ISP_SAVE(11, 0x059c); ISP_SAVE(12, 0x05a0);
  ISP_SAVE(13, 0x0880); ISP_SAVE(14, 0x0c00);
  ISP_SAVE(15, 0x1400); ISP_SAVE(16, 0x1404);
  ISP_SAVE(17, 0x1408); ISP_SAVE(18, 0x140c);
  ISP_SAVE(19, 0x141c); ISP_SAVE(20, 0x1420);
  ISP_SAVE(21, 0x142c); ISP_SAVE(22, 0x1430);
  ISP_SAVE(23, 0x14f8); ISP_SAVE(24, 0x1548);
  ISP_SAVE(25, 0x15e8); ISP_SAVE(26, 0x15ec);
  ISP_SAVE(27, 0x15f0); ISP_SAVE(28, 0x15f4);
  ISP_SAVE(29, 0x15f8); ISP_SAVE(30, 0x2500);
  ISP_SAVE(31, 0x18c0);
  ISP_SAVE(34, 0x1814);
  ISP_SAVE(35, 0x1410); ISP_SAVE(36, 0x1414);
  ISP_SAVE(37, 0x1424); ISP_SAVE(38, 0x1428);
  ISP_SAVE(39, 0x1434); ISP_SAVE(40, 0x1438);
  ISP_SAVE(41, 0x1800);
  ISP_SAVE(42, 0x0538); ISP_SAVE(43, 0x053c);
  ISP_SAVE(44, 0x0540); ISP_SAVE(45, 0x0544);
  ISP_SAVE(46, 0x0548); ISP_SAVE(47, 0x054c);
  ISP_SAVE(48, 0x3000);
#undef ISP_SAVE

  saved_toisp_ctrl = *(FAR volatile uint32_t *)(cif + 0x780);
  saved_toisp_size = *(FAR volatile uint32_t *)(cif + 0x784);
  saved_toisp_crop = *(FAR volatile uint32_t *)(cif + 0x788);
  saved_cif_id0_ctrl0 = *(FAR volatile uint32_t *)(cif + 0x100);
  saved_cif_id0_ctrl1 = *(FAR volatile uint32_t *)(cif + 0x104);
  saved_cif_mipi_ctrl = *(FAR volatile uint32_t *)(cif + 0x120);
  saved_cif_frm0 = *(FAR volatile uint32_t *)(cif + 0x124);
  saved_cif_frm1 = *(FAR volatile uint32_t *)(cif + 0x128);
  saved_cif_frm0_uv = *(FAR volatile uint32_t *)(cif + 0x12c);
  saved_cif_frm1_uv = *(FAR volatile uint32_t *)(cif + 0x130);
  saved_cif_vir_line = *(FAR volatile uint32_t *)(cif + 0x134);
  saved_cif_int_en = *(FAR volatile uint32_t *)(cif + 0x174);

  /* Enable HCLK/ACLK/core plus the VICAP-to-ISP input clock. */

  sc3336_putreg(((7u << 7) << 16), vicru + 0x800);
  sc3336_putreg((1u << (2 + 16)), vicru + 0x804);

  /* A live TOISP route requires the VICAP MIPI0 ID0 channel as well as the
   * TOISP selector.  The normal RAW capture deliberately disables ID0 after
   * each burst, which left only one residual frame for the earlier ISP
   * probes.  Give this exclusive ISP transaction its own proven RAW10
   * ping-pong buffers so enabling ID0 cannot overwrite any retained
   * /dev/camera0 or /dev/video0 frame.
   */

  sc3336_putreg(0, cif + 0x100);
  sc3336_putreg(0x1fffffff, cif + 0x178);
  sc3336_putreg(SC3336_WIDTH | (SC3336_HEIGHT << 16), cif + 0x104);
  sc3336_putreg((uint32_t)(uintptr_t)raw0, cif + 0x124);
  sc3336_putreg((uint32_t)(uintptr_t)raw1, cif + 0x128);
  sc3336_putreg(0, cif + 0x12c);
  sc3336_putreg(0, cif + 0x130);
  sc3336_putreg(SC3336_RAW10_STRIDE, cif + 0x134);
  sc3336_putreg((3u << 13) | (1u << 12) | (3u << 5) |
                (1u << 4) | (2u << 1) | 1u | (1u << 16), cif + 0x120);
  sc3336_putreg((3u << 8) | (0x1fu << 16), cif + 0x174);

  sc3336_putreg(1, cif + 0x780);
  sc3336_putreg(SC3336_WIDTH | (SC3336_HEIGHT << 16), cif + 0x784);
  sc3336_putreg(0, cif + 0x788);

  sc3336_putreg(1, isp + 0x0000);
  sc3336_putreg(0x00400000, isp + 0x0004);
  sc3336_putreg(0x0001797b, isp + 0x0010);
  sc3336_putreg(0x8, isp + 0x001c);
  sc3336_putreg(0x5, isp + 0x0018);
  sc3336_putreg(0, isp + 0x0400);
  sc3336_putreg((3u << 3) | (1u << 12), isp + 0x0404);
  sc3336_putreg(0, isp + 0x0408);
  sc3336_putreg(0, isp + 0x040c);
  sc3336_putreg(SC3336_WIDTH, isp + 0x0410);
  sc3336_putreg(SC3336_HEIGHT, isp + 0x0414);
  sc3336_putreg(0, isp + 0x0594);
  sc3336_putreg(0, isp + 0x0598);
  sc3336_putreg(SC3336_WIDTH, isp + 0x059c);
  sc3336_putreg(SC3336_HEIGHT, isp + 0x05a0);
  sc3336_putreg(0x20, isp + 0x0880);
  sc3336_putreg(0, isp + 0x0c00);
  sc3336_putreg(0x100, isp + 0x0c00);

  sc3336_putreg((uint32_t)(uintptr_t)frame, isp + 0x1408);
  sc3336_putreg(SC3336_ISP_Y_SIZE, isp + 0x140c);
  sc3336_putreg(0, isp + 0x1410);
  sc3336_putreg(0, isp + 0x1414);
  sc3336_putreg((uint32_t)(uintptr_t)(frame + SC3336_ISP_Y_SIZE),
                isp + 0x141c);
  sc3336_putreg(SC3336_ISP_Y_SIZE / 2u, isp + 0x1420);
  sc3336_putreg(0, isp + 0x1424);
  sc3336_putreg(0, isp + 0x1428);
  sc3336_putreg((uint32_t)(uintptr_t)(frame + SC3336_ISP_NV12_SIZE),
                isp + 0x142c);
  sc3336_putreg(SC3336_ISP_Y_SIZE / 2u, isp + 0x1430);
  sc3336_putreg(0, isp + 0x1434);
  sc3336_putreg(0, isp + 0x1438);
  sc3336_putreg(SC3336_WIDTH, isp + 0x15e8);
  sc3336_putreg(SC3336_WIDTH, isp + 0x15ec);
  sc3336_putreg(SC3336_HEIGHT, isp + 0x15f0);
  sc3336_putreg(SC3336_ISP_Y_SIZE, isp + 0x15f4);
  sc3336_putreg(0, isp + 0x1548);
  /* ISP32 NV12 main path: separate Y/UV, YUV420 output. */

  /* Keep the exact values used by Rockchip's RV1106 RT-Thread fast path.
   * 0x502 selects its separate-YUV main-path mode; the high bits of
   * MI_WR_CTRL are retained even though only MP is enabled.
   */

  sc3336_putreg(0x502, isp + 0x18c0);
  sc3336_putreg(0x37, isp + 0x1814);
  sc3336_putreg(0x100, isp + 0x15f8);
  sc3336_putreg(0x217a2000, isp + 0x1400);
  sc3336_putreg(0x205, isp + 0x0018);

  /* ISP_CTRL0 enables AWB gain (bit 7).  The Rockchip RT-Thread reference
   * calls rk_isp_module_init() before writing 0x6397; that routine installs
   * non-zero gains for all three HDR banks.  Leaving their reset value of
   * zero suppresses the Bayer pipeline before the main path.  Use unity
   * Q8.8 gains for this no-IQ checkpoint and enable zero-offset BLS, exactly
   * matching the reference driver's minimum module setup.
   */

  sc3336_putreg(0x01000100, isp + 0x0538);
  sc3336_putreg(0x01000100, isp + 0x053c);
  sc3336_putreg(0x01000100, isp + 0x0540);
  sc3336_putreg(0x01000100, isp + 0x0544);
  sc3336_putreg(0x01000100, isp + 0x0548);
  sc3336_putreg(0x01000100, isp + 0x054c);
  sc3336_putreg(1, isp + 0x3000);
  sc3336_putreg(0xffffffffu, isp + 0x1504);
  sc3336_putreg(1, isp + 0x14f8);
  sc3336_putreg(0xffffffffu, isp + 0x05c8);
  sc3336_putreg(0xffffffffu, isp + 0x0640);

  /* Match the RV1106 start order: commit base/offset registers, request the
   * MP self-update, enable the MP writer, then force the ISP configuration
   * into its shadow registers.  Retain MPSELF_UPD because it is required for
   * MI_WR_CTRL_SHD to latch in this bounded, software-triggered path.
   */

  sc3336_putreg(0x6397, isp + 0x0400);
  sc3336_putreg(0x10, isp + 0x1404);
  sc3336_putreg(0x10, isp + 0x1800);
  sc3336_putreg(0x217a2001, isp + 0x1400);

  /* capture_v32 enables MP after its first self-update while ISP is still
   * stopped.  Our bounded diagnostic attaches to an already live input, so
   * the enable write misses that update boundary.  Commit once more after
   * MP_ENABLE: this is idempotent, but is required for MI_WR_CTRL_SHD to
   * become non-zero on RV1103/RV1106 in the live-route case.
   */

  sc3336_putreg(0x10, isp + 0x1800);
  sc3336_putreg(0x10, isp + 0x1404);
  frames_before = *(FAR volatile uint32_t *)(isp + 0x0644);

  /* rk_isp_hw_set_isp_top() is called after the literal 0x6397 write in the
   * official fast path.  For RAW10 BGGR to YUV it overwrites CTRL0 with
   * Bayer mode + INFORM + AWB + permanent/force update = 0x397.  Debayer is
   * enabled only after that final top-level commit.
   */

  sc3336_putreg(0x397, isp + 0x0400);
  sc3336_putreg(0x111, isp + 0x2500);
  sc3336_putreg((1u << 28) | (0x2bu << 10) | (1u << 4) |
                (1u << 1) | 1u, cif + 0x100);
  sensor_ret = sc3336_sensor_stream(true);

  start = clock_systime_ticks();
  while (sensor_ret >= 0)
    {
      ris = *(FAR volatile uint32_t *)(isp + 0x14fc);
      status = *(FAR volatile uint32_t *)(isp + 0x150c);
      isp_err = *(FAR volatile uint32_t *)(isp + 0x063c);
      if ((ris & (1u << 30)) != 0 || isp_err != 0)
        {
          break;
        }

      if ((ris & 1u) != 0)
        {
          mi_frames++;
          sc3336_putreg(ris, isp + 0x1504);
          if (mi_frames >= 60)
            {
              break;
            }
        }

      nxsig_usleep(1000);
      if (clock_systime_ticks() - start >= MSEC2TICK(4000))
        {
          break;
        }
    }

  elapsed = clock_systime_ticks() - start;

  frames_after = *(FAR volatile uint32_t *)(isp + 0x0644);
  mi_ctrl_shd = *(FAR volatile uint32_t *)(isp + 0x1474);
  y_base_shd = *(FAR volatile uint32_t *)(isp + 0x1478);
  isp_debug2 = *(FAR volatile uint32_t *)(isp + 0x064c);
  mi_bytes = *(FAR volatile uint32_t *)(isp + 0x1470);
  mi_pixels = *(FAR volatile uint32_t *)(isp + 0x152c);
  crop_shd = *(FAR volatile uint32_t *)(isp + 0x0884);
  rsz_shd = *(FAR volatile uint32_t *)(isp + 0x0c30);
  cif_status = *(FAR volatile uint32_t *)(cif + 0x178);

  sc3336_putreg(0, cif + 0x100);
  sc3336_putreg(0, cif + 0x174);
  if (g_sc3336_streaming)
    {
      sc3336_sensor_stream(false);
    }

  sc3336_putreg(0x217a2000, isp + 0x1400);
  sc3336_putreg(0, isp + 0x0400);
  sc3336_putreg(0, isp + 0x14f8);
  sc3336_putreg(0, cif + 0x780);

  up_invalidate_dcache((uintptr_t)frame,
                       (uintptr_t)frame + SC3336_ISP_ALLOC_SIZE);
  hash = 2166136261u;
  for (i = 0; i < SC3336_ISP_NV12_SIZE; i++)
    {
      hash = (hash ^ frame[i]) * 16777619u;
      if (frame[i] != 0xa5)
        {
          changed++;
        }
    }

  for (i = SC3336_ISP_Y_SIZE * 2u; i < SC3336_ISP_ALLOC_SIZE; i++)
    {
      if (frame[i] != 0xa5)
        {
          guard_ok = false;
          break;
        }
    }

  /* Stop first, then restore the complete register set used by the test. */

#define ISP_RESTORE(n, off) sc3336_putreg(saved[n], isp + (off))
  ISP_RESTORE(48, 0x3000);
  ISP_RESTORE(47, 0x054c); ISP_RESTORE(46, 0x0548);
  ISP_RESTORE(45, 0x0544); ISP_RESTORE(44, 0x0540);
  ISP_RESTORE(43, 0x053c); ISP_RESTORE(42, 0x0538);
  ISP_RESTORE(41, 0x1800);
  ISP_RESTORE(34, 0x1814); ISP_RESTORE(31, 0x18c0);
  ISP_RESTORE(30, 0x2500);
  ISP_RESTORE(29, 0x15f8);
  ISP_RESTORE(28, 0x15f4); ISP_RESTORE(27, 0x15f0);
  ISP_RESTORE(26, 0x15ec); ISP_RESTORE(25, 0x15e8);
  ISP_RESTORE(24, 0x1548); ISP_RESTORE(23, 0x14f8);
  ISP_RESTORE(22, 0x1430); ISP_RESTORE(21, 0x142c);
  ISP_RESTORE(20, 0x1420); ISP_RESTORE(19, 0x141c);
  ISP_RESTORE(40, 0x1438); ISP_RESTORE(39, 0x1434);
  ISP_RESTORE(38, 0x1428); ISP_RESTORE(37, 0x1424);
  ISP_RESTORE(36, 0x1414); ISP_RESTORE(35, 0x1410);
  ISP_RESTORE(18, 0x140c); ISP_RESTORE(17, 0x1408);
  ISP_RESTORE(16, 0x1404); ISP_RESTORE(15, 0x1400);
  ISP_RESTORE(14, 0x0c00); ISP_RESTORE(13, 0x0880);
  ISP_RESTORE(12, 0x05a0); ISP_RESTORE(11, 0x059c);
  ISP_RESTORE(10, 0x0598); ISP_RESTORE(9, 0x0594);
  ISP_RESTORE(8, 0x0414); ISP_RESTORE(7, 0x0410);
  ISP_RESTORE(6, 0x040c); ISP_RESTORE(5, 0x0408);
  ISP_RESTORE(4, 0x0404); ISP_RESTORE(3, 0x0400);
  ISP_RESTORE(2, 0x001c); ISP_RESTORE(1, 0x0018);
  ISP_RESTORE(33, 0x0010); ISP_RESTORE(32, 0x0004);
  ISP_RESTORE(0, 0x0000);
#undef ISP_RESTORE
  sc3336_putreg(saved_toisp_size, cif + 0x784);
  sc3336_putreg(saved_toisp_crop, cif + 0x788);
  sc3336_putreg(saved_toisp_ctrl, cif + 0x780);
  sc3336_putreg(saved_cif_mipi_ctrl, cif + 0x120);
  sc3336_putreg(saved_cif_frm0, cif + 0x124);
  sc3336_putreg(saved_cif_frm1, cif + 0x128);
  sc3336_putreg(saved_cif_frm0_uv, cif + 0x12c);
  sc3336_putreg(saved_cif_frm1_uv, cif + 0x130);
  sc3336_putreg(saved_cif_vir_line, cif + 0x134);
  sc3336_putreg(saved_cif_id0_ctrl1, cif + 0x104);
  sc3336_putreg(saved_cif_int_en, cif + 0x174);
  sc3336_putreg(saved_cif_id0_ctrl0, cif + 0x100);

  if (was_streaming)
    {
      sensor_ret = sc3336_sensor_stream(true);
    }

  if (sensor_ret >= 0 && mi_frames >= 30 &&
      (ris & (1u << 30)) == 0 && isp_err == 0 &&
      changed > SC3336_ISP_NV12_SIZE / 100u && guard_ok)
    {
      ret = register_driver("/dev/isp0", &g_sc3336_isp_fops, 0444, NULL);
      if (ret == -EEXIST)
        {
          ret = 0;
        }

      if (ret >= 0)
        {
          nxmutex_lock(&g_sc3336_lock);
          oldframe = g_sc3336_isp_frame;
          g_sc3336_isp_frame = frame;
          g_sc3336_isp_frame_size = SC3336_ISP_NV12_SIZE;
          g_sc3336_isp_hash = hash;
          nxmutex_unlock(&g_sc3336_lock);
          kmm_free(oldframe);
          g_sc3336_isp_burst_frames = mi_frames;
          g_sc3336_isp_burst_msec = TICK2MSEC(elapsed);
        }
    }

  if (ret < 0)
    {
      kmm_free(frame);
    }

  kmm_free(raw0);
  kmm_free(raw1);

  syslog(ret >= 0 ? LOG_INFO : LOG_ERR,
         "CAMERA: RKISP32 MI capture %s ris=%08lx status=%08lx "
         "err=%08lx changed=%lu guard=%s hash=%08lx shd=%08lx/%08lx "
         "frames=%lu outln=%lu bytes=%lu pixels=%lu crop=%08lx "
         "rsz=%08lx cif=%08lx mi=%lu msec=%lu; route restored\n",
         ret >= 0 ? "passed" : "failed", (unsigned long)ris,
         (unsigned long)status, (unsigned long)isp_err,
         (unsigned long)changed, guard_ok ? "ok" : "bad",
         (unsigned long)hash, (unsigned long)mi_ctrl_shd,
         (unsigned long)y_base_shd,
         (unsigned long)(frames_after != frames_before ? 1 : 0),
         (unsigned long)(isp_debug2 & 0x3fffu),
         (unsigned long)mi_bytes, (unsigned long)mi_pixels,
         (unsigned long)crop_shd, (unsigned long)rsz_shd,
         (unsigned long)cif_status, (unsigned long)mi_frames,
         (unsigned long)TICK2MSEC(elapsed));
  return ret;
}

static int sc3336_capture_first_frame(void)
{
  FAR uint8_t *frame0;
  FAR uint8_t *frame1;
  uintptr_t cif = RV1103_RKCIF_BASE;
  uintptr_t vicru = RV1103_VICRU_BASE;
  uint32_t status = 0;
  uint32_t hash = 2166136261u;
  uint32_t nonzero = 0;
  uint32_t ctrl0;
  unsigned int retry;
  size_t i;
  int ret;

  frame0 = kmm_memalign(64, SC3336_FRAME_SIZE);
  frame1 = kmm_memalign(64, SC3336_FRAME_SIZE);
  if (frame0 == NULL || frame1 == NULL)
    {
      kmm_free(frame0);
      kmm_free(frame1);
      return -ENOMEM;
    }

  memset(frame0, 0xa5, SC3336_FRAME_SIZE);
  memset(frame1, 0x5a, SC3336_FRAME_SIZE);
  up_clean_dcache((uintptr_t)frame0,
                  (uintptr_t)frame0 + SC3336_FRAME_SIZE);
  up_clean_dcache((uintptr_t)frame1,
                  (uintptr_t)frame1 + SC3336_FRAME_SIZE);

  /* Enable D/P/A/H/I0/I1 VICAP clocks plus RX0 PCLK.  Pulse only the
   * VICAP core/bus resets here: CSI host and D-PHY are already receiving
   * a valid stream and must not be disturbed.
   */

  sc3336_putreg((0xfc00u << 16), vicru + 0x800);
  sc3336_putreg((1u << 16), vicru + 0x804);
  sc3336_putreg((0xfc00u << 16) | 0xfc00u, vicru + 0xa00);
  nxsig_usleep(5);
  sc3336_putreg((0xfc00u << 16), vicru + 0xa00);

  /* RV1106 RKCIF MIPI0, VC0, CSI-2 DT=0x2b RAW10 compact.  Packed data is
   * 2880 bytes/line; the RV1106 DMA requires its 256-byte-aligned 3072
   * byte virtual line width.  Two addresses prevent the next frame from
   * overwriting frame0 while software observes the completion bit.
   */

  sc3336_putreg(0, cif + 0x100);                       /* ID0 disabled */
  sc3336_putreg(0x1fffffff, cif + 0x178);              /* clear status */
  sc3336_putreg(SC3336_WIDTH | (SC3336_HEIGHT << 16), cif + 0x104);
  sc3336_putreg((uint32_t)(uintptr_t)frame0, cif + 0x124);
  sc3336_putreg((uint32_t)(uintptr_t)frame1, cif + 0x128);
  sc3336_putreg(0, cif + 0x12c);
  sc3336_putreg(0, cif + 0x130);
  sc3336_putreg(SC3336_RAW10_STRIDE, cif + 0x134);
  sc3336_putreg((3u << 13) | (1u << 12) | (3u << 5) |
                (1u << 4) | (2u << 1) | 1u | (1u << 16), cif + 0x120);
  sc3336_putreg((3u << 8) | (0x1fu << 16), cif + 0x174);

  ctrl0 = (1u << 28) | (0x2bu << 10) | (1u << 4) |
          (1u << 1) | 1u;
  sc3336_putreg(ctrl0, cif + 0x100);

  for (retry = 0; retry < 1000; retry++)
    {
      status = *(FAR volatile uint32_t *)(cif + 0x178);
      if ((status & (1u << 8)) != 0)
        {
          break;
        }

      if ((status & (0x1fu << 16)) != 0)
        {
          break;
        }

      nxsig_usleep(1000);
    }

  sc3336_putreg(0, cif + 0x100);                       /* freeze frame0 */
  sc3336_putreg(0, cif + 0x174);

  if ((status & (1u << 8)) == 0)
    {
      syslog(LOG_ERR, "CAMERA: RKCIF first frame failed stat=%08lx\n",
             (unsigned long)status);
      kmm_free(frame0);
      kmm_free(frame1);
      return (status & (0x1fu << 16)) != 0 ? -EIO : -ETIMEDOUT;
    }

  up_invalidate_dcache((uintptr_t)frame0,
                       (uintptr_t)frame0 + SC3336_FRAME_SIZE);
  kmm_free(frame1);

  for (i = 0; i < SC3336_FRAME_SIZE; i++)
    {
      hash = (hash ^ frame0[i]) * 16777619u;
      if (frame0[i] != 0 && frame0[i] != 0xa5)
        {
          nonzero++;
        }
    }

  if (nonzero < SC3336_FRAME_SIZE / 100)
    {
      syslog(LOG_ERR,
             "CAMERA: RKCIF frame completed but payload unchanged (%lu)\n",
             (unsigned long)nonzero);
      kmm_free(frame0);
      return -EIO;
    }

  g_sc3336_frame = frame0;
  g_sc3336_frame_size = SC3336_FRAME_SIZE;
  ret = register_driver("/dev/camera0", &g_sc3336_frame_fops, 0444, NULL);
  if (ret < 0 && ret != -EEXIST)
    {
      g_sc3336_frame = NULL;
      g_sc3336_frame_size = 0;
      kmm_free(frame0);
      return ret;
    }

  syslog(LOG_INFO,
         "CAMERA: RKCIF first RAW10 frame ready, %lu bytes hash=%08lx "
         "stat=%08lx\n", (unsigned long)SC3336_FRAME_SIZE,
         (unsigned long)hash, (unsigned long)status);
  snprintf(g_sc3336_status + strlen(g_sc3336_status),
           sizeof(g_sc3336_status) - strlen(g_sc3336_status),
           "RKCIF frame=/dev/camera0 format=SBGGR10P stride=%u "
           "size=%lu hash=0x%08lx stat=0x%08lx\n",
           SC3336_RAW10_STRIDE, (unsigned long)SC3336_FRAME_SIZE,
           (unsigned long)hash, (unsigned long)status);
  return 0;
}

static void sc3336_enable_mclk(void)
{
  /* GPIO3_C4 function 2 is mipi_refclk_out0.  The upstream 2304x1296
   * 30-fps mode accepts 24 MHz, so select xin24m directly.  Clock gates use
   * zero=enabled and Rockchip upper-half write masks.
   */

  rv1103_pinctrl_config(3, SC3336_MCLK_PIN, 2,
                        RV1103_PULL_NONE, false);
  sc3336_putreg((RV1103_MIPI0_OUT_SEL_MASK << 16) |
                RV1103_MIPI0_OUT_SEL_XIN24M, RV1103_CLKSEL_CON(27));
  sc3336_putreg(RV1103_MIPI0_OUT_GATE << 16, RV1103_CLKGATE_CON(3));
}

static int sc3336_read_id(FAR struct i2c_master_s *i2c,
                          FAR uint16_t *chipid)
{
  struct i2c_msg_s msg[2];
  uint8_t address[2] = {SC3336_REG_CHIP_ID >> 8,
                        SC3336_REG_CHIP_ID & 0xff};
  uint8_t id[2] = {0, 0};
  int ret;

  /* Read both ID bytes in one SCCB transaction.  Separate transactions can
   * observe one stale/zero byte while the sensor is just leaving PWDN.
   */

  msg[0].frequency = SC3336_I2C_FREQUENCY;
  msg[0].addr = SC3336_I2C_ADDRESS;
  msg[0].flags = 0;
  msg[0].buffer = address;
  msg[0].length = sizeof(address);
  msg[1].frequency = SC3336_I2C_FREQUENCY;
  msg[1].addr = SC3336_I2C_ADDRESS;
  msg[1].flags = I2C_M_READ;
  msg[1].buffer = id;
  msg[1].length = sizeof(id);

  ret = I2C_TRANSFER(i2c, msg, 2);
  if (ret >= 0)
    {
      *chipid = ((uint16_t)id[0] << 8) | id[1];
    }

  return ret;
}

static int sc3336_probe_id(FAR struct i2c_master_s *i2c,
                           FAR uint16_t *chipid)
{
  uint16_t previous = 0xffff;
  unsigned int stable = 0;
  unsigned int cycle;
  unsigned int retry;
  int ret = -ENODEV;

  for (cycle = 0; cycle < 2; cycle++)
    {
      for (retry = 0; retry < 10; retry++)
        {
          ret = sc3336_read_id(i2c, chipid);
          if (ret >= 0 && *chipid == SC3336_CHIP_ID)
            {
              stable = previous == *chipid ? stable + 1 : 1;
              previous = *chipid;
              if (stable >= 2)
                {
                  if (cycle != 0 || retry != 1)
                    {
                      syslog(LOG_INFO,
                             "CAMERA: SC3336 ID stabilized after "
                             "power-cycle=%u retry=%u\n", cycle, retry);
                    }

                  return 0;
                }
            }
          else
            {
              stable = 0;
              if (ret >= 0)
                {
                  previous = *chipid;
                }
            }

          nxsig_usleep(5000);
        }

      if (cycle == 0)
        {
          syslog(LOG_WARNING,
                 "CAMERA: SC3336 ID unstable (%04x), retrying power-on\n",
                 *chipid);
          rv1103_gpio3_write(SC3336_PWDN_PIN, false);
          nxsig_usleep(5000);
          sc3336_enable_mclk();
          rv1103_gpio3_write(SC3336_PWDN_PIN, true);
          nxsig_usleep(20000);
          previous = 0xffff;
          stable = 0;
        }
    }

  return ret < 0 ? ret : -ENODEV;
}

static int sc3336_write_reg(FAR struct i2c_master_s *i2c, uint16_t reg,
                            uint8_t value)
{
  struct i2c_msg_s msg;
  uint8_t data[3] = {reg >> 8, reg & 0xff, value};
  msg.frequency = SC3336_I2C_FREQUENCY;
  msg.addr = SC3336_I2C_ADDRESS;
  msg.flags = 0;
  msg.buffer = data;
  msg.length = sizeof(data);
  return I2C_TRANSFER(i2c, &msg, 1);
}

static int sc3336_csi_checkpoint(FAR struct i2c_master_s *i2c)
{
  uintptr_t dphy = 0xff3e8000u;
  uintptr_t host = 0xffa20000u;
  uintptr_t vicru = 0xff3b4000u;
  uintptr_t grf = 0xff000000u;
  uint32_t state;
  uint32_t err1;
  uint32_t err2;
  unsigned int i;
  unsigned int retry;
  int ret;

  /* Enable PCLK_MIPICSIPHY, PCLK_CSIHOST0 and RXBYTECLKHS0, pulse their
   * documented resets, then reproduce the official full-mode two-lane
   * D-PHY setup.  SC3336 uses a 255-MHz link clock (510 Mb/s/lane), whose
   * upstream settle code is 0x0e.
   */
  sc3336_putreg(((1u << 3 | 1u << 4 | 1u << 14) << 16), vicru + 0x804);
  sc3336_putreg(((1u << 4 | 1u << 14) << 16) |
                (1u << 4 | 1u << 14), vicru + 0xa04);
  nxsig_usleep(5);
  sc3336_putreg((1u << 4 | 1u << 14) << 16, vicru + 0xa04);

  sc3336_putreg((0xfu << 16), grf + 0x50014);          /* force-rx off */
  sc3336_putreg((0xfu << 20) | (3u << 4), grf + 0x50014);
  sc3336_putreg((1u << 24) | (1u << 8), grf + 0x50014);
  sc3336_putreg(0x4c, dphy + 0x00);
  sc3336_putreg(0x1e, dphy + 0x80);
  sc3336_putreg(0x1f, dphy + 0x80);
  sc3336_putreg(0x0e, dphy + 0x160);
  sc3336_putreg(0x0e, dphy + 0x1e0);
  sc3336_putreg(0x0e, dphy + 0x260);
  sc3336_putreg(0x02, dphy + 0x44c);
  sc3336_putreg(0x04, dphy + 0x84);

  sc3336_putreg(0, host + 0x10);
  sc3336_putreg(1, host + 0x04);                       /* two lanes */
  sc3336_putreg(0x0c204000u, host + 0x40);             /* CSI FS/FE/LS/LE */
  sc3336_putreg(0, host + 0x28);
  sc3336_putreg(0xf000, host + 0x2c);
  sc3336_putreg(1, host + 0x10);

  /* 0x0103 takes effect before this polling I2C controller has always
   * observed the final ACK/STOP.  A resulting ENXIO is therefore expected,
   * not evidence that the sensor disappeared.  Give the internal state
   * machine time to restart before applying the remaining mode table.
   */

  ret = sc3336_write_reg(i2c, g_sc3336_30fps[0].reg,
                         g_sc3336_30fps[0].value);
  if (ret < 0 && ret != -ENXIO)
    {
      syslog(LOG_ERR, "CAMERA: SC3336 reset write failed: %d\n", ret);
      return ret;
    }

  nxsig_usleep(10000);

  for (i = 1; i < sizeof(g_sc3336_30fps) / sizeof(g_sc3336_30fps[0]); i++)
    {
      for (retry = 0; retry < 3; retry++)
        {
          ret = sc3336_write_reg(i2c, g_sc3336_30fps[i].reg,
                                 g_sc3336_30fps[i].value);
          if (ret >= 0)
            {
              break;
            }

          nxsig_usleep(1000);
        }

      if (ret < 0)
        {
          syslog(LOG_ERR,
                 "CAMERA: SC3336 mode write[%u] reg=%04x val=%02x "
                 "failed: %d\n", i, g_sc3336_30fps[i].reg,
                 g_sc3336_30fps[i].value, ret);
          return ret;
        }

      /* Linux scheduling naturally spaces these SCCB writes.  A short
       * bounded gap prevents back-to-back STOP/START races on the sensor.
       */

      nxsig_usleep(100);
    }

  for (retry = 0; retry < 3; retry++)
    {
  ret = sc3336_write_reg(i2c, 0x0100, 0x01);
      if (ret >= 0)
        {
          break;
        }

      nxsig_usleep(1000);
    }

  if (ret < 0)
    {
      syslog(LOG_ERR, "CAMERA: SC3336 stream-on failed: %d\n", ret);
      return ret;
    }

  g_sc3336_streaming = true;

  nxsig_usleep(100000);
  state = *(FAR volatile uint32_t *)(host + 0x14);
  err1 = *(FAR volatile uint32_t *)(host + 0x20);
  err2 = *(FAR volatile uint32_t *)(host + 0x24);
  syslog(LOG_INFO, "CAMERA: CSI2 phy=%08lx err=%08lx/%08lx\n",
         (unsigned long)state, (unsigned long)err1, (unsigned long)err2);
  snprintf(g_sc3336_status + strlen(g_sc3336_status),
           sizeof(g_sc3336_status) - strlen(g_sc3336_status),
           "CSI2 phy=0x%08lx err1=0x%08lx err2=0x%08lx\n",
           (unsigned long)state, (unsigned long)err1, (unsigned long)err2);
  return 0;
}

int luckfox_sc3336_initialize(void)
{
  FAR struct i2c_master_s *i2c;
  uint16_t chipid = 0xffff;
  int ret;

  /* Rails are always-on.  Keep PWDN inactive while starting XVCLK, then
   * follow the upstream no-reset-gpio delay and the 8192-cycle SCCB guard.
   */

  ret = rv1103_gpio3_config_output(SC3336_PWDN_PIN, false);
  if (ret < 0)
    {
      return ret;
    }

  sc3336_enable_mclk();
  nxsig_usleep(1000);
  rv1103_gpio3_write(SC3336_PWDN_PIN, true);
  nxsig_usleep(16000);
  nxsig_usleep(1000);

  i2c = rv1103_i2cbus_initialize(4);
  if (i2c == NULL)
    {
      return -ENODEV;
    }

  g_sc3336_i2c = i2c;

  ret = sc3336_probe_id(i2c, &chipid);
  if (ret < 0)
    {
      syslog(LOG_ERR,
             "CAMERA: SC3336 probe failed id=%04x at I2C4 address 0x30: %d\n",
             chipid, ret);
      return ret;
    }

  snprintf(g_sc3336_status, sizeof(g_sc3336_status),
           "SC3336 id=0x%04x i2c4=0x30 xvclk=24000000 lanes=2 "
           "mode=2304x1296@30\n", chipid);

  ret = sc3336_csi_checkpoint(i2c);
  if (ret < 0)
    {
      syslog(LOG_ERR, "CAMERA: CSI2 checkpoint failed: %d\n", ret);
    }
  else
    {
      ret = sc3336_capture_first_frame();
      if (ret < 0)
        {
          syslog(LOG_ERR, "CAMERA: RKCIF capture failed: %d\n", ret);
        }
      else
        {
          ret = sc3336_capture_burst();
          if (ret >= 0)
            {
              ret = video_register("/dev/video0", &g_sc3336_video);
              if (ret < 0 && ret != -EEXIST)
                {
                  syslog(LOG_ERR,
                         "CAMERA: /dev/video0 registration failed: %d\n",
                         ret);
                }
              else
                {
                  snprintf(g_sc3336_status + strlen(g_sc3336_status),
                           sizeof(g_sc3336_status) -
                           strlen(g_sc3336_status),
                           "RKCIF burst=/dev/video0 frames=%lu msec=%lu "
                           "hash=0x%08lx\n",
                           (unsigned long)g_sc3336_video_frames,
                           (unsigned long)g_sc3336_video_msec,
                           (unsigned long)g_sc3336_video_hash);
                }

              sc3336_isp_checkpoint();

              ret = video_register("/dev/video1", &g_sc3336_isp_video);
              if (ret < 0 && ret != -EEXIST)
                {
                  syslog(LOG_ERR,
                         "CAMERA: /dev/video1 registration failed: %d\n",
                         ret);
                }
              else
                {
                  syslog(LOG_INFO,
                         "CAMERA: /dev/video1 RKISP32 NV12 registered\n");
                }
            }
        }
    }

  ret = register_driver("/dev/sc3336", &g_sc3336_fops, 0666, NULL);
  if (ret < 0 && ret != -EEXIST)
    {
      return ret;
    }

  syslog(LOG_INFO,
         "CAMERA: SC3336 id=%04x detected; 24 MHz XVCLK, 2-lane MIPI\n",
         chipid);
  return 0;
}

#endif /* CONFIG_RV1103_SC3336_CHECKPOINT */
