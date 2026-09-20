/****************************************************************************
 * apps/system/a733display/a733display_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <nuttx/lcd/lcd.h>
#include <nuttx/lcd/lcd_dev.h>
#include <lvgl/lvgl.h>

/* Provided by the board ST7735 glue.  The panel is intentionally NOT brought
 * up during board bring-up, so the first display command registers /dev/lcd0.
 */

extern int a7z_lcd_ensure_registered(void);

/* Byte order of 16-bit SPI words, owned by the SPI lower half. */

extern void a733_spi_set_word_lsb(bool lsb);

static volatile sig_atomic_t g_stop;

static void display_signal(int signo)
{
  (void)signo;
  g_stop = 1;
}

static void display_block(lv_obj_t *parent, int x, int y, int w, int h,
                          uint32_t rgb, int radius)
{
  lv_obj_t *block = lv_obj_create(parent);

  lv_obj_remove_style_all(block);
  lv_obj_set_pos(block, x, y);
  lv_obj_set_size(block, w, h);
  lv_obj_set_style_bg_opa(block, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(block, lv_color_hex(rgb), 0);
  lv_obj_set_style_radius(block, radius, 0);
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

static int display_run(unsigned int seconds)
{
  lv_nuttx_dsc_t info;
  lv_nuttx_result_t result;
  struct sigaction action;
  struct sigaction previous;
  unsigned int elapsed = 0;
  int ret;

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

  /* Bring the panel up on demand.  This runs the ST7735 command table and the
   * initial full-screen clear, which is the slowest step of the whole chain, so
   * report it rather than appearing to hang.
   */

  puts("display: initializing ST7735 (/dev/lcd0) ...");

  ret = a7z_lcd_ensure_registered();
  if (ret < 0)
    {
      fprintf(stderr, "display: panel bring-up failed (%d)\n", ret);
      return ret;
    }

  lv_init();
  lv_nuttx_dsc_init(&info);
  info.fb_path = "/dev/lcd0";
  memset(&result, 0, sizeof(result));
  lv_nuttx_init(&info, &result);
  if (result.disp == NULL)
    {
      lv_deinit();
      sigaction(SIGINT, &previous, NULL);
      fprintf(stderr, "display: cannot initialize /dev/lcd0\n");
      return -ENODEV;
    }

  display_test_scene(result.disp);
  puts("ST7735/LVGL test active; press Ctrl+C to stop.");
  while (!g_stop && (seconds == 0 || elapsed < seconds * 1000))
    {
      uint32_t idle = lv_timer_handler();
      unsigned int delay = idle == 0 ? 1 : idle > 20 ? 20 : idle;
      usleep(delay * 1000);
      elapsed += delay;
    }

  lv_nuttx_deinit(&result);
  lv_deinit();
  sigaction(SIGINT, &previous, NULL);
  return OK;
}

static void display_usage(const char *progname)
{
  fprintf(stderr,
          "Usage:\n"
          "  %s status\n"
          "  %s test [seconds]\n"
          "  %s fill <RRGGBB> [RRGGBB ...]\n"
          "  %s run\n",
          progname, progname, progname, progname);
}

/****************************************************************************
 * Name: display_fill
 *
 * Description:
 *   Write solid RGB565 colours straight to /dev/lcd0, with no LVGL and no
 *   scene graph in the way.
 *
 *   This exists to tell a data-path fault from a rendering fault.  A solid
 *   field of one colour removes the test pattern, the LVGL colour conversion
 *   and the scene graph from the picture: if a solid red field still comes
 *   out as vertical stripes then the RGB565 stream itself is being misread
 *   (byte order or interface width), and no amount of panel setup will fix
 *   it.
 *
 ****************************************************************************/

static int display_fill(int argc, char *argv[])
{
  struct lcddev_area_s area;
  uint16_t *fb;
  uint32_t argb[8];
  int ncolors = 0;
  int fd;
  int i;

  if (argc < 3 || argc > 10)
    {
      return -EINVAL;
    }

  for (i = 2; i < argc; i++)
    {
      char *end;
      unsigned long rgb = strtoul(argv[i], &end, 16);

      if (*argv[i] == '\0' || *end != '\0' || rgb > 0xfffffful)
        {
          fprintf(stderr, "display: bad colour '%s'\n", argv[i]);
          return -EINVAL;
        }

      argb[ncolors++] = (uint32_t)rgb;
    }

  if (a7z_lcd_ensure_registered() < 0)
    {
      fprintf(stderr, "display: panel bring-up failed\n");
      return -ENODEV;
    }

  fd = open("/dev/lcd0", O_RDWR | O_CLOEXEC);
  if (fd < 0)
    {
      fprintf(stderr, "display: cannot open /dev/lcd0: %d\n", errno);
      return -errno;
    }

  /* CONFIG_LCD_RPORTRAIT swaps the axes: the framebuffer is 160 columns by
   * 128 rows.  LCDDEVIO_PUTAREA takes the whole frame in one call.
   */

  fb = malloc(160u * 128u * sizeof(uint16_t));
  if (fb == NULL)
    {
      close(fd);
      return -ENOMEM;
    }

  memset(&area, 0, sizeof(area));
  area.row_start = 0;
  area.row_end   = 127;
  area.col_start = 0;
  area.col_end   = 159;
  area.stride    = 160 * sizeof(uint16_t);
  area.data      = (FAR uint8_t *)fb;

  /* Show each requested colour for a couple of seconds, first with the
   * datasheet byte order and then with the swapped one.  Red, green and blue
   * together identify the channel order, and the two passes identify which
   * byte order this panel wants - all from a single run.
   */

  for (i = 0; i < ncolors; i++)
    {
      uint8_t r = (argb[i] >> 16) & 0xff;
      uint8_t g = (argb[i] >> 8) & 0xff;
      uint8_t b = argb[i] & 0xff;
      uint16_t pix = ((uint16_t)(r & 0xf8) << 8) |
                     ((uint16_t)(g & 0xfc) << 3) |
                     ((uint16_t)b >> 3);
      int pass;

      for (pass = 0; pass < 2; pass++)
        {
          size_t n;

          a733_spi_set_word_lsb(pass == 1);

          for (n = 0; n < 160u * 128u; n++)
            {
              fb[n] = pix;
            }

          printf("display: fill %02x%02x%02x rgb565=%04x order=%s\n",
                 r, g, b, pix, pass == 1 ? "lsb" : "msb");

          if (ioctl(fd, LCDDEVIO_PUTAREA,
                    (unsigned long)(uintptr_t)&area) < 0)
            {
              fprintf(stderr, "display: PUTAREA failed: %d\n", errno);
              break;
            }

          usleep(2000000);
        }
    }

  /* Leave the driver in the datasheet order. */

  a733_spi_set_word_lsb(false);

  free(fb);
  close(fd);
  return OK;
}

int main(int argc, char *argv[])
{
  struct stat info;
  unsigned int seconds = 10;
  int ret;

  if (argc == 2 && strcmp(argv[1], "status") == 0)
    {
      /* Register on demand so status reflects whether the panel can actually be
       * brought up, not merely whether someone else already did it.
       */

      ret = a7z_lcd_ensure_registered();
      if (ret < 0)
        {
          printf("display: lcd0=missing controller=ST7735 resolution=128x160 "
                 "format=RGB565 spi=1 mode=0 frequency=12000000 "
                 "bring-up-failed=%d\n", ret);
          return EXIT_FAILURE;
        }

      ret = stat("/dev/lcd0", &info);
      printf("display: lcd0=%s controller=ST7735 resolution=128x160 "
             "format=RGB565 spi=1 mode=0 frequency=12000000\n",
             ret == 0 ? "ready" : "missing");
      return ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }

  if ((argc == 2 || argc == 3) && strcmp(argv[1], "test") == 0)
    {
      if (argc == 3)
        {
          char *end;
          unsigned long value = strtoul(argv[2], &end, 10);
          if (*argv[2] == '\0' || *end != '\0' || value > 3600)
            {
              display_usage(argv[0]);
              return EXIT_FAILURE;
            }

          seconds = value;
        }

      return display_run(seconds) < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
    }

  if (argc >= 2 && strcmp(argv[1], "fill") == 0)
    {
      ret = display_fill(argc, argv);
      if (ret == -EINVAL)
        {
          display_usage(argv[0]);
        }

      return ret < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
    }

  if (argc == 2 && strcmp(argv[1], "run") == 0)
    {
      return display_run(0) < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
    }

  display_usage(argv[0]);
  return EXIT_FAILURE;
}
