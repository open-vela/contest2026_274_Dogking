/****************************************************************************
 * apps/system/a733display/a733display_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

#include <nuttx/board.h>

#include <arch/board/board.h>

#include <arch/chip/a733_peripherals.h>

#include <lvgl/lvgl.h>

#include <system/a733_vision.h>
#ifdef CONFIG_A733_CAMERA_LIVE_TEST
#include <system/a733_camera_live.h>
#endif

#include "aipet_ui_ipc.h"

static volatile sig_atomic_t g_stop;
static int g_lv_flush_status;
static uint32_t g_lv_flush_count;
static uint32_t g_lv_frame_count;
static uint64_t g_lv_pixel_count;
static int g_display_owner;
#ifdef CONFIG_A733_PRODUCT_V1
#include <sched.h>
static int product_pet_active,product_pet_stop,product_pet_error;
#endif

_Static_assert(sizeof(struct a733_vision_message_s) == 32,
               "vision IPC must match CONFIG_MQ_MAXMSGSIZE");

/* Ten scan lines keep the LVGL working set small while still amortizing
 * rendering.  Static storage avoids handing an LCD transfer buffer to the
 * heap allocator while a flush is in progress.
 */

static uint16_t g_lv_drawbuf[128 * 10];

enum display_mode_e
{
  DISPLAY_MODE_TEST,
  DISPLAY_MODE_PET,
  DISPLAY_MODE_DEMO,
  DISPLAY_MODE_FPS,
  DISPLAY_MODE_VISION
};

enum pet_emotion_e
{
  PET_NEUTRAL,
  PET_HAPPY,
  PET_SAD,
  PET_SURPRISE,
  PET_ANGRY,
  PET_THINKING,
  PET_COMFORT
};

struct pet_scene_s
{
  lv_obj_t *screen;
  lv_obj_t *band;
  lv_obj_t *left_eye;
  lv_obj_t *right_eye;
  lv_obj_t *mouth;
  lv_obj_t *accent[3];
  enum pet_emotion_e emotion;
  unsigned int emotion_ms;
  unsigned int animation_ms;
  bool blink;
};

struct vision_scene_s
{
  lv_obj_t *screen;
  lv_obj_t *band;
  lv_obj_t *image_object;
  lv_obj_t *edge[A733_VISION_MAX_BOXES][4];
  struct a733_vision_batch_s batch;
  lv_image_dsc_t image;
};

struct vision_background_s {uint16_t *pixels;uint32_t crc;};
static int vision_image_load(const char *path,struct vision_background_s *bg)
{
  uint8_t row[640*3],extra;struct stat st;int r=0;
  memset(bg,0,sizeof(*bg));
  int fd=open(path,O_RDONLY|O_NONBLOCK);if(fd<0)return -errno;
  if(fstat(fd,&st)<0)r=-errno;
  else if(!S_ISREG(st.st_mode) || st.st_size!=640*640*3)r=-EMSGSIZE;
  if(!r){bg->pixels=malloc(128*128*2);if(!bg->pixels)r=-ENOMEM;}
  for(unsigned y=0;!r && y<640;++y)
    {
      size_t have=0;
      while(have<sizeof(row))
        {
          ssize_t n=read(fd,row+have,sizeof(row)-have);
          if(n<0 && errno==EINTR)continue;
          if(n<=0){r=n<0?-errno:-EIO;break;}have+=(size_t)n;
        }
      if(r)break;
      bg->crc=a733_vision_crc32(row,sizeof(row),bg->crc);
      /* Exact 5:1 centre sampling of the SAME square model input; no
       * independent contrast/resize that could displace detection boxes. */
      if(y%5==2)for(unsigned x=0;x<128;++x)
        {unsigned i=(5*x+2)*3;bg->pixels[(y/5)*128+x]=
          (uint16_t)((row[i]>>3)<<11 | (row[i+1]>>2)<<5 | row[i+2]>>3);}
    }
  if(!r){ssize_t n;do{n=read(fd,&extra,1);}while(n<0 && errno==EINTR);
    if(n)r=n<0?-errno:-EMSGSIZE;}
  if(close(fd)<0 && !r)r=-errno;
  if(r){free(bg->pixels);bg->pixels=NULL;}
  return r;
}

static void display_flush(lv_display_t *disp, const lv_area_t *area,
                          uint8_t *pixels)
{
  unsigned int width = (unsigned int)(area->x2 - area->x1 + 1);
  unsigned int height = (unsigned int)(area->y2 - area->y1 + 1);
  int ret;

  ret = a7z_st7735_write_area(area->x1, area->y1, area->x2, area->y2,
                              (const uint16_t *)pixels, width * height);
  if (ret < 0)
    {
      g_lv_flush_status = ret;
    }
  else
    {
      g_lv_flush_count++;
      g_lv_pixel_count += (uint64_t)width * height;
      if (lv_display_flush_is_last(disp))
        {
          g_lv_frame_count++;
        }
    }

  lv_display_flush_ready(disp);
}

static int display_prepare(void)
{
  unsigned int row;
  int ret;

  if (a7z_st7735_is_ready())
    {
      return OK;
    }

  puts("display: initializing ST7735 direct 8-bit path");
  ret = board_lcd_initialize();
  if (ret < 0)
    {
      fprintf(stderr, "display: board LCD initialization failed: %d\n", ret);
      return ret;
    }

  puts("display: ST7735 direct path ready");

  /* Controller GRAM is undefined after reset.  Clear it before returning so
   * a status probe cannot expose a frame of random colored pixels.
   */

  memset(g_lv_drawbuf, 0, sizeof(g_lv_drawbuf));
  for (row = 0; row < 160; row += 10)
    {
      ret = a7z_st7735_write_area(0, row, 127, row + 9,
                                  g_lv_drawbuf, 128 * 10);
      if (ret < 0)
        {
          fprintf(stderr, "display: initial clear failed at row %u: %d\n",
                  row, ret);
          return ret;
        }
    }

  return OK;
}

static void display_signal(int signo)
{
  (void)signo;
  g_stop = 1;
}

static lv_obj_t *display_block(lv_obj_t *parent, int x, int y, int w, int h,
                               uint32_t rgb, int radius)
{
  lv_obj_t *block = lv_obj_create(parent);

  lv_obj_remove_style_all(block);
  lv_obj_set_pos(block, x, y);
  lv_obj_set_size(block, w, h);
  lv_obj_set_style_bg_opa(block, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(block, lv_color_hex(rgb), 0);
  lv_obj_set_style_radius(block, radius, 0);
  return block;
}

static enum pet_emotion_e pet_emotion_from_name(const char *name)
{
  if (!strcmp(name, "开心") || !strcmp(name, "兴奋") ||
      !strcmp(name, "得意") || !strcmp(name, "调皮") ||
      !strcmp(name, "赞") || !strcmp(name, "happy"))
    {
      return PET_HAPPY;
    }

  if (!strcmp(name, "难过") || !strcmp(name, "委屈") ||
      !strcmp(name, "sad"))
    {
      return PET_SAD;
    }

  if (!strcmp(name, "惊讶") || !strcmp(name, "害怕") ||
      !strcmp(name, "surprise"))
    {
      return PET_SURPRISE;
    }

  if (!strcmp(name, "生气") || !strcmp(name, "鄙视") ||
      !strcmp(name, "angry"))
    {
      return PET_ANGRY;
    }

  if (!strcmp(name, "思考") || !strcmp(name, "疑惑") ||
      !strcmp(name, "困惑") || !strcmp(name, "好奇") ||
      !strcmp(name, "thinking"))
    {
      return PET_THINKING;
    }

  if (!strcmp(name, "安慰") || !strcmp(name, "害羞") ||
      !strcmp(name, "期待") || !strcmp(name, "comfort"))
    {
      return PET_COMFORT;
    }

  return PET_NEUTRAL;
}

static void pet_object_geometry(lv_obj_t *object, int x, int y,
                                int width, int height, int radius)
{
  lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_pos(object, x, y);
  lv_obj_set_size(object, width, height);
  lv_obj_set_style_radius(object, radius, 0);
}

static void pet_hide_accents(struct pet_scene_s *scene)
{
  unsigned int index;

  for (index = 0; index < 3; index++)
    {
      lv_obj_add_flag(scene->accent[index], LV_OBJ_FLAG_HIDDEN);
    }
}

static void pet_apply_emotion(struct pet_scene_s *scene,
                              enum pet_emotion_e emotion)
{
  uint32_t background = 0xf7fbff;
  uint32_t band = 0x68b5ff;

  scene->emotion = emotion;
  scene->emotion_ms = 0;
  scene->animation_ms = 0;
  scene->blink = false;
  pet_hide_accents(scene);
  pet_object_geometry(scene->left_eye, 27, 54, 18, 28, 9);
  pet_object_geometry(scene->right_eye, 83, 54, 18, 28, 9);
  pet_object_geometry(scene->mouth, 39, 116, 50, 7, 4);

  switch (emotion)
    {
      case PET_HAPPY:
        background = 0xfff7d6;
        band = 0xffb52e;
        pet_object_geometry(scene->left_eye, 24, 66, 24, 6, 3);
        pet_object_geometry(scene->right_eye, 80, 66, 24, 6, 3);
        pet_object_geometry(scene->mouth, 35, 108, 58, 12, 6);
        break;

      case PET_SAD:
        background = 0xeaf3ff;
        band = 0x5b80c9;
        pet_object_geometry(scene->left_eye, 29, 58, 15, 22, 7);
        pet_object_geometry(scene->right_eye, 84, 58, 15, 22, 7);
        pet_object_geometry(scene->mouth, 47, 122, 34, 6, 3);
        pet_object_geometry(scene->accent[0], 43, 79, 5, 13, 3);
        lv_obj_set_style_bg_color(scene->accent[0],
                                  lv_color_hex(0x419cff), 0);
        break;

      case PET_SURPRISE:
        background = 0xfff2e8;
        band = 0xff7043;
        pet_object_geometry(scene->left_eye, 25, 51, 22, 32, 11);
        pet_object_geometry(scene->right_eye, 81, 51, 22, 32, 11);
        pet_object_geometry(scene->mouth, 53, 106, 22, 25, 11);
        break;

      case PET_ANGRY:
        background = 0xffeaea;
        band = 0xe23c3c;
        pet_object_geometry(scene->left_eye, 24, 64, 26, 8, 4);
        pet_object_geometry(scene->right_eye, 78, 64, 26, 8, 4);
        pet_object_geometry(scene->mouth, 43, 119, 42, 7, 3);
        pet_object_geometry(scene->accent[0], 23, 48, 30, 5, 2);
        pet_object_geometry(scene->accent[1], 75, 48, 30, 5, 2);
        break;

      case PET_THINKING:
        background = 0xf1ecff;
        band = 0x8b68d7;
        pet_object_geometry(scene->right_eye, 80, 65, 26, 6, 3);
        pet_object_geometry(scene->mouth, 57, 117, 14, 7, 3);
        pet_object_geometry(scene->accent[0], 101, 94, 7, 7, 4);
        pet_object_geometry(scene->accent[1], 108, 83, 10, 10, 5);
        pet_object_geometry(scene->accent[2], 116, 67, 12, 12, 6);
        break;

      case PET_COMFORT:
        background = 0xedfff3;
        band = 0x52bd78;
        pet_object_geometry(scene->left_eye, 24, 66, 24, 6, 3);
        pet_object_geometry(scene->right_eye, 80, 66, 24, 6, 3);
        pet_object_geometry(scene->mouth, 41, 109, 46, 10, 5);
        pet_object_geometry(scene->accent[0], 16, 90, 14, 7, 4);
        pet_object_geometry(scene->accent[1], 98, 90, 14, 7, 4);
        lv_obj_set_style_bg_color(scene->accent[0],
                                  lv_color_hex(0xff92a8), 0);
        lv_obj_set_style_bg_color(scene->accent[1],
                                  lv_color_hex(0xff92a8), 0);
        break;

      case PET_NEUTRAL:
      default:
        break;
    }

  lv_obj_set_style_bg_color(scene->screen, lv_color_hex(background), 0);
  lv_obj_set_style_bg_color(scene->band, lv_color_hex(band), 0);
}

static void pet_scene_create(lv_display_t *disp, struct pet_scene_s *scene)
{
  (void)disp;
  memset(scene, 0, sizeof(*scene));
  scene->screen = lv_screen_active();
  lv_obj_set_style_bg_opa(scene->screen, LV_OPA_COVER, 0);
  scene->band = display_block(scene->screen, 0, 0, 128, 18,
                              0x68b5ff, 0);
  scene->left_eye = display_block(scene->screen, 27, 54, 18, 28,
                                  0x17202a, 9);
  scene->right_eye = display_block(scene->screen, 83, 54, 18, 28,
                                   0x17202a, 9);
  scene->mouth = display_block(scene->screen, 39, 116, 50, 7,
                               0x17202a, 4);
  scene->accent[0] = display_block(scene->screen, 0, 0, 4, 4,
                                   0x17202a, 2);
  scene->accent[1] = display_block(scene->screen, 0, 0, 4, 4,
                                   0x17202a, 2);
  scene->accent[2] = display_block(scene->screen, 0, 0, 4, 4,
                                   0x17202a, 2);
  pet_apply_emotion(scene, PET_NEUTRAL);
}

static void pet_tick(struct pet_scene_s *scene, unsigned int elapsed_ms)
{
  bool blink;

  scene->emotion_ms += elapsed_ms;
  scene->animation_ms += elapsed_ms;
  if (scene->emotion != PET_NEUTRAL &&
      scene->emotion_ms >= AIPET_UI_IDLE_MS)
    {
      pet_apply_emotion(scene, PET_NEUTRAL);
      return;
    }

  if (scene->emotion != PET_NEUTRAL)
    {
      return;
    }

  blink = scene->animation_ms % 3200 >= 3000;
  if (blink != scene->blink)
    {
      scene->blink = blink;
      if (blink)
        {
          pet_object_geometry(scene->left_eye, 24, 66, 24, 5, 3);
          pet_object_geometry(scene->right_eye, 80, 66, 24, 5, 3);
        }
      else
        {
          pet_object_geometry(scene->left_eye, 27, 54, 18, 28, 9);
          pet_object_geometry(scene->right_eye, 83, 54, 18, 28, 9);
        }
    }
}

static mqd_t pet_queue_open(void)
{
  struct mq_attr attributes;

  memset(&attributes, 0, sizeof(attributes));
  attributes.mq_maxmsg = 8;
  attributes.mq_msgsize = AIPET_UI_MESSAGE_SIZE;
  return mq_open(AIPET_UI_QUEUE_NAME,
                 O_RDONLY | O_CREAT | O_NONBLOCK, 0666, &attributes);
}

static void pet_queue_drain(mqd_t queue, struct pet_scene_s *scene)
{
  char message[AIPET_UI_MESSAGE_SIZE + 1];
  ssize_t length;

  for (;;)
    {
      memset(message, 0, sizeof(message));
      length = mq_receive(queue, message, AIPET_UI_MESSAGE_SIZE, NULL);
      if (length < 0)
        {
          break;
        }

      message[AIPET_UI_MESSAGE_SIZE] = '\0';
      pet_apply_emotion(scene, pet_emotion_from_name(message));
      printf("display: emotion=%s\n", message);
    }
}

static void vision_hide_box(struct vision_scene_s *scene,
                            unsigned int index)
{
  unsigned int edge;

  for (edge = 0; edge < 4; edge++)
    {
      lv_obj_add_flag(scene->edge[index][edge], LV_OBJ_FLAG_HIDDEN);
    }
}

static int vision_scene_create(struct vision_scene_s *scene,
                                 const struct vision_background_s *bg)
{
  unsigned int index;
  unsigned int edge;

  memset(scene, 0, sizeof(*scene));
  scene->screen = lv_screen_active();
  lv_obj_set_style_bg_opa(scene->screen, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(scene->screen, lv_color_hex(0x101820), 0);
  if(bg)
    {
      scene->batch.require_crc=true;scene->batch.required_crc=bg->crc;
      scene->image.header.magic=LV_IMAGE_HEADER_MAGIC;
      scene->image.header.cf=LV_COLOR_FORMAT_RGB565;
      scene->image.header.w=128;scene->image.header.h=128;scene->image.header.stride=256;
      scene->image.data_size=128*128*2;scene->image.data=(const uint8_t*)bg->pixels;
      lv_obj_t *image=lv_image_create(scene->screen);
      if(!image)return -ENOMEM;
      scene->image_object=image;
      lv_image_set_src(image,&scene->image);lv_obj_set_pos(image,0,24);
    }
  scene->band = display_block(scene->screen, 0, 0, 128, 18,
                              0x44505c, 0);
  for (index = 0; index < A733_VISION_MAX_BOXES; index++)
    {
      for (edge = 0; edge < 4; edge++)
        {
          scene->edge[index][edge] = display_block(scene->screen,
                                                   0, 20, 2, 2,
                                                   0x00ff66, 0);
        }

      vision_hide_box(scene, index);
    }
  return 0;
}

static int vision_map_x(uint16_t value)
{
  int mapped = (int)value * 128 / A733_VISION_MODEL_SIZE;
  return mapped > 127 ? 127 : mapped;
}

static int vision_map_y(uint16_t value)
{
  int mapped = 24 + (int)value * 128 / A733_VISION_MODEL_SIZE;
  return mapped > 151 ? 151 : mapped;
}

static void vision_apply_detection(struct vision_scene_s *scene,
                                   const struct a733_vision_message_s *message)
{
  static const uint32_t colors[] =
  {
    0x00ff66, 0xffcc00, 0x00bfff, 0xff5577
  };
  unsigned int index = message->index;
  int x1;
  int y1;
  int x2;
  int y2;
  int width;
  int height;
  unsigned int edge;
  uint32_t color;

  if (index >= A733_VISION_MAX_BOXES)
    {
      return;
    }

  x1 = vision_map_x(message->x1);
  y1 = vision_map_y(message->y1);
  x2 = vision_map_x(message->x2);
  y2 = vision_map_y(message->y2);
  if (x2 <= x1)
    {
      x2 = x1 < 127 ? x1 + 1 : 127;
    }

  if (y2 <= y1)
    {
      y2 = y1 < 151 ? y1 + 1 : 151;
    }

  width = x2 - x1 + 1;
  height = y2 - y1 + 1;
  color = colors[message->class_id %
                 (sizeof(colors) / sizeof(colors[0]))];
  for (edge = 0; edge < 4; edge++)
    {
      lv_obj_remove_flag(scene->edge[index][edge], LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_style_bg_color(scene->edge[index][edge],
                                lv_color_hex(color), 0);
    }

  lv_obj_set_pos(scene->edge[index][0], x1, y1);
  lv_obj_set_size(scene->edge[index][0], width, 2);
  lv_obj_set_pos(scene->edge[index][1], x1, y2 > 1 ? y2 - 1 : y2);
  lv_obj_set_size(scene->edge[index][1], width, 2);
  lv_obj_set_pos(scene->edge[index][2], x1, y1);
  lv_obj_set_size(scene->edge[index][2], 2, height);
  lv_obj_set_pos(scene->edge[index][3], x2 > 1 ? x2 - 1 : x2, y1);
  lv_obj_set_size(scene->edge[index][3], 2, height);
}

static mqd_t vision_queue_open(void)
{
  struct mq_attr attributes;

  memset(&attributes, 0, sizeof(attributes));
  attributes.mq_maxmsg = 8;
  attributes.mq_msgsize = sizeof(struct a733_vision_message_s);
  return mq_open(A733_VISION_QUEUE_NAME,
                 O_RDONLY | O_CREAT | O_NONBLOCK, 0666, &attributes);
}

static void vision_queue_drain(mqd_t queue, struct vision_scene_s *scene)
{
  struct a733_vision_message_s message;
  unsigned int index;
  ssize_t length;

  for (unsigned processed=0;processed<8;++processed)
    {
      length = mq_receive(queue, (char *)&message, sizeof(message), NULL);
      if (length < 0)
        {
          break;
        }

      if (length != sizeof(message) || message.magic != A733_VISION_MAGIC ||
          message.version != A733_VISION_VERSION)
        {
          puts("display: ignored invalid vision message");
          continue;
        }

      int update=a733_vision_batch_feed(&scene->batch,&message);
      if(update==A733_VISION_IGNORED)continue;
      if(update==A733_VISION_INVALID){puts("display: invalid vision batch ignored");continue;}
      if (message.type == A733_VISION_FRAME)
        {
          for (index = 0; index < A733_VISION_MAX_BOXES; index++)
            {
              vision_hide_box(scene, index);
            }

          lv_obj_set_style_bg_color(scene->band,
            lv_color_hex(update==A733_VISION_WRONG_INPUT?0xff6600:0x44505c), 0);
          if(update==A733_VISION_WRONG_INPUT)
            {puts("display: input CRC mismatch; NO boxes published over this image");continue;}
          printf("display: vision frame=%lu boxes=%u npu=%lu us\n",
                 (unsigned long)message.sequence,
                 (unsigned int)message.count,
                 (unsigned long)message.value);
        }
      if(update==A733_VISION_COMPLETE)
        {
          for(index=0;index<scene->batch.count;++index)
            {
              const struct a733_vision_message_s *d=&scene->batch.boxes[index];
              vision_apply_detection(scene,d);
              printf("display: vision det=%u class=%u score=%u.%03u\n",d->index,
                     d->class_id,d->score_milli/1000,d->score_milli%1000);
            }
          lv_obj_set_style_bg_color(scene->band,
            lv_color_hex(scene->batch.count?0x25a65a:0x44505c),0);
          printf("display: complete vision batch=%lu boxes=%u input-bound=%u\n",
            (unsigned long)scene->batch.sequence,scene->batch.count,scene->batch.require_crc);
        }
    }
}

static void display_test_scene(lv_display_t *disp)
{
  lv_obj_t *screen = lv_screen_active();
  int width = lv_display_get_horizontal_resolution(disp);
  int height = lv_display_get_vertical_resolution(disp);
  int band = width / 3;

  lv_obj_set_style_bg_color(screen, lv_color_hex(0xffffff), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

  /* RGB bars prove color order.  Their unequal widths plus the face below
   * make rotation and clipping errors immediately visible.
   */

  display_block(screen, 0, 0, band, 20, 0xff0000, 0);
  display_block(screen, band, 0, band, 20, 0x00ff00, 0);
  display_block(screen, band * 2, 0, width - band * 2, 20, 0x0000ff, 0);
  display_block(screen, width / 4 - 8, height / 2 - 20,
                16, 24, 0x202020, 8);
  display_block(screen, width * 3 / 4 - 8, height / 2 - 20,
                16, 24, 0x202020, 8);
  display_block(screen, width / 4, height * 3 / 4,
                width / 2, 8, 0x202020, 4);
}

static int display_raw_test(void)
{
  uint16_t line[128];
  unsigned int row;
  unsigned int col;
  int ret;

  ret = display_prepare();
  if (ret < 0)
    {
      return ret;
    }

  for (row = 0; row < 160; row++)
    {
      for (col = 0; col < 128; col++)
        {
          if (row < 3 || row >= 157 || col < 3 || col >= 125)
            {
              line[col] = 0xffff;
            }
          else if (col < 42)
            {
              line[col] = 0xf800;
            }
          else if (col < 85)
            {
              line[col] = 0x07e0;
            }
          else
            {
              line[col] = 0x001f;
            }
        }

      ret = a7z_st7735_write_area(0, row, 127, row, line, 128);
      if (ret < 0)
        {
          fprintf(stderr, "display: raw row %u failed: %d\n", row, ret);
          return ret;
        }
    }

  puts("display: raw8 path passed (128x160 RGB565, MSB first)");
  return OK;
}

static int display_image_file(const char *path)
{
  const size_t bytes=128u*160u*3u;
  uint8_t *rgb=malloc(bytes);uint16_t *pixels=malloc(128u*160u*sizeof(*pixels));
  if(!rgb || !pixels){free(rgb);free(pixels);return -ENOMEM;}
  int fd=open(path,O_RDONLY),ret=fd<0?-errno:0;size_t done=0;
  while(!ret && done<bytes)
    {
      ssize_t n=read(fd,rgb+done,bytes-done);
      if(n<0 && errno==EINTR)continue;
      if(n<=0){ret=n<0?-errno:-EMSGSIZE;break;}done+=(size_t)n;
    }
  if(!ret)
    {
      uint8_t extra;ssize_t n;
      do{n=read(fd,&extra,1);}while(n<0 && errno==EINTR);
      if(n)ret=n<0?-errno:-EMSGSIZE;
    }
  if(fd>=0 && close(fd)<0 && !ret)ret=-errno;
  if(!ret)
    for(unsigned i=0;i<128u*160u;++i)
      pixels[i]=(uint16_t)(((rgb[i*3]&0xf8)<<8)|((rgb[i*3+1]&0xfc)<<3)|(rgb[i*3+2]>>3));
  if(!ret)ret=display_prepare();
  if(!ret)ret=a7z_st7735_write_area(0,0,127,159,pixels,128u*160u);
  free(pixels);free(rgb);
  if(!ret)puts("display: RGB24 file shown; image origin depends on the supplied file");
  return ret;
}

static uint64_t display_micros(const struct timespec *value)
{
  return (uint64_t)value->tv_sec * UINT64_C(1000000) +
         (uint64_t)value->tv_nsec / UINT64_C(1000);
}

static int display_run_image(unsigned int seconds, enum display_mode_e mode,
                               const struct vision_background_s *bg)
{
  static const char *demo_emotions[] =
  {
    "普通", "开心", "难过", "惊讶", "生气", "思考", "安慰"
  };

  lv_display_t *disp;
  struct pet_scene_s pet;
  struct vision_scene_s vision;
  struct sigaction action;
  struct sigaction previous;
  mqd_t queue = (mqd_t)-1;
  struct timespec benchmark_start;
  struct timespec benchmark_now;
  uint64_t benchmark_us = 0;
  uint64_t fps_milli;
  uint64_t throughput_kib;
  unsigned int elapsed = 0;
  unsigned int tick_elapsed = 0;
  unsigned int demo_elapsed = 0;
  unsigned int demo_index = 0;
  unsigned int frame_index = 0;
  int ret = OK;

  ret = display_prepare();
  if (ret < 0)
    {
      return ret;
    }

  if (lv_is_initialized())
    {
      fprintf(stderr, "display: LVGL is already owned by another task\n");
      return -EBUSY;
    }

  memset(&action, 0, sizeof(action));
  action.sa_handler = display_signal;
  sigemptyset(&action.sa_mask);
  sigaction(SIGINT, &action, &previous);
  g_stop = 0;

  g_lv_flush_status = OK;
  lv_init();
  disp = lv_display_create(128, 160);
  if (disp == NULL)
    {
      lv_deinit();
      sigaction(SIGINT, &previous, NULL);
      fprintf(stderr, "display: cannot create LVGL display\n");
      return -ENOMEM;
    }

  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(disp, g_lv_drawbuf, NULL, sizeof(g_lv_drawbuf),
                         LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(disp, display_flush);
  g_lv_flush_count = 0;
  g_lv_frame_count = 0;
  g_lv_pixel_count = 0;

  if (mode == DISPLAY_MODE_TEST)
    {
      display_test_scene(disp);
      puts("ST7735/LVGL test active; press Ctrl+C to stop.");
    }
  else if (mode == DISPLAY_MODE_FPS)
    {
      lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, 0);
      puts("ST7735/LVGL full-frame benchmark active; press Ctrl+C to stop.");
      clock_gettime(CLOCK_MONOTONIC, &benchmark_start);
    }
  else if (mode == DISPLAY_MODE_VISION)
    {
      queue = vision_queue_open();
      if (queue == (mqd_t)-1)
        {
          ret = -errno;
          fprintf(stderr, "display: cannot open vision queue: %d\n", ret);
          lv_display_delete(disp);
          lv_deinit();
          sigaction(SIGINT, &previous, NULL);
          return ret;
        }

      /* Discard at most one old queueful before accepting a fresh run. */
      struct a733_vision_message_s old;
      for(unsigned n=0;n<8;++n)if(mq_receive(queue,(char*)&old,sizeof(old),NULL)<0)break;
      ret=vision_scene_create(&vision,bg);
      if(ret<0)
        {mq_close(queue);mq_unlink(A733_VISION_QUEUE_NAME);lv_display_delete(disp);
         lv_deinit();sigaction(SIGINT,&previous,NULL);return ret;}
      if(bg)printf("display: model background input-crc32=%08lx origin=file; waiting for matching inference\n",(unsigned long)bg->crc);
      puts("YOLO LVGL overlay active; run 'npu yolo-ui <rgb.bin>' "
           "from another shell.");
    }
  else
    {
      queue = pet_queue_open();
      if (queue == (mqd_t)-1)
        {
          ret = -errno;
          fprintf(stderr, "display: cannot open AI pet UI queue: %d\n", ret);
          lv_display_delete(disp);
          lv_deinit();
          sigaction(SIGINT, &previous, NULL);
          return ret;
        }

      pet_scene_create(disp, &pet);
      pet_queue_drain(queue, &pet);
      if (mode == DISPLAY_MODE_DEMO)
        {
          puts("AIPET LVGL emotion demo active; press Ctrl+C to stop.");
        }
      else
        {
          puts("AIPET LVGL UI active; use 'display emotion <name>' from "
               "another shell.");
        }
    }

  while (!g_stop && (seconds == 0 || elapsed < seconds * 1000))
    {
#ifdef CONFIG_A733_PRODUCT_V1
      if(mode==DISPLAY_MODE_PET && __atomic_load_n(&product_pet_active,__ATOMIC_ACQUIRE) &&
         __atomic_load_n(&product_pet_stop,__ATOMIC_ACQUIRE))break;
#endif
      if (mode == DISPLAY_MODE_FPS)
        {
          static const uint32_t colors[] =
          {
            0x102040, 0x204010, 0x401020, 0x205080
          };
          unsigned int delta;

          lv_obj_set_style_bg_color(lv_screen_active(),
                                    lv_color_hex(colors[frame_index & 3]), 0);
          lv_obj_invalidate(lv_screen_active());
          lv_refr_now(disp);
          frame_index++;

          ret = g_lv_flush_status < 0 ? g_lv_flush_status :
                a733_spibus_last_error(1);
          if (ret < 0)
            {
              fprintf(stderr, "display: SPI transfer failed: %d\n", ret);
              break;
            }

          clock_gettime(CLOCK_MONOTONIC, &benchmark_now);
          benchmark_us = display_micros(&benchmark_now) -
                         display_micros(&benchmark_start);
          elapsed = (unsigned int)(benchmark_us / 1000);
          delta = elapsed - tick_elapsed;
          if (delta > 0)
            {
              lv_tick_inc(delta);
              tick_elapsed = elapsed;
            }

          continue;
        }

      uint32_t idle = lv_timer_handler();
      unsigned int delay = idle == 0 ? 1 : idle > 20 ? 20 : idle;

      if (mode == DISPLAY_MODE_VISION)
        {
          vision_queue_drain(queue, &vision);
        }
      else if (mode != DISPLAY_MODE_TEST)
        {
          pet_queue_drain(queue, &pet);
          pet_tick(&pet, delay);
          if (mode == DISPLAY_MODE_DEMO)
            {
              demo_elapsed += delay;
              if (demo_elapsed >= 2000)
                {
                  demo_elapsed = 0;
                  demo_index = (demo_index + 1) %
                               (sizeof(demo_emotions) /
                                sizeof(demo_emotions[0]));
                  pet_apply_emotion(&pet,
                    pet_emotion_from_name(demo_emotions[demo_index]));
                  printf("display: demo emotion=%s\n",
                         demo_emotions[demo_index]);
                }
            }
        }

      ret = g_lv_flush_status < 0 ? g_lv_flush_status :
            a733_spibus_last_error(1);
      if (ret < 0)
        {
          fprintf(stderr, "display: SPI transfer failed: %d\n", ret);
          break;
        }

      usleep(delay * 1000);
      /* This application owns the LVGL run loop, so it must also advance
       * LVGL's millisecond clock.  Without this, the first invalidated frame
       * is flushed but refresh, animation, and blink timers never become due.
       */

      lv_tick_inc(delay);
      elapsed += delay;
    }

  if (mode == DISPLAY_MODE_FPS)
    {
      if (benchmark_us == 0)
        {
          clock_gettime(CLOCK_MONOTONIC, &benchmark_now);
          benchmark_us = display_micros(&benchmark_now) -
                         display_micros(&benchmark_start);
        }

      fps_milli = benchmark_us == 0 ? 0 :
                  (uint64_t)g_lv_frame_count * UINT64_C(1000000000) /
                  benchmark_us;
      throughput_kib = benchmark_us == 0 ? 0 :
                       g_lv_pixel_count * 2 * UINT64_C(1000000) /
                       benchmark_us / 1024;
      printf("display: lvgl-fps frames=%lu elapsed=%llu ms "
             "fps=%llu.%03llu flushes=%lu pixels=%llu "
             "throughput=%llu KiB/s\n",
             (unsigned long)g_lv_frame_count,
             (unsigned long long)(benchmark_us / 1000),
             (unsigned long long)(fps_milli / 1000),
             (unsigned long long)(fps_milli % 1000),
             (unsigned long)g_lv_flush_count,
             (unsigned long long)g_lv_pixel_count,
             (unsigned long long)throughput_kib);
    }

  if (queue != (mqd_t)-1)
    {
      mq_close(queue);
      mq_unlink(mode == DISPLAY_MODE_VISION ? A733_VISION_QUEUE_NAME :
                AIPET_UI_QUEUE_NAME);
    }

  lv_display_delete(disp);
  lv_deinit();
  sigaction(SIGINT, &previous, NULL);
  return ret;
}

static int display_run(unsigned seconds,enum display_mode_e mode)
{return display_run_image(seconds,mode,NULL);}

static int display_send_emotion(const char *emotion)
{
  int ret = aipet_ui_notify(emotion);

  if (ret == -ENOENT)
    {
      fprintf(stderr,
              "display: UI is not running; start 'display pet' first\n");
    }
  else if (ret < 0)
    {
      fprintf(stderr, "display: cannot send emotion '%s': %d\n",
              emotion, ret);
    }
  else
    {
      printf("display: queued emotion=%s\n", emotion);
    }

  return ret;
}

static int display_parse_seconds(const char *text, unsigned int *seconds)
{
  char *end;
  unsigned long value = strtoul(text, &end, 10);

  if (*text == '\0' || *end != '\0' || value > 3600)
    {
      return -EINVAL;
    }

  *seconds = value;
  return OK;
}

static void display_usage(const char *progname)
{
  fprintf(stderr,
          "Usage:\n"
          "  %s status\n"
          "  %s raw8\n"
          "  %s image <128x160-RGB24-file>\n"
          "  %s test [seconds]\n"
          "  %s fps [seconds]\n"
          "  %s vision [seconds]\n"
          "  %s vision-image <640x640-RGB24-file> [seconds]\n"
          "  %s pet [seconds]\n"
          "  %s demo [seconds]\n"
          "  %s emotion <name>\n"
          "  %s run\n",
          progname, progname, progname, progname,
          progname, progname, progname, progname, progname, progname, progname);
}

static int display_dispatch(int argc, char *argv[])
{
  unsigned int seconds = 10;
  int ret;

  if(argc==3 && !strcmp(argv[1],"image"))
    {
      ret=display_image_file(argv[2]);
      if(ret)fprintf(stderr,"display: image failed=%d\n",ret);
      return ret?EXIT_FAILURE:EXIT_SUCCESS;
    }

  if((argc==3 || argc==4) && !strcmp(argv[1],"vision-image"))
    {
      seconds=120;
      if(argc==4 && display_parse_seconds(argv[3],&seconds)<0)return EXIT_FAILURE;
      struct vision_background_s bg;
      ret=vision_image_load(argv[2],&bg);
      if(ret)
        {fprintf(stderr,"display: model input missing/invalid: %s (%d); LCD was NOT initialized by this command. Verify camera capture/COMPLETE first.\n",argv[2],ret);return EXIT_FAILURE;}
      if(!ret){ret=display_run_image(seconds,DISPLAY_MODE_VISION,&bg);free(bg.pixels);}
      if(ret)fprintf(stderr,"display: vision image failed=%d\n",ret);
      return ret?EXIT_FAILURE:EXIT_SUCCESS;
    }

  if (argc == 2 && strcmp(argv[1], "status") == 0)
    {
      ret = display_prepare();
      printf("display: direct=%s controller=ST7735 resolution=128x160 "
             "format=RGB565-MSB spi=1 mode=0 frequency=12000000\n",
             ret == 0 ? "ready" : "failed");
      return ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }

  if (argc == 2 && (strcmp(argv[1], "raw8") == 0 ||
                    strcmp(argv[1], "raw") == 0))
    {
      return display_raw_test() < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
    }

  if ((argc == 2 || argc == 3) && strcmp(argv[1], "test") == 0)
    {
      if (argc == 3)
        {
          if (display_parse_seconds(argv[2], &seconds) < 0)
            {
              display_usage(argv[0]);
              return EXIT_FAILURE;
            }
        }

      return display_run(seconds, DISPLAY_MODE_TEST) < 0 ?
             EXIT_FAILURE : EXIT_SUCCESS;
    }

  if ((argc == 2 || argc == 3) && strcmp(argv[1], "fps") == 0)
    {
      seconds = 10;
      if (argc == 3)
        {
          if (display_parse_seconds(argv[2], &seconds) < 0 || seconds == 0)
            {
              display_usage(argv[0]);
              return EXIT_FAILURE;
            }
        }

      return display_run(seconds, DISPLAY_MODE_FPS) < 0 ?
             EXIT_FAILURE : EXIT_SUCCESS;
    }

  if ((argc == 2 || argc == 3) && strcmp(argv[1], "vision") == 0)
    {
      seconds = 0;
      if (argc == 3 && display_parse_seconds(argv[2], &seconds) < 0)
        {
          display_usage(argv[0]);
          return EXIT_FAILURE;
        }

      return display_run(seconds, DISPLAY_MODE_VISION) < 0 ?
             EXIT_FAILURE : EXIT_SUCCESS;
    }

  if ((argc == 2 || argc == 3) && strcmp(argv[1], "pet") == 0)
    {
      seconds = 0;
      if (argc == 3 && display_parse_seconds(argv[2], &seconds) < 0)
        {
          display_usage(argv[0]);
          return EXIT_FAILURE;
        }

      return display_run(seconds, DISPLAY_MODE_PET) < 0 ?
             EXIT_FAILURE : EXIT_SUCCESS;
    }

  if ((argc == 2 || argc == 3) && strcmp(argv[1], "demo") == 0)
    {
      seconds = 30;
      if (argc == 3 && display_parse_seconds(argv[2], &seconds) < 0)
        {
          display_usage(argv[0]);
          return EXIT_FAILURE;
        }

      return display_run(seconds, DISPLAY_MODE_DEMO) < 0 ?
             EXIT_FAILURE : EXIT_SUCCESS;
    }

  if (argc == 3 && strcmp(argv[1], "emotion") == 0)
    {
      return display_send_emotion(argv[2]) < 0 ?
             EXIT_FAILURE : EXIT_SUCCESS;
    }

  if (argc == 2 && strcmp(argv[1], "run") == 0)
    {
      return display_run(0, DISPLAY_MODE_PET) < 0 ?
             EXIT_FAILURE : EXIT_SUCCESS;
    }

  display_usage(argv[0]);
  return EXIT_FAILURE;
}

int main(int argc,char **argv)
{
  if(__atomic_exchange_n(&g_display_owner,1,__ATOMIC_ACQUIRE))
    {fputs("display: owned by another display/live task\n",stderr);return EXIT_FAILURE;}
  int r=display_dispatch(argc,argv);
  __atomic_store_n(&g_display_owner,0,__ATOMIC_RELEASE);return r;
}

#ifdef CONFIG_A733_PRODUCT_V1
static int product_pet_worker(int argc,char **argv)
{
  (void)argc;(void)argv;
  int fd=open("/dev/a7zconsole",O_WRONLY),rc=fd<0?-errno:0;
  if(!rc && (dup2(fd,1)<0 || dup2(fd,2)<0))rc=-errno;
  if(fd>=0)close(fd);
  if(!rc && __atomic_exchange_n(&g_display_owner,1,__ATOMIC_ACQUIRE))rc=-EBUSY;
  else if(!rc)
    {rc=display_run(0,DISPLAY_MODE_PET);__atomic_store_n(&g_display_owner,0,__ATOMIC_RELEASE);}
  __atomic_store_n(&product_pet_error,rc,__ATOMIC_RELEASE);
  __atomic_store_n(&product_pet_active,0,__ATOMIC_RELEASE);return rc?1:0;
}
int a733_display_pet_status(void)
{
  int error=__atomic_load_n(&product_pet_error,__ATOMIC_ACQUIRE);
  return error?error:__atomic_load_n(&product_pet_active,__ATOMIC_ACQUIRE);
}
int a733_display_pet_start(void)
{
  if(a733_camera_live_blocked())return -EBUSY;
  if(__atomic_exchange_n(&product_pet_active,1,__ATOMIC_ACQUIRE))return 0;
  __atomic_store_n(&product_pet_stop,0,__ATOMIC_RELEASE);
  __atomic_store_n(&product_pet_error,0,__ATOMIC_RELEASE);
  /* Initialize the SPI panel/PB0 reset synchronously BEFORE the voice worker
   * can open I2S on PB5..PB8. GUI and audio may then run with stable pinmux. */
  if(__atomic_exchange_n(&g_display_owner,1,__ATOMIC_ACQUIRE))
    {__atomic_store_n(&product_pet_active,0,__ATOMIC_RELEASE);return -EBUSY;}
  int prepare=display_prepare();
  __atomic_store_n(&g_display_owner,0,__ATOMIC_RELEASE);
  if(prepare){__atomic_store_n(&product_pet_error,prepare,__ATOMIC_RELEASE);__atomic_store_n(&product_pet_active,0,__ATOMIC_RELEASE);return prepare;}
  int pid=task_create("pet-screen",100,65536,product_pet_worker,NULL);
  if(pid<0){int rc=-errno;__atomic_store_n(&product_pet_error,rc,__ATOMIC_RELEASE);__atomic_store_n(&product_pet_active,0,__ATOMIC_RELEASE);return rc;}
  return 0;
}
int a733_display_pet_pause(void)
{
  __atomic_store_n(&product_pet_stop,1,__ATOMIC_RELEASE);
  for(unsigned i=0;i<300;i++) {
    if(!__atomic_load_n(&product_pet_active,__ATOMIC_ACQUIRE))return 0;
    usleep(10000);
  }
  return -EOWNERDEAD; /* Never release/reuse an unjoined SPI/LVGL owner. */
}
#endif
#ifdef CONFIG_A733_CAMERA_LIVE_TEST
struct live_display_s
{
  lv_display_t *display;
  struct vision_scene_s scene;
  lv_obj_t *status,*labels[A733_VISION_MAX_BOXES];
  uint16_t pixels[128*128];
  uint32_t sequence,crc;
  uint64_t last_us;
  bool completed;
};
static struct live_display_s *live_display;
int a733_display_live_begin(void)
{
  if(__atomic_exchange_n(&g_display_owner,1,__ATOMIC_ACQUIRE))return -EBUSY;
  int ret=0;
  if(lv_is_initialized())ret=-EBUSY;
  if(!ret && !(live_display=calloc(1,sizeof(*live_display))))ret=-ENOMEM;
  if(!ret)ret=display_prepare();
  if(ret)
    {free(live_display);live_display=NULL;__atomic_store_n(&g_display_owner,0,__ATOMIC_RELEASE);return ret;}
  lv_init();g_lv_flush_status=0;g_lv_frame_count=g_lv_flush_count=0;g_lv_pixel_count=0;
  live_display->display=lv_display_create(128,160);
  if(!live_display->display){lv_deinit();free(live_display);live_display=NULL;
    __atomic_store_n(&g_display_owner,0,__ATOMIC_RELEASE);return -ENOMEM;}
  lv_display_set_color_format(live_display->display,LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(live_display->display,g_lv_drawbuf,NULL,sizeof(g_lv_drawbuf),LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(live_display->display,display_flush);
  struct vision_background_s bg={.pixels=live_display->pixels};
  ret=vision_scene_create(&live_display->scene,&bg);
  if(!ret)
    {
      live_display->status=lv_label_create(live_display->scene.screen);
      if(!live_display->status)ret=-ENOMEM;
      else {lv_obj_set_pos(live_display->status,2,1);lv_obj_set_style_text_color(live_display->status,lv_color_hex(0xffffff),0);}
      for(unsigned i=0;!ret && i<A733_VISION_MAX_BOXES;++i)
        {
          lv_obj_t *label=lv_label_create(live_display->scene.screen);live_display->labels[i]=label;
          if(!label){ret=-ENOMEM;break;}
          lv_obj_set_width(label,80);lv_label_set_long_mode(label,LV_LABEL_LONG_CLIP);
          lv_obj_set_style_text_color(label,lv_color_hex(0xffffff),0);lv_obj_add_flag(label,LV_OBJ_FLAG_HIDDEN);
        }
    }
  struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);live_display->last_us=display_micros(&now);
  if(ret){a733_display_live_end();return ret;}
  puts("display: live owner ready; exact model-input background; no MQ pointer transfer");return 0;
}
static uint64_t display_live_now(void)
{struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return display_micros(&t);}
static int display_live_apply(const uint8_t *rgb,size_t bytes,const struct a733_live_result *result,
                             bool final,struct a733_live_display_profile *profile)
{
  uint64_t start=display_live_now(),tick=start;
  if(profile)memset(profile,0,sizeof(*profile));
  if(!live_display)return -ENODEV;
  if(!rgb || bytes!=A733_LIVE_RGB_BYTES)return -EMSGSIZE;
  int ret=a733_live_result_valid(result);if(ret)return ret;
  if(live_display->sequence && (int32_t)(result->sequence-live_display->sequence)<0)return -ESTALE;
  if(final && result->sequence==live_display->sequence)return -ESTALE;
  if(!final && result->kind==A733_LIVE_RESULT && (result->sequence!=live_display->sequence ||
     result->input_crc!=live_display->crc || live_display->completed))return -ESTALE;
  if(result->kind==A733_LIVE_PREVIEW && result->sequence==live_display->sequence)return -ESTALE;
  if(a733_vision_crc32(rgb,bytes,0)!=result->input_crc)return -EILSEQ;
  if(profile)profile->crc_us=display_live_now()-tick;
  tick=display_live_now();
  lv_image_cache_drop(&live_display->scene.image);
  for(unsigned y=0;y<128;++y)for(unsigned x=0;x<128;++x)
    {
      const uint8_t *p=rgb+((5*y+2)*640+5*x+2)*3;
      live_display->pixels[y*128+x]=(uint16_t)((p[0]>>3)<<11 | (p[1]>>2)<<5 | p[2]>>3);
    }
  for(unsigned i=0;i<A733_VISION_MAX_BOXES;++i)
    {vision_hide_box(&live_display->scene,i);lv_obj_add_flag(live_display->labels[i],LV_OBJ_FLAG_HIDDEN);}
  for(unsigned i=0;i<result->count;++i)
    {
      vision_apply_detection(&live_display->scene,&result->boxes[i]);
      lv_obj_t *label=live_display->labels[i];int x=vision_map_x(result->boxes[i].x1),y=vision_map_y(result->boxes[i].y1);
      lv_obj_set_pos(label,x>48?48:x,y>137?137:y);
      lv_label_set_text_fmt(label,"%u:%u%%",result->boxes[i].class_id,result->boxes[i].score_milli/10);
      lv_obj_remove_flag(label,LV_OBJ_FLAG_HIDDEN);
    }
  lv_label_set_text_fmt(live_display->status,"F%lu %s",(unsigned long)result->sequence,
    result->kind==A733_LIVE_RESULT?"YOLO":"VIEW");
  lv_obj_set_style_bg_color(live_display->scene.band,
    lv_color_hex(result->kind==A733_LIVE_RESULT?0x008844:0x44505c),0);
  struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);uint64_t us=display_micros(&now);
  lv_tick_inc((uint32_t)((us-live_display->last_us)/1000));live_display->last_us=us;
  if(profile)profile->draw_us=display_live_now()-tick;
  tick=display_live_now();
  /* Refresh the changing image, not the static whole 128x160 screen.
   * LVGL setters/hide operations above independently invalidate old/new
   * labels, boxes and status. No old box may survive on the new bitmap. */
  lv_obj_invalidate(live_display->scene.image_object);lv_refr_now(live_display->display);
  if(profile)profile->flush_us=display_live_now()-tick;
  ret=g_lv_flush_status<0?g_lv_flush_status:a733_spibus_last_error(1);
  if(!ret){live_display->sequence=result->sequence;live_display->crc=result->input_crc;
    live_display->completed=result->kind==A733_LIVE_RESULT;}
  if(profile)profile->total_us=display_live_now()-start;
  return ret;
}
int a733_display_live_frame(const uint8_t *rgb,size_t bytes,const struct a733_live_result *result)
{return display_live_apply(rgb,bytes,result,false,NULL);}
int a733_display_live_present(const uint8_t *rgb,size_t bytes,const struct a733_live_result *result,
                             struct a733_live_display_profile *profile)
{return display_live_apply(rgb,bytes,result,true,profile);}
int a733_display_live_end(void)
{
  if(!live_display)return -ENODEV;
  if(live_display->scene.screen)
    {
      for(unsigned i=0;i<A733_VISION_MAX_BOXES;++i)
        {
          if(live_display->labels[i])lv_obj_add_flag(live_display->labels[i],LV_OBJ_FLAG_HIDDEN);
          for(unsigned e=0;e<4;++e)if(live_display->scene.edge[i][e])lv_obj_add_flag(live_display->scene.edge[i][e],LV_OBJ_FLAG_HIDDEN);
        }
      if(live_display->status)lv_label_set_text(live_display->status,"STOP");
      if(live_display->scene.band)lv_obj_set_style_bg_color(live_display->scene.band,lv_color_hex(0x883300),0);
      lv_obj_invalidate(live_display->scene.screen);lv_refr_now(live_display->display);
    }
  int ret=g_lv_flush_status<0?g_lv_flush_status:a733_spibus_last_error(1);
  lv_image_cache_drop(&live_display->scene.image);
  lv_display_delete(live_display->display);lv_deinit();free(live_display);live_display=NULL;
  __atomic_store_n(&g_display_owner,0,__ATOMIC_RELEASE);return ret;
}
#endif
