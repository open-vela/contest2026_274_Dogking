/* SPDX-License-Identifier: Apache-2.0 */
#include <nuttx/config.h>
#include <system/a733_camera_image.h>
#include <system/a733_vision.h>
#ifdef CONFIG_A733_CAMERA_LIVE_TEST
#include <system/a733_camera_live.h>
#endif
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MODEL_BYTES (640u * 640u * 3u)
#define FILE_LIMIT (64u * 1024u * 1024u)
static int camera_command_active;
#ifdef CONFIG_A733_CAMERA_CONTROL_TEST
/* Empirical preview preset for the supplied V1.11/IMX214 samples, not AWB. */
static struct a733_camera_tone camera_tone={64,512,512,256,256,180};
static int tone_command(int argc,char **argv)
{
  if(argc==8)
    {
      uint32_t values[6];
      for(unsigned i=0;i<6;++i)
        {
          const char *p=argv[i+2];char *end;
          if(!*p || *p<'0' || *p>'9')return -EINVAL;
          errno=0;unsigned long n=strtoul(p,&end,10);
          if(errno || *end || n>UINT32_MAX)return -EINVAL;
          values[i]=(uint32_t)n;
        }
      struct a733_camera_tone t={values[0],values[1],values[2],values[3],values[4],values[5]};
      int ret=a733_camera_tone_valid(&t);if(ret)return ret;
      camera_tone=t;
    }
  printf("camera-tone-v203: black=%lu software-gain-q8=%lu RGB-q8=%lu/%lu/%lu rotation=%lu gamma=2.2; RAM-only manual-WB, NOT calibrated ISP or sensor exposure\n",
    (unsigned long)camera_tone.black,(unsigned long)camera_tone.gain_q8,
    (unsigned long)camera_tone.red_q8,(unsigned long)camera_tone.green_q8,
    (unsigned long)camera_tone.blue_q8,(unsigned long)camera_tone.rotation);
  return 0;
}
#endif

static int number(const char *text, uint32_t *value)
{
  char *end;
  if (!text || *text < '0' || *text > '9') return -EINVAL;
  errno = 0;
  unsigned long n = strtoul(text, &end, 10);
  if (errno || *end || !n || n > UINT32_MAX) return -EINVAL;
  *value = n;
  return 0;
}

static int format(const char *name, struct a733_camera_image *s)
{
  if (!strcmp(name, "rgb24")) {s->pixel = A733_CAMERA_RGB24; return 0;}
  static const char *const nv[] = {"nv12-601-limited", "nv12-709-limited",
    "nv12-601-full", "nv12-709-full"};
  for (unsigned i = 0; i < 4; ++i)
    if (!strcmp(name, nv[i]))
      {s->pixel = A733_CAMERA_NV12; s->yuv = i + 1; return 0;}
  static const char *const bayer[] = {"rggb", "grbg", "gbrg", "bggr"};
  const char *suffix;
  if (!strncmp(name, "raw10p-", 7))
    {s->pixel = A733_CAMERA_RAW10_PACKED; suffix = name + 7;}
  else if (!strncmp(name, "raw10le-", 8))
    {s->pixel = A733_CAMERA_RAW10_LE16; suffix = name + 8;}
  else return -ENOTSUP;
  for (unsigned i = 0; i < 4; ++i)
    if (!strcmp(suffix, bayer[i])) {s->bayer = i + 1; return 0;}
  return -ENOTSUP;
}

static int read_all(int fd, uint8_t *buffer, size_t size)
{
  while (size)
    {
      ssize_t n = read(fd, buffer, size > 32768 ? 32768 : size);
      if (n < 0 && errno == EINTR) continue;
      if (n <= 0) return n == 0 ? -EIO : -errno;
      buffer += n; size -= n;
    }
  uint8_t extra;
  ssize_t n;
  do {n = read(fd, &extra, 1);} while (n < 0 && errno == EINTR);
  return n == 0 ? 0 : n < 0 ? -errno : -EMSGSIZE;
}

static int write_new(const char *path, const void *data, size_t size)
{
  int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
  if (fd < 0) return -errno;
  const uint8_t *buffer = data;
  int ret = 0;
  while (size)
    {
      ssize_t n = write(fd, buffer, size > 32768 ? 32768 : size);
      if (n < 0 && errno == EINTR) continue;
      if (n <= 0) {ret = n == 0 ? -EIO : -errno; break;}
      buffer += n; size -= n;
    }
  if (!ret && fsync(fd) < 0) ret = -errno;
  if (close(fd) < 0 && !ret) ret = -errno;
  return ret;
}

static int prepare(char **argv)
{
  struct a733_camera_image s = {0};
  struct a733_camera_transform t;
  struct stat st;
  char path[512], marker[512];
  int ret = format(argv[2], &s);
  if (ret) return ret;
  if (number(argv[3], &s.width) || number(argv[4], &s.height) ||
      number(argv[5], &s.stride) || s.width > 8192 || s.height > 8192)
    return -EINVAL;
  if (strlen(argv[7]) > sizeof(path) - 32) return -ENAMETOOLONG;
  uint64_t expected = (uint64_t)s.stride * s.height;
  if (s.pixel == A733_CAMERA_NV12) expected += expected / 2;
  if (!expected || expected > FILE_LIMIT) return -EFBIG;
  int fd = open(argv[6], O_RDONLY | O_NONBLOCK);
  if (fd < 0) return -errno;
  if (fstat(fd, &st) < 0)
    {ret = -errno; close(fd); return ret;}
  if (!S_ISREG(st.st_mode) || st.st_size < 0 || (uint64_t)st.st_size != expected)
    {close(fd); return -EMSGSIZE;}
  s.bytes = (size_t)expected;
  uint8_t *input = malloc(s.bytes), *output = malloc(MODEL_BYTES);
  if (!input || !output)
    {free(input); free(output); close(fd); return -ENOMEM;}
  ret = read_all(fd, input, s.bytes);
  if (close(fd) < 0 && !ret) ret = -errno;
  s.data = input;
  if (!ret) ret = a733_camera_letterbox(&s, output, MODEL_BYTES, 640, 640, 114, &t);
  if (!ret && mkdir(argv[7], 0700) < 0) ret = -errno;
  if (!ret)
    {
      snprintf(path, sizeof(path), "%s/model-rgb.bin", argv[7]);
      ret = write_new(path, output, MODEL_BYTES);
    }
  if (!ret)
    {
      int n = snprintf(marker, sizeof(marker),
        "stage=cpu-file-prepared\nlive-camera=0\nhardware-verified=0\n"
        "format=RGB24 width=640 height=640 bytes=%u padding=114\n"
        "source-width=%lu source-height=%lu source-stride=%lu\n"
        "image-width=%lu image-height=%lu pad-x=%lu pad-y=%lu\n"
        "RAW=CPU-bilinear-NOT-ISP-AWB-AE-qualified\n",
        MODEL_BYTES, (unsigned long)s.width, (unsigned long)s.height,
        (unsigned long)s.stride, (unsigned long)t.image_width,
        (unsigned long)t.image_height, (unsigned long)t.pad_x,
        (unsigned long)t.pad_y);
      if (n < 0 || (size_t)n >= sizeof(marker)) ret = -EOVERFLOW;
      else
        {
          snprintf(path, sizeof(path), "%s/ready.partial", argv[7]);
          ret = write_new(path, marker, (size_t)n);
          if (!ret)
            {
              char committed[512];
              snprintf(committed, sizeof(committed), "%s/ready.txt", argv[7]);
              /* This containing directory was exclusively created above.
               * Publish only after complete writes, sync and close. A
               * failure preserves partial evidence without a ready marker.
               * A pre-existing output directory is never reused.
               */
              if (rename(path, committed) < 0) ret = -errno;
            }
        }
    }
  if (!ret)
    printf("camera: CPU file prepared: %s/model-rgb.bin (1228800 bytes); "
           "NOT live capture/NPU acceptance\n", argv[7]);
  else
    fprintf(stderr, "camera: preparation failed %d; no accepted result. "
            "Any newly created partial directory is preserved.\n", ret);
  free(output); free(input);
  return ret;
}

/* Runs on host AND on the board, with no file/device access or allocation.
 * Synthetic pixels exercise the same application code used by prepare.
 * These results must never be published as live-camera/NPU acceptance.
 */
static int selftest(void)
{
  uint8_t rgb[24], output[192], raw[32], nv12[24];
  struct a733_camera_transform t;
  struct a733_camera_image s =
    {.data = rgb, .bytes = sizeof(rgb), .width = 4, .height = 2,
     .stride = 12, .pixel = A733_CAMERA_RGB24, .sequence = 7,
     .timestamp_us = 123456};
  for (size_t i = 0; i < sizeof(rgb); i += 3)
    {rgb[i] = 255; rgb[i+1] = 0; rgb[i+2] = 0;}
  int ret = a733_camera_letterbox(&s, output, sizeof(output), 8, 8, 114, &t);
  if (ret) return ret;
  if (t.image_width != 8 || t.image_height != 4 || t.pad_x ||
      t.pad_y != 2 || t.sequence != 7 || t.timestamp_us != 123456)
    return -EIO;
  for (unsigned y = 0; y < 8; ++y)
    for (unsigned x = 0; x < 8; ++x)
      for (unsigned c = 0; c < 3; ++c)
        if (output[(y*8+x)*3+c] !=
            (y < 2 || y >= 6 ? 114 : c == 0 ? 255 : 0)) return -EIO;
  struct a733_camera_box model = {0, 200, 800, 600}, source;
  ret = a733_camera_unletterbox(&t, 7, &model, &source);
  if (ret || source.x1 || source.y1 || source.x2 != 400 || source.y2 != 200)
    return -EIO;
  if (a733_camera_unletterbox(&t, 8, &model, &source) != -ESTALE)
    return -EIO;
  /* Every supported explicit YUV range/matrix: neutral black. */
  s.data = nv12; s.bytes = sizeof(nv12); s.width = 4; s.height = 4;
  s.stride = 4; s.pixel = A733_CAMERA_NV12;
  memset(nv12+16, 128, 8);
  for (unsigned range = A733_CAMERA_BT601_LIMITED;
       range <= A733_CAMERA_BT709_FULL; ++range)
    {
      s.yuv = (enum a733_camera_yuv)range;
      memset(nv12, range <= A733_CAMERA_BT709_LIMITED ? 16 : 0, 16);
      ret = a733_camera_letterbox(&s, output, 48, 4, 4, 114, &t);
      if (ret) return ret;
      for (unsigned i = 0; i < 48; ++i) if (output[i]) return -EIO;
    }
  /* Uniform right-justified RAW10 white, all four Bayer orders. */
  for (unsigned i = 0; i < sizeof(raw); i += 2)
    {raw[i] = 255; raw[i+1] = 3;}
  s.data = raw; s.bytes = sizeof(raw); s.stride = 8;
  s.pixel = A733_CAMERA_RAW10_LE16;
  for (unsigned bayer = A733_CAMERA_RGGB; bayer <= A733_CAMERA_BGGR; ++bayer)
    {
      s.bayer = (enum a733_camera_bayer)bayer;
      ret = a733_camera_letterbox(&s, output, 48, 4, 4, 114, &t);
      if (ret) return ret;
      for (unsigned i = 0; i < 48; ++i) if (output[i] != 255) return -EIO;
    }
  memset(output, 0xa5, sizeof(output));
  s.bytes = 1;
  if (!a733_camera_letterbox(&s, output, 48, 4, 4, 114, &t)) return -EIO;
  for (unsigned i = 0; i < sizeof(output); ++i)
    if (output[i] != 0xa5) return -EIO;
  puts("camera-selftest-v191: PASS synthetic RGB letterbox/geometry, stale "
       "sequence, 4 YUV profiles, 4 Bayer orders, rejected short frame\n"
       "CPU software only; NOT camera capture, ISP, NPU or hardware acceptance.");
  return 0;
}

static int dump_node(const char *node)
{
  char text[256];
  int fd = open(node, O_RDONLY);
  if (fd < 0) return -errno;
  int ret = 0;
  for (;;)
    {
      ssize_t n = read(fd, text, sizeof(text));
      if (n < 0 && errno == EINTR) continue;
      if (n <= 0) {if (n < 0) ret = -errno; break;}
      if (fwrite(text, 1, (size_t)n, stdout) != (size_t)n) {ret = -EIO; break;}
    }
  if (close(fd) < 0 && !ret) ret = -errno;
  return ret;
}

#ifdef CONFIG_A733_CAMERA_CONTROL_TEST
static void camera_boot_help(void)
{
  puts("camera: missing same-boot handoff is NOT an LCD/NPU error.\n"
       "Reboot, interrupt SD U-Boot, then run:\n"
       "sunxi_card0_probe\nmmc dev 0\nmmc rescan\nmmc part\n"
#ifdef CONFIG_A733_CAMERA_PERF_TEST
       "ext4load mmc 0:3 0x4fc00000 /boot/openvela/a733/camera-v205.scr && source 0x4fc00000\n"
#elif defined(CONFIG_A733_CAMERA_LIVE_TEST)
       "ext4load mmc 0:3 0x4fc00000 /boot/openvela/a733/camera-v204.scr && source 0x4fc00000\n"
#else
       "ext4load mmc 0:3 0x4fc00000 /boot/openvela/a733/camera-v203.scr && source 0x4fc00000\n"
#endif
       "After NSH starts, run camera capture with a NEW directory.\n"
       "Do NOT first run prepare/id/mode/link. No saveenv, PMIC writes or UFS erase required.");
}
static int control_command(const char *command)
{
  int fd=open("/dev/a733-camera-control",O_WRONLY);if(fd<0)return -errno;
  size_t len=strlen(command);ssize_t n=write(fd,command,len);
  int r=n<0?-errno:(size_t)n!=len?-EIO:0;
  if(close(fd)<0 && !r)r=-errno;
  return r;
}
static int save_capture(const char *directory,const char *order,bool capture,const char *input_file)
{
  char path[512],fmt[32],metadata[512];
  struct a733_camera_image s={.width=1920,.height=1080,.stride=3840,.sequence=1};
  struct a733_camera_transform transform,model_transform;
  if(strlen(directory)>sizeof(path)-32 || strlen(order)>4)return -ENAMETOOLONG;
  struct stat output_stat;
  if(stat(directory,&output_stat)==0)return -EEXIST;
  if(errno!=ENOENT)return -errno;
  snprintf(fmt,sizeof(fmt),"raw10le-%s",order);
  int ret=format(fmt,&s);if(ret)return ret;
  size_t bytes=3840u*1080u,preview_bytes=128u*160u*3u;
  uint8_t *raw=malloc(bytes),*preview=malloc(preview_bytes+32),*model=malloc(MODEL_BYTES);
  if(!raw || !preview || !model){free(raw);free(preview);free(model);return -ENOMEM;}
  if(capture)
    {
      ret=control_command("prepare");
      if(ret==-ENODATA)camera_boot_help();
      if(!ret)ret=control_command("frame-experiment");
      int report=dump_node("/dev/a733-camera-control");if(!ret)ret=report;
    }
  int fd=-1;
  if(!ret){fd=open(input_file?input_file:"/dev/a733-camera-frame",O_RDONLY);if(fd<0)ret=-errno;}
  if(fd>=0)
    {
      struct stat st;
      if(fstat(fd,&st)<0)ret=-errno;
      else if(input_file ? (!S_ISREG(st.st_mode) || st.st_size!=(off_t)bytes) : !S_ISCHR(st.st_mode))ret=-ENODEV;
      if(!ret)ret=read_all(fd,raw,bytes);
      if(close(fd)<0 && !ret)ret=-errno;
    }
  s.data=raw;s.bytes=bytes;
  if(!ret && input_file)
    for(size_t i=1;i<bytes;i+=2)if(raw[i]>3){ret=-EPROTO;break;}
  if(!ret)ret=a733_camera_letterbox(&s,preview,preview_bytes,128,160,0,&transform);
  /* The model uses unlevelled native RAW, not the display-only stretched RGB.
   * Keep this exact tensor as the LVGL background and NPU input identity. */
  if(!ret)ret=a733_camera_letterbox(&s,model,MODEL_BYTES,640,640,114,&model_transform);
  uint32_t legacy_crc=ret?0:a733_vision_crc32(model,MODEL_BYTES,0);
  /* Display-only global levels; preserve untouched native RAW. No ISP/AWB
   * calibration claim. Skip black letterbox bars when estimating levels. */
  if(!ret)
    {
      unsigned lo=255,hi=0;
      for(unsigned y=transform.pad_y;y<transform.pad_y+transform.image_height;++y)
        for(unsigned x=transform.pad_x;x<transform.pad_x+transform.image_width;++x)
          for(unsigned c=0;c<3;++c)
            {unsigned v=preview[(y*128+x)*3+c];if(v<lo)lo=v;if(v>hi)hi=v;}
      if(hi>lo+3)
        for(unsigned y=transform.pad_y;y<transform.pad_y+transform.image_height;++y)
          for(unsigned x=transform.pad_x;x<transform.pad_x+transform.image_width;++x)
            for(unsigned c=0;c<3;++c)
              {unsigned i=(y*128+x)*3+c;preview[i]=(preview[i]-lo)*255/(hi-lo);}
      if(mkdir(directory,0700)<0)ret=-errno;
    }
  if(!ret){snprintf(path,sizeof(path),"%s/frame.raw",directory);ret=write_new(path,raw,bytes);}
  if(!ret){snprintf(path,sizeof(path),"%s/preview.rgb",directory);ret=write_new(path,preview,preview_bytes);}
  if(!ret){snprintf(path,sizeof(path),"%s/model-rgb.bin",directory);ret=write_new(path,model,MODEL_BYTES);}
  if(!ret)
    {
      static const char header[]="P6\n128 160\n255\n";
      memmove(preview+sizeof(header)-1,preview,preview_bytes);
      memcpy(preview,header,sizeof(header)-1);
      snprintf(path,sizeof(path),"%s/preview.ppm",directory);
      ret=write_new(path,preview,preview_bytes+sizeof(header)-1);
    }
  if(!ret)ret=a733_camera_letterbox_tone(&s,preview,preview_bytes,128,160,0,&camera_tone,&transform);
  if(!ret){snprintf(path,sizeof(path),"%s/view.rgb",directory);ret=write_new(path,preview,preview_bytes);}
  if(!ret)
    {
      static const char header[]="P6\n128 160\n255\n";
      memmove(preview+sizeof(header)-1,preview,preview_bytes);memcpy(preview,header,sizeof(header)-1);
      snprintf(path,sizeof(path),"%s/view.ppm",directory);ret=write_new(path,preview,preview_bytes+sizeof(header)-1);
    }
  if(!ret)ret=a733_camera_letterbox_tone(&s,model,MODEL_BYTES,640,640,114,&camera_tone,&model_transform);
  if(!ret){snprintf(path,sizeof(path),"%s/vision-rgb.bin",directory);ret=write_new(path,model,MODEL_BYTES);}
  if(!ret)
    {
      int n=snprintf(metadata,sizeof(metadata),
        "profile=cpu-raw-tone-v203\nblack=%lu\ngain-q8=%lu\nRGB-q8=%lu/%lu/%lu\nrotation=%lu\ngamma=2.2\n"
        "source-raw-crc32=%08lx\nvision-input-crc32=%08lx convention=NuttX-seed0-no-xor\n"
        "input=640x640-RGB24-padding114\nimage=640x360 pad=0,140\nmanual-WB=not-calibrated\naccuracy-qualified=0\n",
        (unsigned long)camera_tone.black,(unsigned long)camera_tone.gain_q8,
        (unsigned long)camera_tone.red_q8,(unsigned long)camera_tone.green_q8,(unsigned long)camera_tone.blue_q8,
        (unsigned long)camera_tone.rotation,(unsigned long)a733_vision_crc32(raw,bytes,0),
        (unsigned long)a733_vision_crc32(model,MODEL_BYTES,0));
      if(n<0 || (size_t)n>=sizeof(metadata))ret=-EOVERFLOW;
      else {snprintf(path,sizeof(path),"%s/VIEW.txt",directory);ret=write_new(path,metadata,n);}
    }
  if(!ret)
    {
      int n=snprintf(metadata,sizeof(metadata),
        "origin=%s\nwidth=1920\nheight=1080\nstride=3840\nbytes=%lu\n"
        "pixel=%s\npreview=128x160-RGB24-letterbox-display-only-levels\nISP-colour-qualified=0\n"
        "model=640x640-RGB24-padding114-unlevelled\nmodel-image=%lux%lu pad=%lu,%lu\n"
        "model-input-crc32=%08lx convention=NuttX-seed0-no-xor\n",
        input_file?"user-supplied-raw-file":"real-published-DMA-frame",(unsigned long)bytes,fmt,(unsigned long)model_transform.image_width,
        (unsigned long)model_transform.image_height,(unsigned long)model_transform.pad_x,
        (unsigned long)model_transform.pad_y,(unsigned long)legacy_crc);
      if(n<0 || (size_t)n>=sizeof(metadata))ret=-EOVERFLOW;
      else
        {
          snprintf(path,sizeof(path),"%s/COMPLETE.partial",directory);
          ret=write_new(path,metadata,n);
          if(!ret)
            {char committed[512];snprintf(committed,sizeof(committed),"%s/COMPLETE.txt",directory);
             if(rename(path,committed)<0)ret=-errno;}
        }
    }
  free(model);free(preview);free(raw);
  if(!ret && !input_file)printf("camera: real RAW + RGB/PPM preview saved in %s; display image %s/preview.rgb\n",directory,directory);
  if(!ret && input_file)printf("camera-v203: file-derived CPU output in %s; NOT a new camera capture\n",directory);
  if(!ret)printf("camera-v203: enhanced upright preview: display image %s/view.rgb; optional vision input %s/vision-rgb.bin (see VIEW.txt; legacy files retained)\n",directory,directory);
  else fprintf(stderr,"camera: NO new COMPLETE result accepted; do NOT run display/NPU on missing output. Inspect camera control status; reboot if cleanup/owned remain nonzero.\n");
  return ret;
}
#endif

static int camera_dispatch(int argc, char **argv)
{
  int ret;
  if (argc == 2 && !strcmp(argv[1], "board")) ret = dump_node("/dev/a733-camera");
#ifdef CONFIG_A733_CAMERA_CONTROL_TEST
#ifdef CONFIG_A733_CAMERA_LIVE_TEST
  else if(argc>=2 && !strcmp(argv[1],"live"))ret=a733_camera_live_command(argc,argv,&camera_tone);
#ifdef CONFIG_A733_CAMERA_PERF_TEST
  else if(argc>=2 && (!strcmp(argv[1],"live-stream") || !strcmp(argv[1],"live-pipe") || !strcmp(argv[1],"live-legacy") || !strcmp(argv[1],"live-verify") || !strcmp(argv[1],"live-async") || !strcmp(argv[1],"live-async-verify") || !strcmp(argv[1],"live-cpu")))ret=a733_camera_live_command(argc,argv,&camera_tone);
#endif
#endif
  else if((argc==2 || argc==8) && !strcmp(argv[1],"tone"))ret=tone_command(argc,argv);
  else if(argc==2 && !strcmp(argv[1],"boot-help")){camera_boot_help();ret=0;}
  else if((argc==3 || argc==4) && (!strcmp(argv[1],"capture") || !strcmp(argv[1],"export")))
    ret=save_capture(argv[2],argc==4?argv[3]:"bggr",!strcmp(argv[1],"capture"),NULL);
  else if((argc==4 || argc==5) && !strcmp(argv[1],"process"))
    ret=save_capture(argv[3],argc==5?argv[4]:"bggr",false,argv[2]);
  else if (argc == 3 && !strcmp(argv[1], "control"))
    {
      ret=0;
      if (strcmp(argv[2],"status"))
        {
          if (strcmp(argv[2],"prepare") && strcmp(argv[2],"park") &&
              strcmp(argv[2],"calibrate") && strcmp(argv[2],"normal-clock") &&
              strcmp(argv[2],"id-experiment") && strcmp(argv[2],"mode-experiment") &&
              strcmp(argv[2],"link-experiment") && strcmp(argv[2],"frame-experiment")) ret=-EINVAL;
          else
            {
              int fd=open("/dev/a733-camera-control",O_WRONLY);
              if (fd<0) ret=-errno;
              else
                {
                  size_t n=strlen(argv[2]);ssize_t written=write(fd,argv[2],n);
                  if (written<0)ret=-errno;else if ((size_t)written!=n)ret=-EIO;
                  if (close(fd)<0 && !ret)ret=-errno;
                }
            }
        }
      int report=dump_node("/dev/a733-camera-control");if (!ret)ret=report;
    }
#endif
  else if (argc == 2 && !strcmp(argv[1], "selftest")) ret = selftest();
  else if (argc == 8 && !strcmp(argv[1], "prepare")) ret = prepare(argv);
  else
    {
      puts("camera board  (read-only inventory; requires checkpoint node)\n"
           "camera selftest  (synthetic CPU pixels; no sensor/device/NPU access)\n"
#ifdef CONFIG_A733_CAMERA_CONTROL_TEST
#ifdef CONFIG_A733_CAMERA_LIVE_TEST
           "camera live <seconds:1..3600> [detect-every:0..60] [model.a7pm] (0=preview only; background task)\n"
           "camera live-status | camera live-stop (cooperative stop, no forced hardware reset)\n"
#ifdef CONFIG_A733_CAMERA_PERF_TEST
           "camera live-stream <seconds> [detect-every] [model] (sensor-on/gated DMA; separate candidate)\n"
           "camera live-pipe <seconds> [detect-every] [model] (two owned RGB slots, same-frame YOLO/display)\n"
           "camera live-cpu (bounded A76 migration and hardware CRC proof; no camera/NPU/SPI)\n"
           "camera live-async[-verify] <seconds> [detect-every] [model] (RGB/NPU/SPI three stages; same-frame two-slot ownership)\n"
           "camera live-legacy <seconds> [detect-every] [model] (old one-shot NPU and two-flush path)\n"
           "camera live-verify <seconds> [detect-every] [model] (same-input legacy/cold/warm output check)\n"
#endif
#endif
           "camera boot-help (startup steps; ZERO device access)\n"
           "camera control status|prepare|calibrate|normal-clock|id-experiment|mode-experiment|link-experiment|frame-experiment|park\n"
#ifdef CONFIG_A733_CAMERA_LIVE_TEST
           "camera capture <NEW-directory> [bggr|rggb|grbg|gbrg] (shared clean-cycle budget; see control status)\n"
#else
           "camera capture <NEW-directory> [bggr|rggb|grbg|gbrg] (up to32 clean RAW cycles per same-boot handoff)\n"
#endif
           "camera tone [black gainQ8 redQ8 greenQ8 blueQ8 rotation:0/180] (preview software only; gains64..2048, black0..255)\n"
           "camera export <NEW-directory> [bggr|rggb|grbg|gbrg] (already completed frame, no sensor operation)\n"
           "camera process <saved-1920x1080-RAW10-LE16-file> <NEW-directory> [bayer] (CPU/file only, no camera I/O)\n"
#endif
           "camera prepare <format> <width> <height> <stride> <one-frame-file> <NEW-directory>\n"
           "formats: rgb24 | nv12-{601,709}-{limited,full} | raw10{p,le}-{rggb,grbg,gbrg,bggr}\n"
           "FILE preparation only, NOT sensor/video capture or realtime inference.");
      return 1;
    }
  if (ret) fprintf(stderr, "camera: error=%d\n", ret);
  return ret ? 1 : 0;
}
int main(int argc,char **argv)
{
#ifdef CONFIG_A733_CAMERA_LIVE_TEST
  if(argc==2 && (!strcmp(argv[1],"live-status") || !strcmp(argv[1],"live-stop")))
    return a733_camera_live_command(argc,argv,&camera_tone)?1:0;
#endif
  if(__atomic_exchange_n(&camera_command_active,1,__ATOMIC_ACQUIRE))
    {fputs("camera: another camera CLI operation active\n",stderr);return 1;}
  int ret;
#ifdef CONFIG_A733_CAMERA_LIVE_TEST
  if(a733_camera_live_busy())
    {fputs("camera: live task owns camera; use live-stop and wait active=0\n",stderr);ret=1;}
  else
#endif
    ret=camera_dispatch(argc,argv);
  __atomic_store_n(&camera_command_active,0,__ATOMIC_RELEASE);return ret;
}
