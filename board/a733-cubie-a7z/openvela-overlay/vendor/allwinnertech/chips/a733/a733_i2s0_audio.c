/****************************************************************************
 * vendor/allwinnertech/chips/a733/a733_i2s0_audio.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/* Cubie A7Z I2S0 hardware acceptance driver.
 *
 * The node is passive at boot.  Clock, pinmux and stream state change only
 * after an explicit init/tone/capture command.  This keeps an optional,
 * incorrectly wired module from blocking startup while validating:
 *
 *   MAX98357A: PB5/BCLK, PB6/LRC, PB7/DIN (standard I2S, no MCLK)
 *   INMP441:   PB5/SCK,  PB6/WS,  PB8/SD, selectable L/R slot
 *   ICS43434:  PB5/SCK,  PB6/WS,  PB8/SD, both slots captured explicitly
 *
 * The acceptance path uses bounded polling and a 48 kHz, 2-slot, 32-bit
 * frame.  A standard NuttX I2S/audio upper-half can later wrap this proven
 * clock/register programming without changing the hardware layer.
 */

#include <nuttx/config.h>

#ifdef CONFIG_A733_I2S0_AUDIO

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/a733_audio.h>

#include <nuttx/arch.h>
#include <nuttx/clock.h>
#include <nuttx/fs/fs.h>
#include <nuttx/kmalloc.h>
#include <nuttx/mutex.h>

#include "arm64_internal.h"

#define A733_CCU_BASE          UINT64_C(0x02002000)
#define A733_PIO_BASE          UINT64_C(0x02000000)
#define A733_PIO_FIRST_BANK    UINT64_C(0x80)
#define A733_PIO_STRIDE        UINT64_C(0x80)
#define A733_PB_BASE           (A733_PIO_BASE + A733_PIO_FIRST_BANK + \
                                A733_PIO_STRIDE)
#define A733_PLL_AUDIO1        (A733_CCU_BASE + UINT64_C(0x0280))
#define A733_I2S0_BASE         UINT64_C(0x02532000)
#define A733_I2S0_CLK          (A733_CCU_BASE + UINT64_C(0x1200))
#define A733_I2S0_BGR          (A733_CCU_BASE + UINT64_C(0x120c))

#define PLL_AUDIO1_N_MASK      (0xffu << 8)
#define PLL_AUDIO1_DIV5_MASK   (7u << 16)
#define PLL_AUDIO1_DIV2_MASK   (7u << 20)
#define PLL_AUDIO1_OUTPUT      (1u << 27)
#define PLL_AUDIO1_LOCK        (1u << 28)
#define PLL_AUDIO1_LOCK_EN     (1u << 29)
#define PLL_AUDIO1_LDO_EN      (1u << 30)
#define PLL_AUDIO1_ENABLE      (1u << 31)

#define I2S_CTL                0x00
#define I2S_FMT0               0x04
#define I2S_FMT1               0x08
#define I2S_INTSTA             0x0c
#define I2S_RXFIFO             0x10
#define I2S_FIFOCTL            0x14
#define I2S_FIFOSTA            0x18
#define I2S_INTCTL             0x1c
#define I2S_TXFIFO             0x20
#define I2S_CLKDIV             0x24
#define I2S_TXCNT              0x28
#define I2S_RXCNT              0x2c
#define I2S_CHCFG              0x30
#define I2S_TX0CHSEL           0x34
#define I2S_TX0CHMAP0          0x44
#define I2S_TX0CHMAP1          0x48
#define I2S_RXCHSEL            0x64
#define I2S_RXCHMAP0           0x68
#define I2S_RXCHMAP1           0x6c
#define I2S_RXCHMAP2           0x70
#define I2S_RXCHMAP3           0x74
#define I2S_REV                0x7c

#define I2S_CTL_BCLK_OUT       (1u << 18)
#define I2S_CTL_LRCK_OUT       (1u << 17)
#define I2S_CTL_SDO0_EN        (1u << 8)
#define I2S_CTL_MODE_I2S       (1u << 4)
#define I2S_CTL_TXEN           (1u << 2)
#define I2S_CTL_RXEN           (1u << 1)
#define I2S_CTL_GLOBAL_EN      (1u << 0)

#define I2S_FIFOCTL_FTX        (1u << 25)
#define I2S_FIFOCTL_FRX        (1u << 24)
#define I2S_FIFOCTL_TXIM_1     (1u << 2)
#define I2S_FIFOCTL_RXOM_1     (1u << 0)
#define I2S_FIFOSTA_RXA        (1u << 8)
#define I2S_FIFO_TX_COUNT(v)   (((v) >> 16) & 0xffu)
#define I2S_FIFO_RX_COUNT(v)   ((v) & 0x7fu)

#define I2S_INTSTA_RX_UNDERRUN (1u << 2)
#define I2S_INTSTA_RX_OVERRUN  (1u << 1)

#define A733_AUDIO_RATE        48000u
#define A733_AUDIO_TIMEOUT_MS  100u
#define A733_INMP441_WARMUP_MS 100u
#define A733_INMP441_DEFAULT_SLOT 1u
#define A733_CAPTURE_PATH      "/data/inmp441.wav"
#define A733_CAPTURE_RAW_PATH  "/data/inmp441-raw32.bin"
#define A733_CAPTURE_TMP_PATH  "/data/.inmp441.wav.tmp"
#define A733_CAPTURE_RAW_TMP_PATH "/data/.inmp441-raw32.bin.tmp"
#define A733_ICS_CAPTURE_PATH  "/data/ics43434-stereo.wav"
#define A733_ICS_RAW_PATH      "/data/ics43434-stereo-s24.bin"

#define I2S_FMT0_LRCK_32       (31u << 8)
#define I2S_FMT0_RES_16        (3u << 4)
#define I2S_FMT0_RES_24        (5u << 4)
#define I2S_FMT0_RES_32        (7u << 4)
#define I2S_FMT0_SLOT_32       7u
#define I2S_FMT0_BCLK_POLARITY (1u << 7)
#define I2S_FMT0_LRCK_POLARITY (1u << 19)

struct a733_i2s0_diag_s
{
  mutex_t lock;
  bool initialized;
  bool pll_locked;
  unsigned int probes;
  unsigned int tones;
  unsigned int captures;
  unsigned int timeouts;
  unsigned int plays;
  unsigned int playback_volume;
  uint32_t play_rate;
  uint32_t play_channels;
  uint32_t play_frames;
  uint32_t play_sent;
  uint32_t play_elapsed;
  uint32_t play_intsta;
  int play_status;
  int last_status;
  int last_capture_status;
  bool last_capture_committed;
  uint32_t samples;
  uint32_t tx_count;
  uint32_t rx_count;
  uint32_t capture_nonzero;
  uint32_t capture_raw_nonzero;
  uint32_t capture_warmup;
  uint32_t capture_first_raw;
  uint32_t capture_last_raw;
  uint32_t capture_requested_ms;
  uint32_t capture_elapsed_ms;
  uint32_t capture_target_rx;
  uint32_t capture_consumed_rx;
  uint32_t capture_fifo_peak;
  uint32_t capture_intsta;
  uint32_t capture_slot;
  uint32_t capture_bclk;
  uint32_t capture_pull;
  uint32_t capture_fmt0;
  uint32_t capture_rxmap;
  uint32_t capture_pb_pull;
  bool capture_ics;
  uint32_t capture_rx_channels;
  uint32_t capture_file_channels;
  uint32_t capture_chcfg;
  uint32_t capture_rxchsel;
  uint32_t capture_expected_counter;
  int capture_counter_status;
  uint32_t capture_counter_points;
  uint32_t capture_counter_words[6];
  uint32_t capture_counter_hw[6];
  uint32_t capture_zero;
  uint32_t capture_sign_transitions;
  uint32_t capture_raw55;
  uint32_t capture_rawaa;
  uint64_t capture_sum_square;
  int16_t capture_min;
  int16_t capture_max;
  bool capture_voice;
  bool capture_processed;
  uint32_t capture_gain;
  uint32_t capture_clipped;
  int16_t capture_input_min;
  int16_t capture_input_max;
  uint32_t pll;
  uint32_t clock;
  uint32_t bus;
  uint32_t ctl;
  uint32_t fmt0;
  uint32_t fifoctl;
  uint32_t fifosta;
  uint32_t clkdiv;
  uint32_t chcfg;
  uint32_t txchsel;
  uint32_t txmap0;
  uint32_t txmap1;
  uint32_t rxchsel;
  uint32_t rxmap0;
  uint32_t rxmap1;
  uint32_t rxmap2;
  uint32_t rxmap3;
  uint32_t revision;
  uint32_t pb_cfg0;
  uint32_t pb_cfg1;
  uint32_t pb_pull;
  uint32_t pb_data;
  uint32_t tx_samples,tx_nonzero,tx_peak,tx_active_cfg,tx_active_ctl;
  uint32_t tx_active_fmt,tx_active_clock,tx_active_div;
  int tx_pin_status;
  char stage[24];
  char last_operation[16];
  char last_capture_mode[8];
  char last_capture_path[96];
};

static struct a733_i2s0_diag_s g_i2s0 =
{
  .lock = NXMUTEX_INITIALIZER,
  .last_status = -EAGAIN,
  .last_capture_status = -EAGAIN,
  .playback_volume = 25,
  .play_status = -EAGAIN,
  .stage = "passive",
  .last_operation = "none",
  .last_capture_mode = "none",
};

static void a733_pb_pinmux(unsigned int pin, unsigned int function)
{
  uintptr_t cfg = A733_PB_BASE + (pin / 8) * 4;
  uintptr_t drv = A733_PB_BASE + 0x20 + (pin / 8) * 4;
  uintptr_t pull = A733_PB_BASE + 0x30 + (pin / 16) * 4;
  unsigned int cfgshift = (pin % 8) * 4;
  unsigned int drvshift = (pin % 8) * 4;
  unsigned int pullshift = (pin % 16) * 2;
  uint32_t value;

  value = getreg32(cfg);
  value = (value & ~(0xfu << cfgshift)) | (function << cfgshift);
  putreg32(value, cfg);

  value = getreg32(drv);
  value = (value & ~(0xfu << drvshift)) | (1u << drvshift);
  putreg32(value, drv);

  value = getreg32(pull);
  value &= ~(3u << pullshift);
  putreg32(value, pull);
}

static void a733_pb_pull(unsigned int pin, unsigned int mode)
{
  uintptr_t pull = A733_PB_BASE + 0x30 + (pin / 16) * 4;
  unsigned int shift = (pin % 16) * 2;
  uint32_t value = getreg32(pull);

  /* Allwinner pull encoding: 0=disabled, 1=up, 2=down.  Allow explicit
   * disable/down comparison for the INMP441 tri-state data line.
   */

  value = (value & ~(3u << shift)) | (mode << shift);
  putreg32(value, pull);
}

static void a733_i2s0_dout_quiet(void)
{
  /* RX and MAX98357 share BCLK/LRCK. Disabling SDO0 alone does not
   * guarantee a defined level at the amplifier DIN. Preload the PB7 GPIO
   * latch LOW before selecting output mode. Never touch PB8/microphone SD,
   * and never repurpose the module's SD pin as an amplifier shutdown pin.
   * Call with TX/SDO stopped, before enabling capture clocks.
   */

  uint32_t value = getreg32(A733_PB_BASE + 0x10);
  putreg32(value & ~(1u << 7), A733_PB_BASE + 0x10);
  a733_pb_pinmux(7, 1); /* GPIO output, stable zero PCM for the amp */
}

static void a733_i2s0_probe(void)
{
  g_i2s0.pll = getreg32(A733_PLL_AUDIO1);
  g_i2s0.clock = getreg32(A733_I2S0_CLK);
  g_i2s0.bus = getreg32(A733_I2S0_BGR);
  g_i2s0.ctl = getreg32(A733_I2S0_BASE + I2S_CTL);
  g_i2s0.fmt0 = getreg32(A733_I2S0_BASE + I2S_FMT0);
  g_i2s0.fifoctl = getreg32(A733_I2S0_BASE + I2S_FIFOCTL);
  g_i2s0.fifosta = getreg32(A733_I2S0_BASE + I2S_FIFOSTA);
  g_i2s0.clkdiv = getreg32(A733_I2S0_BASE + I2S_CLKDIV);
  g_i2s0.chcfg = getreg32(A733_I2S0_BASE + I2S_CHCFG);
  g_i2s0.txchsel = getreg32(A733_I2S0_BASE + I2S_TX0CHSEL);
  g_i2s0.txmap0 = getreg32(A733_I2S0_BASE + I2S_TX0CHMAP0);
  g_i2s0.txmap1 = getreg32(A733_I2S0_BASE + I2S_TX0CHMAP1);
  g_i2s0.rxchsel = getreg32(A733_I2S0_BASE + I2S_RXCHSEL);
  g_i2s0.rxmap0 = getreg32(A733_I2S0_BASE + I2S_RXCHMAP0);
  g_i2s0.rxmap1 = getreg32(A733_I2S0_BASE + I2S_RXCHMAP1);
  g_i2s0.rxmap2 = getreg32(A733_I2S0_BASE + I2S_RXCHMAP2);
  g_i2s0.rxmap3 = getreg32(A733_I2S0_BASE + I2S_RXCHMAP3);
  g_i2s0.revision = getreg32(A733_I2S0_BASE + I2S_REV);
  g_i2s0.pb_cfg0 = getreg32(A733_PB_BASE + 0x00);
  g_i2s0.pb_cfg1 = getreg32(A733_PB_BASE + 0x04);
  g_i2s0.pb_pull = getreg32(A733_PB_BASE + 0x30);
  g_i2s0.pb_data = getreg32(A733_PB_BASE + 0x10);
  g_i2s0.probes++;
}

static int a733_i2s0_wait_pll(void)
{
  clock_t start = clock_systime_ticks();

  while ((getreg32(A733_PLL_AUDIO1) & PLL_AUDIO1_LOCK) == 0)
    {
      if (clock_systime_ticks() - start > MSEC2TICK(20))
        {
          return -ETIMEDOUT;
        }
    }

  return OK;
}

static int a733_i2s0_hw_initialize(void)
{
  uint32_t value;
  int ret;

  if (g_i2s0.initialized)
    {
      return OK;
    }

  strlcpy(g_i2s0.stage, "pll", sizeof(g_i2s0.stage));

  /* 24 MHz * 128 = 3.072 GHz; DIV5 supplies 614.4 MHz.  I2S0 then
   * divides by 25 to 24.576 MHz, and BCLK /8 gives 3.072 MHz.
   */

  value = getreg32(A733_PLL_AUDIO1);
  value &= ~(PLL_AUDIO1_ENABLE | PLL_AUDIO1_N_MASK |
             PLL_AUDIO1_DIV5_MASK | PLL_AUDIO1_DIV2_MASK);
  putreg32(value, A733_PLL_AUDIO1);
  up_udelay(10);
  value |= (127u << 8) | (4u << 16) | (1u << 20) |
           PLL_AUDIO1_OUTPUT | PLL_AUDIO1_LOCK_EN |
           PLL_AUDIO1_LDO_EN | PLL_AUDIO1_ENABLE;
  putreg32(value, A733_PLL_AUDIO1);

  ret = a733_i2s0_wait_pll();
  if (ret < 0)
    {
      g_i2s0.timeouts++;
      g_i2s0.last_status = ret;
      strlcpy(g_i2s0.stage, "pll-timeout", sizeof(g_i2s0.stage));
      a733_i2s0_probe();
      return ret;
    }

  g_i2s0.pll_locked = true;
  strlcpy(g_i2s0.stage, "clock", sizeof(g_i2s0.stage));
  value = getreg32(A733_I2S0_BGR);
  value |= (1u << 16) | 1u;
  putreg32(value, A733_I2S0_BGR);
  putreg32((1u << 31) | (2u << 24) | 24u, A733_I2S0_CLK);

  /* Stop the serializer before changing PB7 ownership. */

  putreg32(0, A733_I2S0_BASE + I2S_CTL);

  /* PB4/MCLK is intentionally untouched: neither module requires it. */

  a733_pb_pinmux(5, 3); /* BCLK */
  a733_pb_pinmux(6, 3); /* LRCK */
  a733_i2s0_dout_quiet(); /* Do not leave the amplifier DIN undefined. */
  a733_pb_pinmux(8, 3); /* DIN0 */
  a733_pb_pull(8, 2);

  strlcpy(g_i2s0.stage, "format", sizeof(g_i2s0.stage));
  putreg32(0, A733_I2S0_BASE + I2S_CTL);
  putreg32(0, A733_I2S0_BASE + I2S_INTCTL);
  putreg32(0, A733_I2S0_BASE + I2S_INTSTA);
  putreg32(I2S_FMT0_LRCK_32 | I2S_FMT0_RES_32 | I2S_FMT0_SLOT_32,
           A733_I2S0_BASE + I2S_FMT0);
  putreg32(0, A733_I2S0_BASE + I2S_FMT1);
  putreg32((5u << 4), A733_I2S0_BASE + I2S_CLKDIV);
  /* Default legacy state only.  Each capture programs its complete RX slot
   * count, channel count and mapping before enabling clocks.  ICS43434 uses
   * two FIFO channels, independently of which channel is exported to WAV.
   */

  putreg32(1u, A733_I2S0_BASE + I2S_CHCFG);
  putreg32((1u << 20) | (1u << 16) | 3u,
           A733_I2S0_BASE + I2S_TX0CHSEL);
  /* A733 uses the full 16-slot channel-map layout.  TX MAP1 describes
   * slots 0..7.  RX MAP3 describes channels 0..3, and each RX entry also
   * selects SDI0..3.  Explicitly initialize all four RX maps so channel 0
   * is sample 0 from SDI0 even if an earlier boot stage touched I2S0.
   */

  putreg32(0, A733_I2S0_BASE + I2S_TX0CHMAP0);
  putreg32(0x10u, A733_I2S0_BASE + I2S_TX0CHMAP1);
  putreg32(1u << 20, A733_I2S0_BASE + I2S_RXCHSEL);
  putreg32(0, A733_I2S0_BASE + I2S_RXCHMAP0);
  putreg32(0, A733_I2S0_BASE + I2S_RXCHMAP1);
  putreg32(0, A733_I2S0_BASE + I2S_RXCHMAP2);
  putreg32(0, A733_I2S0_BASE + I2S_RXCHMAP3);
  putreg32(I2S_FIFOCTL_FTX | I2S_FIFOCTL_FRX |
           I2S_FIFOCTL_TXIM_1 | I2S_FIFOCTL_RXOM_1,
           A733_I2S0_BASE + I2S_FIFOCTL);
  putreg32(0, A733_I2S0_BASE + I2S_TXCNT);
  putreg32(0, A733_I2S0_BASE + I2S_RXCNT);

  g_i2s0.initialized = true;
  g_i2s0.last_status = OK;
  strlcpy(g_i2s0.stage, "ready", sizeof(g_i2s0.stage));
  a733_i2s0_probe();
  return OK;
}

static void a733_i2s0_stream_stop(void)
{
  putreg32(0, A733_I2S0_BASE + I2S_INTCTL);
  putreg32(0, A733_I2S0_BASE + I2S_CTL);
  putreg32(I2S_FIFOCTL_FTX | I2S_FIFOCTL_FRX |
           I2S_FIFOCTL_TXIM_1 | I2S_FIFOCTL_RXOM_1,
           A733_I2S0_BASE + I2S_FIFOCTL);
  a733_i2s0_dout_quiet();
  strlcpy(g_i2s0.stage, "ready", sizeof(g_i2s0.stage));
}

static int a733_i2s0_wait_tx_space(void)
{
  clock_t start = clock_systime_ticks();

  while (I2S_FIFO_TX_COUNT(getreg32(A733_I2S0_BASE + I2S_FIFOSTA)) < 2)
    {
      if (clock_systime_ticks() - start > MSEC2TICK(A733_AUDIO_TIMEOUT_MS))
        {
          g_i2s0.timeouts++;
          return -ETIMEDOUT;
        }
    }

  return OK;
}

static int a733_i2s0_wait_rx_data(uint32_t *status)
{
  clock_t start = clock_systime_ticks();
  uint32_t value;

  for (;;)
    {
      value = getreg32(A733_I2S0_BASE + I2S_FIFOSTA);

      /* FSTA bit 7 is a hard-wired PLACE_HOLDER whose reset value is one.
       * It is not the eighth bit of RXA_CNT.  The old 0xff mask therefore
       * interpreted an empty FIFO (0x80) as 128 available words and read
       * stale RXFIFO data at CPU speed.  RXA bit 8 is the authoritative
       * non-empty indication; RXA_CNT occupies bits 6:0 only.
       */

      if ((value & I2S_FIFOSTA_RXA) != 0)
        {
          if (status != NULL)
            {
              *status = value;
            }

          return OK;
        }

      if (clock_systime_ticks() - start > MSEC2TICK(A733_AUDIO_TIMEOUT_MS))
        {
          g_i2s0.timeouts++;
          return -ETIMEDOUT;
        }
    }
}

static void a733_i2s0_set_sample_resolution(uint32_t resolution)
{
  uint32_t value = getreg32(A733_I2S0_BASE + I2S_FMT0);

  value &= ~(7u << 4);
  value |= resolution;
  putreg32(value, A733_I2S0_BASE + I2S_FMT0);
}

static void a733_i2s0_set_clock_polarity(unsigned int bclk)
{
  uint32_t value = getreg32(A733_I2S0_BASE + I2S_FMT0);

  /* Match vendor NB_NF/IB_NF: LRCK remains normal in both cases.
   * Call only while the stream is stopped.  A tone or legacy capture
   * always restores normal BCLK after an inverted matrix experiment.
   */

  value &= ~(I2S_FMT0_BCLK_POLARITY | I2S_FMT0_LRCK_POLARITY);
  value |= bclk ? I2S_FMT0_BCLK_POLARITY : 0;
  putreg32(value, A733_I2S0_BASE + I2S_FMT0);
}

/* Every explicit TX operation owns the stopped serializer under g_i2s0.lock.
 * Re-establish the complete proven TX profile, not only PB7. A cached
 * initialized flag does not prove that a previous RX/client left it intact.
 * Preserve microphone pins, RX maps, PB4/MCLK and unrelated bank fields. */
static int a733_i2s0_tx_prepare(bool fifo16)
{
  int r=a733_i2s0_hw_initialize();if(r<0)return r;
  a733_i2s0_stream_stop();
  a733_pb_pinmux(5,3);a733_pb_pinmux(6,3);
  putreg32(getreg32(A733_I2S0_BGR)|0x10001,A733_I2S0_BGR);
  putreg32(0x82000018,A733_I2S0_CLK);
  putreg32(0x50,A733_I2S0_BASE+I2S_CLKDIV);
  a733_i2s0_set_clock_polarity(0);
  a733_i2s0_set_sample_resolution(fifo16?I2S_FMT0_RES_16:I2S_FMT0_RES_32);
  putreg32(0,A733_I2S0_BASE+I2S_FMT1);
  putreg32((getreg32(A733_I2S0_BASE+I2S_CHCFG)&~15u)|1u,A733_I2S0_BASE+I2S_CHCFG);
  putreg32((1u<<20)|(1u<<16)|3u,A733_I2S0_BASE+I2S_TX0CHSEL);
  putreg32(0,A733_I2S0_BASE+I2S_TX0CHMAP0);
  putreg32(0x10,A733_I2S0_BASE+I2S_TX0CHMAP1);
  putreg32(0,A733_I2S0_BASE+I2S_TXCNT);
  g_i2s0.tx_samples=g_i2s0.tx_nonzero=g_i2s0.tx_peak=0;
  g_i2s0.tx_active_cfg=g_i2s0.tx_active_ctl=0;g_i2s0.tx_pin_status=-EINPROGRESS;
  return 0;
}
static void a733_i2s0_tx_sample(int16_t value)
{
  unsigned peak=value<0?-(int)value:value;
  ++g_i2s0.tx_samples;g_i2s0.tx_nonzero+=value!=0;
  if(peak>g_i2s0.tx_peak)g_i2s0.tx_peak=peak;
}
static int a733_i2s0_tx_begin(void)
{
  a733_pb_pinmux(7,3);
  uint32_t ctl=I2S_CTL_BCLK_OUT|I2S_CTL_LRCK_OUT|I2S_CTL_SDO0_EN|
    I2S_CTL_MODE_I2S|I2S_CTL_TXEN|I2S_CTL_GLOBAL_EN;
  putreg32(ctl,A733_I2S0_BASE+I2S_CTL);
#ifdef __aarch64__
  __asm__ volatile("dsb sy" ::: "memory");
#endif
  g_i2s0.tx_active_cfg=getreg32(A733_PB_BASE);
  g_i2s0.tx_active_ctl=getreg32(A733_I2S0_BASE+I2S_CTL);
  g_i2s0.tx_active_fmt=getreg32(A733_I2S0_BASE+I2S_FMT0);
  g_i2s0.tx_active_div=getreg32(A733_I2S0_BASE+I2S_CLKDIV);
  g_i2s0.tx_active_clock=getreg32(A733_I2S0_CLK);
  int r=((g_i2s0.tx_active_cfg&0xfff00000)!=0x33300000 ||
    g_i2s0.tx_active_ctl!=ctl || g_i2s0.tx_active_clock!=0x82000018 ||
    g_i2s0.tx_active_div!=0x50 || !(getreg32(A733_PLL_AUDIO1)&PLL_AUDIO1_LOCK) ||
    (getreg32(A733_I2S0_BGR)&0x10001)!=0x10001)?-EIO:0;
  g_i2s0.tx_pin_status=r;
  if(r)a733_i2s0_stream_stop();
  return r;
}

static int a733_i2s0_tone(unsigned int frequency, unsigned int duration_ms,
                          bool fifo16)
{
  static const int16_t sine32[32] =
    {
          0,  3196,  6269,  9102, 11585, 13622, 15136, 16069,
      16384, 16069, 15136, 13622, 11585,  9102,  6269,  3196,
          0, -3196, -6269, -9102,-11585,-13622,-15136,-16069,
     -16384,-16069,-15136,-13622,-11585, -9102, -6269, -3196
    };
  uint64_t phase = 0;
  uint64_t step;
  uint32_t frames;
  uint32_t i;
  int ret = OK;

  if (frequency < 100 || frequency > 10000 ||
      duration_ms == 0 || duration_ms > 10000)
    {
      return -ERANGE;
    }

  ret = a733_i2s0_hw_initialize();
  if (ret < 0)
    {
      return ret;
    }

  ret=a733_i2s0_tx_prepare(fifo16);if(ret<0)return ret;
  strlcpy(g_i2s0.stage, "tone", sizeof(g_i2s0.stage));
  putreg32(I2S_FIFOCTL_FTX | I2S_FIFOCTL_FRX |
           I2S_FIFOCTL_TXIM_1 | I2S_FIFOCTL_RXOM_1,
           A733_I2S0_BASE + I2S_FIFOCTL);
  putreg32(0, A733_I2S0_BASE + I2S_TXCNT);

  frames = (A733_AUDIO_RATE * duration_ms) / 1000u;
  step = ((uint64_t)frequency << 32) / A733_AUDIO_RATE;
  for (i = 0; i < frames; i++)
    {
      int16_t sample = sine32[((uint32_t)phase) >> 27];

      ret = a733_i2s0_wait_tx_space();
      if (ret < 0)
        {
          break;
        }

      if (fifo16)
        {
          /* Match the vendor Linux S16_LE path: sample resolution 16,
           * TXIM=1 and half-word FIFO writes.  A 32-bit MMIO write with the
           * sample only in bits 31:16 can otherwise feed zeros to the FIFO.
           */

          putreg16((uint16_t)sample, A733_I2S0_BASE + I2S_TXFIFO);
          putreg16((uint16_t)sample, A733_I2S0_BASE + I2S_TXFIFO);
        }
      else
        {
          uint32_t sample32 = (uint32_t)(uint16_t)sample << 16;

          putreg32(sample32, A733_I2S0_BASE + I2S_TXFIFO);
          putreg32(sample32, A733_I2S0_BASE + I2S_TXFIFO);
        }
      phase += step;
      a733_i2s0_tx_sample(sample);
      if(i==63 || (i+1==frames && frames<64))
        {ret=a733_i2s0_tx_begin();if(ret<0)break;}
    }

  up_mdelay(2);
  g_i2s0.tx_count = getreg32(A733_I2S0_BASE + I2S_TXCNT);
  a733_i2s0_stream_stop();
  g_i2s0.samples = i;
  if (ret >= 0)
    {
      g_i2s0.tones++;
    }

  g_i2s0.last_status = ret;
  a733_i2s0_probe();
  return ret;
}

static void a733_put_le16(uint8_t *dest, uint16_t value)
{
  dest[0] = value & 0xff;
  dest[1] = value >> 8;
}

static void a733_put_le32(uint8_t *dest, uint32_t value)
{
  dest[0] = value & 0xff;
  dest[1] = (value >> 8) & 0xff;
  dest[2] = (value >> 16) & 0xff;
  dest[3] = value >> 24;
}

static int a733_file_write_all(struct file *filep, const void *buffer,
                               size_t length)
{
  const uint8_t *cursor = buffer;

  while (length > 0)
    {
      ssize_t written = file_write(filep, cursor, length);
      if (written < 0)
        {
          return (int)written;
        }

      if (written == 0)
        {
          return -EIO;
        }

      cursor += written;
      length -= written;
    }

  return OK;
}

static void a733_i2s0_capture_stats(const void *buffer, uint32_t count,
                                    bool raw32)
{
  const uint32_t *raw_samples = buffer;
  const int16_t *samples = buffer;
  int16_t minimum = INT16_MAX;
  int16_t maximum = INT16_MIN;
  int32_t previous = 0;
  uint32_t i;

  g_i2s0.capture_nonzero = 0;
  g_i2s0.capture_sign_transitions = 0;
  g_i2s0.capture_sum_square = 0;
  for (i = 0; i < count; i++)
    {
      int32_t sample = raw32 ? (int16_t)((int32_t)raw_samples[i] >> 8) :
                              samples[i];

      g_i2s0.capture_nonzero += sample != 0;
      if (sample < minimum)
        {
          minimum = sample;
        }

      if (sample > maximum)
        {
          maximum = sample;
        }

      g_i2s0.capture_sum_square += (uint64_t)((int64_t)sample * sample);
      if (i > 0)
        {
          g_i2s0.capture_sign_transitions += (sample < 0) != (previous < 0);
        }

      previous = sample;
    }

  g_i2s0.capture_zero = count - g_i2s0.capture_nonzero;
  g_i2s0.capture_min = count > 0 ? minimum : 0;
  g_i2s0.capture_max = count > 0 ? maximum : 0;
}

static void a733_i2s0_configure_rx(bool ics, unsigned int slot)
{
  uint32_t value = getreg32(A733_I2S0_BASE + I2S_CHCFG);

  /* Match the sun60iw2 Linux BSP: channels-1 in CHCFG[7:4] AND
   * RXCHSEL[19:16].  RXCHMAP entries are BYTES (slot[3:0], SDI[5:4]),
   * not TX's nibbles: FIFO0=SDI0/slot0, FIFO1=SDI0/slot1 => 0x00000100.
   * Keep legacy mono programming for old INMP441 diagnostics only.
   */

  value &= ~(15u << 4);
  value |= ics ? (1u << 4) : 0;
  putreg32(value, A733_I2S0_BASE + I2S_CHCFG);
  putreg32((1u << 20) | (ics ? (1u << 16) : 0),
           A733_I2S0_BASE + I2S_RXCHSEL);
  putreg32(0, A733_I2S0_BASE + I2S_RXCHMAP0);
  putreg32(0, A733_I2S0_BASE + I2S_RXCHMAP1);
  putreg32(0, A733_I2S0_BASE + I2S_RXCHMAP2);
  putreg32(ics ? 0x00000100u : slot,
           A733_I2S0_BASE + I2S_RXCHMAP3);
}

static int a733_i2s0_check_rx_counter(uint32_t words, uint32_t hardware,
                                      unsigned int channels)
{
  uint64_t counted_words;

  if ((channels != 1 && channels != 2) || words % channels != 0)
    {
      return -EINVAL;
    }

  /* A7Z v146 hardware produced two FIFO words per WS frame, while RXCNT
   * advanced at 48 kHz (244801 at 5100 ms, not 489601). Keep RAW RXCNT and
   * software FIFO words separate. This frame-unit policy is a silicon-
   * observed candidate, not an unambiguous statement from the TRM's
   * "sample" wording. Check intermediate points too; never accept either
   * scaling automatically. 64-bit arithmetic rejects wrapped huge counts.
   */

  counted_words = (uint64_t)hardware * channels;
  if (counted_words < words)
    {
      return -EIO;
    }

  if (counted_words > (uint64_t)words + 128u)
    {
      return -EOVERFLOW;
    }

  return OK;
}

static int a733_i2s0_record_rx_counter(uint32_t words, unsigned int channels)
{
  uint32_t hardware = getreg32(A733_I2S0_BASE + I2S_RXCNT);
  unsigned int index = g_i2s0.capture_counter_points;

  if (index > 0 && g_i2s0.capture_counter_words[index - 1] == words)
    {
      index--;
    }
  else if (index < 6)
    {
      g_i2s0.capture_counter_points++;
    }
  else
    {
      index = 5; /* Preserve early checkpoints and refresh the final one. */
    }

  g_i2s0.capture_counter_words[index] = words;
  g_i2s0.capture_counter_hw[index] = hardware;
  g_i2s0.rx_count = hardware;
  g_i2s0.capture_counter_status =
    a733_i2s0_check_rx_counter(words, hardware, channels);
  return g_i2s0.capture_counter_status;
}

static void a733_i2s0_apply_gain(int16_t *samples, uint32_t count,
                                 unsigned int gain)
{
  uint32_t i;

  /* Only after RX is stopped and transport gates pass.  gain is bounded
   * to 1..16 by the caller, so the S16 product fits signed 32 bits.
   * This is hard saturation, not AGC or noise suppression.
   */

  for (i = 0; i < count; i++)
    {
      int32_t value = (int32_t)samples[i] * (int32_t)gain;

      if (value > INT16_MAX)
        {
          value = INT16_MAX;
          g_i2s0.capture_clipped++;
        }
      else if (value < INT16_MIN)
        {
          value = INT16_MIN;
          g_i2s0.capture_clipped++;
        }

      samples[i] = (int16_t)value;
    }
}

/* Continuous idle RX with bounded RAM and per-block hardware proof. All
 * callbacks are provisional until stop + final counter/status validation.
 * No media I/O, NPU or logging in this FIFO loop; no counter reset at 8s. */
static int a733_i2s0_monitor_check(uint64_t words,uint32_t count,uint32_t flags)
{
  int32_t ahead=(int32_t)(count-(uint32_t)(words/2u));
  if(ahead<0)return -EIO;
  if(ahead>64 || (flags&(I2S_INTSTA_RX_UNDERRUN|I2S_INTSTA_RX_OVERRUN)))
    return -EOVERFLOW;
  return 0;
}

static int a733_i2s0_monitor(struct a733_audio_monitor_s *in)
{
  int16_t pcm[480];
  uint64_t words=0,frames=0;
  const uint32_t warm=4800u*2u;
  unsigned used=0,peak=0;
    clock_t start,rate_start;
    uint64_t rate_words=0;
  int ret=a733_i2s0_hw_initialize();
  if(ret<0)return ret;
  a733_i2s0_stream_stop();
  a733_i2s0_set_sample_resolution(I2S_FMT0_RES_24);
  a733_i2s0_set_clock_polarity(0);a733_pb_pull(8,2);
  a733_i2s0_configure_rx(true,0);
  putreg32(I2S_FIFOCTL_FTX|I2S_FIFOCTL_FRX|I2S_FIFOCTL_TXIM_1|I2S_FIFOCTL_RXOM_1,
           A733_I2S0_BASE+I2S_FIFOCTL);
  putreg32(0,A733_I2S0_BASE+I2S_RXCNT);
  putreg32(I2S_INTSTA_RX_UNDERRUN|I2S_INTSTA_RX_OVERRUN,A733_I2S0_BASE+I2S_INTSTA);
  a733_i2s0_dout_quiet();
  strlcpy(g_i2s0.stage,"monitor-rx",sizeof(g_i2s0.stage));
  strlcpy(g_i2s0.last_operation,"monitor",sizeof(g_i2s0.last_operation));
    start=rate_start=clock_systime_ticks();
  putreg32(I2S_CTL_BCLK_OUT|I2S_CTL_LRCK_OUT|I2S_CTL_MODE_I2S|
           I2S_CTL_RXEN|I2S_CTL_GLOBAL_EN,A733_I2S0_BASE+I2S_CTL);
  for(;;)
    {
      uint32_t status=0;
      ret=a733_i2s0_wait_rx_data(&status);if(ret<0)break;
      unsigned available=I2S_FIFO_RX_COUNT(status);if(available>peak)peak=available;
      uint32_t raw=getreg32(A733_I2S0_BASE+I2S_RXFIFO);++words;
        if(words<=warm)
          {
            if(TICK2MSEC(clock_systime_ticks()-start)>250)
              {ret=-ETIMEDOUT;break;}
            continue;
          }
      if((words-warm)%2u) /* left wire slot */
        {
          int32_t v=((int32_t)raw>>8)*(int32_t)in->gain;
          if(v>INT16_MAX)v=INT16_MAX;else if(v<INT16_MIN)v=INT16_MIN;
          pcm[used++]=(int16_t)v;
        }
      else ++frames;
      if(used!=480 || (words%2u))continue;
        /* Rolling wall-clock proof, including the initial warm-up. Clock
         * ticks and RXCNT may wrap; neither limits normal idle duration. */
        if(words-rate_words>=96000)
          {
            clock_t tick=clock_systime_ticks();
            uint64_t ms=TICK2MSEC(tick-rate_start);
            if(ms<850 || ms>1150){ret=-EIO;break;}
            rate_start=tick;rate_words=words;
          }
      /* Modulo frame counter remains meaningful across 32-bit wrap. At
       * this <=10ms checkpoint the FIFO cannot legitimately lead by >64. */
        ret=a733_i2s0_monitor_check(words,getreg32(A733_I2S0_BASE+I2S_RXCNT),
                                  getreg32(A733_I2S0_BASE+I2S_INTSTA));
        if(ret)break;
      ret=in->consume(in->cookie,(const uint8_t *)pcm,sizeof(pcm));used=0;
      if(ret<0)break;
      if(ret==1){in->ended=1;ret=0;break;}
      if(ret!=0){ret=-EINVAL;break;}
    }
  g_i2s0.rx_count=getreg32(A733_I2S0_BASE+I2S_RXCNT);
  g_i2s0.capture_intsta=getreg32(A733_I2S0_BASE+I2S_INTSTA);
  a733_i2s0_stream_stop();
  if(!ret)
    {
        ret=a733_i2s0_monitor_check(words,g_i2s0.rx_count,g_i2s0.capture_intsta);
    }
  in->frames=frames;g_i2s0.capture_fifo_peak=peak;
  g_i2s0.last_capture_status=ret;g_i2s0.last_capture_committed=false;
  strlcpy(g_i2s0.stage,ret<0?"monitor-failed":"monitor-stopped",sizeof(g_i2s0.stage));
  volatile int16_t *wipe=pcm;for(unsigned i=0;i<480;i++)wipe[i]=0;
  return ret;
}

static int a733_i2s0_capture_into(unsigned int duration_ms, bool raw32,
                               unsigned int slot, unsigned int bclk,
                               unsigned int pull, bool matrix, bool ics,
                               bool voice, unsigned int gain,
                               struct a733_audio_capture_s *output,
                               struct a733_audio_live_s *live)
{
  struct file wav;
  void *capture_buffer;
  int16_t *samples = NULL;
  uint32_t *raw_samples = NULL;
  uint8_t header[44];
  uint32_t frames;
  uint32_t received = 0;
  uint32_t rx_channels = ics ? 2u : 1u;
  uint32_t file_channels = slot == 2 ? 2u : 1u;
  uint32_t produced = 0;
  uint32_t raw_nonzero = 0;
  uint32_t warmup_frames;
  uint32_t warmup_done = 0;
  uint32_t first_raw = 0;
  uint32_t last_raw = 0;
  uint32_t fifo_peak = 0;
  uint32_t raw55 = 0;
  uint32_t rawaa = 0;
  uint32_t intsta;
  uint32_t expected_rx;
  uint32_t elapsed_ms;
  uint32_t elapsed_limit_ms;
  clock_t capture_start;
  clock_t capture_end;
  char capture_path[96];
  char temporary_path[104];
  size_t capture_bytes;
  int close_ret;
  int ret;
  bool live_end=false;
  int16_t live_pcm[480];

  strlcpy(g_i2s0.last_operation,
          voice ? "icsvoice" : (ics ? "ics43434" :
          (matrix ? "capturecfg" : (raw32 ? "capture32" : "capture"))),
          sizeof(g_i2s0.last_operation));
  strlcpy(g_i2s0.last_capture_mode, raw32 ? "raw32" : "pcm16",
          sizeof(g_i2s0.last_capture_mode));
  g_i2s0.last_capture_status = -EINPROGRESS;
  g_i2s0.last_capture_committed = false;
  g_i2s0.capture_requested_ms = duration_ms;
  g_i2s0.capture_elapsed_ms = 0;
  g_i2s0.capture_target_rx = 0;
  g_i2s0.capture_consumed_rx = 0;
  g_i2s0.capture_fifo_peak = 0;
  g_i2s0.capture_intsta = 0;
  g_i2s0.capture_slot = slot;
  g_i2s0.capture_bclk = bclk;
  g_i2s0.capture_pull = pull;
  g_i2s0.capture_fmt0 = 0;
  g_i2s0.capture_rxmap = 0;
  g_i2s0.capture_pb_pull = 0;
  g_i2s0.capture_ics = ics;
  g_i2s0.capture_rx_channels = rx_channels;
  g_i2s0.capture_file_channels = file_channels;
  g_i2s0.capture_chcfg = 0;
  g_i2s0.capture_rxchsel = 0;
  g_i2s0.capture_expected_counter = 0;
  g_i2s0.capture_counter_status = 0;
  g_i2s0.capture_counter_points = 0;
  memset(g_i2s0.capture_counter_words, 0,
         sizeof(g_i2s0.capture_counter_words));
  memset(g_i2s0.capture_counter_hw, 0, sizeof(g_i2s0.capture_counter_hw));
  g_i2s0.samples = 0;
  g_i2s0.rx_count = 0;
  g_i2s0.capture_nonzero = 0;
  g_i2s0.capture_raw_nonzero = 0;
  g_i2s0.capture_warmup = 0;
  g_i2s0.capture_first_raw = 0;
  g_i2s0.capture_last_raw = 0;
  g_i2s0.capture_min = 0;
  g_i2s0.capture_max = 0;
  g_i2s0.capture_zero = 0;
  g_i2s0.capture_sign_transitions = 0;
  g_i2s0.capture_raw55 = 0;
  g_i2s0.capture_rawaa = 0;
  g_i2s0.capture_sum_square = 0;
  g_i2s0.capture_voice = voice;
  g_i2s0.capture_processed = false;
  g_i2s0.capture_gain = gain;
  g_i2s0.capture_clipped = 0;
  g_i2s0.capture_input_min = 0;
  g_i2s0.capture_input_max = 0;
  g_i2s0.last_capture_path[0] = '\0';

  if (duration_ms < 100 || duration_ms > 10000 || slot > (ics ? 2u : 1u) ||
      bclk > 1 || (pull != 0 && pull != 2) || (ics && matrix) ||
      (ics && raw32 && slot != 2) || gain < 1 || gain > 16 ||
      (voice && (!ics || raw32 || slot != 0)) || (!voice && gain != 1))
    {
      g_i2s0.last_capture_status = -ERANGE;
      return -ERANGE;
    }

  ret = a733_i2s0_hw_initialize();
  if (ret < 0)
    {
      g_i2s0.last_capture_status = ret;
      return ret;
    }

  frames = (A733_AUDIO_RATE * duration_ms) / 1000u;
  if (ics)
    {
      if (voice)
        {
          strlcpy(capture_path, "/data/ics43434-voice.wav",
                  sizeof(capture_path));
        }
      else if (raw32)
        {
          strlcpy(capture_path, A733_ICS_RAW_PATH, sizeof(capture_path));
        }
      else
        {
          snprintf(capture_path, sizeof(capture_path),
                   "/data/ics43434-%s.wav",
                   slot == 2 ? "stereo" : (slot == 0 ? "left" : "right"));
        }
      snprintf(temporary_path, sizeof(temporary_path), "%s.tmp",
               capture_path);
    }
  else if (matrix)
    {
      snprintf(capture_path, sizeof(capture_path),
               "/data/inmp441-%s-%s-%s.wav",
               slot == 0 ? "left" : "right",
               bclk == 0 ? "normal" : "invert",
               pull == 2 ? "pd" : "none");
      snprintf(temporary_path, sizeof(temporary_path), "%s.tmp",
               capture_path);
    }
  else
    {
      strlcpy(capture_path,
              raw32 ? A733_CAPTURE_RAW_PATH : A733_CAPTURE_PATH,
              sizeof(capture_path));
      strlcpy(temporary_path,
              raw32 ? A733_CAPTURE_RAW_TMP_PATH : A733_CAPTURE_TMP_PATH,
              sizeof(temporary_path));
    }

  strlcpy(g_i2s0.last_capture_path, capture_path,
          sizeof(g_i2s0.last_capture_path));
  if (output != NULL || live != NULL)
    {
      strlcpy(g_i2s0.last_capture_path, "RAM (no file)",
              sizeof(g_i2s0.last_capture_path));
    }
  capture_bytes = (size_t)frames * file_channels *
                                        (raw32 ? sizeof(uint32_t) :
                                             sizeof(int16_t));
  capture_buffer = kmm_malloc(capture_bytes);
  if (capture_buffer == NULL)
    {
      g_i2s0.last_capture_status = -ENOMEM;
      return -ENOMEM;
    }

  if (raw32)
    {
      raw_samples = capture_buffer;
    }
  else
    {
      samples = capture_buffer;
    }

  strlcpy(g_i2s0.stage, "capture", sizeof(g_i2s0.stage));
  /* Both microphones supply a 24-bit two's-complement word in a 32-bit I2S
   * slot.  This matches the vendor Linux S24_3LE hardware programming:
   * sample resolution 24, slot width 32, RXOM=1.  RXOM=1 sign-extends the
   * received 24-bit sample into a right-aligned 32-bit APB word.
   */

  a733_i2s0_set_sample_resolution(I2S_FMT0_RES_24);
  a733_i2s0_set_clock_polarity(bclk);
  a733_pb_pull(8, pull);

  /* ICS43434 captures BOTH wire slots before optional software selection.
   * No assumption is made about the module's unexposed LR strap.  Silence
   * or a constant on one channel is not evidence of a working microphone.
   */

  a733_i2s0_configure_rx(ics, slot);
  g_i2s0.capture_fmt0 = getreg32(A733_I2S0_BASE + I2S_FMT0);
  g_i2s0.capture_rxmap = getreg32(A733_I2S0_BASE + I2S_RXCHMAP3);
  g_i2s0.capture_pb_pull = getreg32(A733_PB_BASE + 0x30);
  g_i2s0.capture_chcfg = getreg32(A733_I2S0_BASE + I2S_CHCFG);
  g_i2s0.capture_rxchsel = getreg32(A733_I2S0_BASE + I2S_RXCHSEL);
  putreg32(I2S_FIFOCTL_FTX | I2S_FIFOCTL_FRX |
           I2S_FIFOCTL_TXIM_1 | I2S_FIFOCTL_RXOM_1,
           A733_I2S0_BASE + I2S_FIFOCTL);
  putreg32(0, A733_I2S0_BASE + I2S_RXCNT);
  putreg32(I2S_INTSTA_RX_UNDERRUN | I2S_INTSTA_RX_OVERRUN,
           A733_I2S0_BASE + I2S_INTSTA);
  a733_i2s0_dout_quiet();
  putreg32(I2S_CTL_BCLK_OUT | I2S_CTL_LRCK_OUT |
           I2S_CTL_MODE_I2S | I2S_CTL_RXEN | I2S_CTL_GLOBAL_EN,
           A733_I2S0_BASE + I2S_CTL);
  capture_start = clock_systime_ticks();
  elapsed_limit_ms = duration_ms + A733_INMP441_WARMUP_MS +
                     duration_ms / 20u + 100u;

  /* INMP441 guarantees valid data only after 2^18 SCK edges.  At
   * 3.072 MHz that is about 85 ms, so discard 100 ms before measuring or
   * writing the requested capture.  ICS43434 specifies <=20 ms; retain the
   * conservative 100 ms here.  With ICS, each frame produces TWO FIFO words.
   */

  warmup_frames = (A733_AUDIO_RATE * A733_INMP441_WARMUP_MS) / 1000u *
                  rx_channels;
  g_i2s0.capture_target_rx = warmup_frames + frames * rx_channels;
  g_i2s0.capture_expected_counter = g_i2s0.capture_target_rx / rx_channels;
  while (warmup_done < warmup_frames)
    {
      uint32_t fifosta;
      uint32_t available;

      if ((warmup_done & 255u) == 0 &&
          TICK2MSEC(clock_systime_ticks() - capture_start) > elapsed_limit_ms)
        {
          ret = -ETIMEDOUT;
          break;
        }

      ret = a733_i2s0_wait_rx_data(&fifosta);
      if (ret < 0)
        {
          break;
        }

      available = I2S_FIFO_RX_COUNT(fifosta);
      if (available == 0)
        {
          /* RXA says at least one word is readable.  The seven-bit count
           * field cannot be extended with bit 7: bit 7 is a fixed-one
           * PLACE_HOLDER, not the 128s bit.
           */

          available = 1;
        }

      if (available > fifo_peak)
        {
          fifo_peak = available;
        }

      (void)getreg32(A733_I2S0_BASE + I2S_RXFIFO);
      warmup_done++;
    }

  if (ret >= 0)
    {
      ret = a733_i2s0_record_rx_counter(warmup_done, rx_channels);
    }

  while (ret >= 0 && received < frames * rx_channels)
    {
      uint32_t left;
      uint32_t fifosta;
      uint32_t available;

      /* A word-by-word timeout alone could take minutes on a severely
       * misclocked source. Bound total capture time as well, while avoiding
       * a system-clock read for every 96 kHz FIFO word in stereo mode.
       */

      if ((received & 255u) == 0 &&
          TICK2MSEC(clock_systime_ticks() - capture_start) > elapsed_limit_ms)
        {
          ret = -ETIMEDOUT;
          break;
        }

      ret = a733_i2s0_wait_rx_data(&fifosta);
      if (ret < 0)
        {
          break;
        }

      available = I2S_FIFO_RX_COUNT(fifosta);
      if (available == 0)
        {
          available = 1;
        }

      if (available > fifo_peak)
        {
          fifo_peak = available;
        }

      left = getreg32(A733_I2S0_BASE + I2S_RXFIFO);
      received++;
      if (received % (A733_AUDIO_RATE * rx_channels) == 0)
        {
          ret = a733_i2s0_record_rx_counter(warmup_done + received,
                                           rx_channels);
          if (ret < 0)
            {
              break;
            }
        }

      if (ics && slot < 2 && ((received - 1u) % 2u) != slot)
        {
          /* Complete the unused right wire word before endpoint stop so the
           * existing RX counter/geometry proof remains meaningful. */
          if(live_end)
            {
              frames=produced;
              duration_ms=produced*1000u/A733_AUDIO_RATE;
              elapsed_limit_ms=duration_ms+A733_INMP441_WARMUP_MS+
                               duration_ms/20u+100u;
              g_i2s0.capture_target_rx=warmup_frames+frames*rx_channels;
              g_i2s0.capture_expected_counter=g_i2s0.capture_target_rx/rx_channels;
              break;
            }
          continue;
        }

      if (produced == 0)
        {
          first_raw = left;
        }

      last_raw = left;
      raw55 += (left & 0xffu) == 0x55u;
      rawaa += (left & 0xffu) == 0xaau;
      if (left != 0)
        {
          raw_nonzero++;
        }

      if (raw32)
        {
          raw_samples[produced] = left;
          produced++;
          continue;
        }

      /* With SR=24 and RXOM=1, A733 returns the sign-extended 24-bit sample
       * right-aligned in the APB word.  Arithmetic >> 8 converts S24 to
       * S16 without destroying the sign or selecting the tri-stated slot.
       */

      samples[produced] = (int16_t)((int32_t)left >> 8);
      produced++;
      if(live && produced%480u==0)
        {
          for(unsigned int n=0;n<480;n++)
            {
              int32_t v=(int32_t)samples[produced-480u+n]*(int32_t)gain;
              if(v>INT16_MAX)v=INT16_MAX;else if(v<INT16_MIN)v=INT16_MIN;
              live_pcm[n]=(int16_t)v;
            }
          int accepted=live->consume(live->cookie,(const uint8_t *)live_pcm,sizeof(live_pcm));
          if(accepted<0){ret=accepted;break;}
          if(accepted>1){ret=-EINVAL;break;}
          if(accepted==1)live_end=true;
        }
    }

  g_i2s0.rx_count = getreg32(A733_I2S0_BASE + I2S_RXCNT);
  intsta = getreg32(A733_I2S0_BASE + I2S_INTSTA);
  capture_end = clock_systime_ticks();
  g_i2s0.capture_raw_nonzero = raw_nonzero;
  g_i2s0.capture_warmup = warmup_done;
  g_i2s0.capture_first_raw = first_raw;
  g_i2s0.capture_last_raw = last_raw;
  g_i2s0.capture_requested_ms = duration_ms;
  elapsed_ms = TICK2MSEC(capture_end - capture_start);
  g_i2s0.capture_elapsed_ms = elapsed_ms;
  expected_rx = warmup_done + received;
  g_i2s0.capture_consumed_rx = expected_rx;
  g_i2s0.capture_fifo_peak = fifo_peak;
  g_i2s0.capture_intsta = intsta;
  a733_i2s0_stream_stop();
  g_i2s0.samples = produced;

  /* Compute acoustic diagnostics after stopping RX, outside the proven
   * FIFO pacing loop.  All PCM metrics use S16 units, including raw32
   * captures.  Sign transitions compare adjacent sample signs (zero is
   * nonnegative).  Sum-square is 64-bit: up to 480000 * 32768^2.
   */

  a733_i2s0_capture_stats(capture_buffer, produced, raw32);
  g_i2s0.capture_input_min = g_i2s0.capture_min;
  g_i2s0.capture_input_max = g_i2s0.capture_max;
  g_i2s0.capture_raw55 = raw55;
  g_i2s0.capture_rawaa = rawaa;

  /* Do not silently create another correctly-sized but fake WAV. Track
   * software FIFO words, the raw engine counter and elapsed time separately.
   * Every frame must supply rx_channels words, including an unselected mic
   * slot. A short wall time, missing data, FIFO error or unit mismatch fails.
   */

  if (ret >= 0 && (produced != frames * file_channels ||
                   received != frames * rx_channels))
    {
      ret = -EIO;
    }

  if (ret >= 0)
    {
      ret = a733_i2s0_record_rx_counter(expected_rx, rx_channels);
    }

  if (ret >= 0 &&
      elapsed_ms + 20u < duration_ms + A733_INMP441_WARMUP_MS)
    {
      ret = -EIO;
    }

  /* A correct 48 kHz stream should finish very close to the requested
   * duration plus microphone warm-up.  Allow 5 percent plus 100 ms for
   * scheduler/tick granularity; a slower result means the programmed
   * sample clock cannot safely match the 48 kHz WAV header.
   */

  if (ret >= 0 && elapsed_ms > elapsed_limit_ms)
    {
      ret = -ETIMEDOUT;
    }

  if (ret >= 0 &&
      (intsta & (I2S_INTSTA_RX_UNDERRUN | I2S_INTSTA_RX_OVERRUN)) != 0)
    {
      ret = -EOVERFLOW;
    }

  if (ret >= 0 && voice)
    {
      /* Keep the proven FIFO loop, S24 conversion and hardware configuration
       * unchanged.  Select the left slot as before; never average a silent
       * right slot into the useful signal.  Processing cannot hide RX errors.
       */

      a733_i2s0_apply_gain(samples, produced, gain);
      a733_i2s0_capture_stats(samples, produced, false);
      g_i2s0.capture_processed = true;
    }

  /* Capture into RAM first.  Writing 512-byte chunks to the SD-backed
   * /data filesystem while RX is live can stall longer than the 128-word
   * FIFO (2.67 ms at 48 kHz) and cause deterministic overruns.
   */

  if(ret>=0 && live!=NULL)
    {
      if(produced%480u)
        {
          unsigned int tail=produced%480u;
          /* RX is stopped and gain has already been applied above. */
          int accepted=live->consume(live->cookie,(const uint8_t *)(samples+produced-tail),tail*2u);
          if(accepted<0)ret=accepted;else if(accepted>1)ret=-EINVAL;
          else if(accepted==1)live_end=true;
        }
      if(ret>=0){live->frames=produced;live->ended=live_end;}
    }
  else if (ret >= 0 && output != NULL)
    {
      memcpy(output->pcm, capture_buffer, (size_t)produced * 2u);
      output->bytes = produced * 2u;
    }

  if (ret >= 0 && output == NULL && live==NULL)
    {
      /* Commit with a same-filesystem rename only after all bytes and the
       * close/flush succeed.  A failed capture must never replace the last
       * known-good file with a partial or correctly-sized fake recording.
       */

      (void)nx_unlink(temporary_path);
      memset(&wav, 0, sizeof(wav));
      ret = file_open(&wav, temporary_path,
                      O_WRONLY | O_CREAT | O_TRUNC, 0666);
      if (ret >= 0)
        {
          if (!raw32)
            {
              memset(header, 0, sizeof(header));
              memcpy(header, "RIFF", 4);
              a733_put_le32(header + 4, 36u + produced * 2u);
              memcpy(header + 8, "WAVEfmt ", 8);
              a733_put_le32(header + 16, 16);
              a733_put_le16(header + 20, 1);
               a733_put_le16(header + 22, file_channels);
              a733_put_le32(header + 24, A733_AUDIO_RATE);
               a733_put_le32(header + 28,
                             A733_AUDIO_RATE * 2u * file_channels);
               a733_put_le16(header + 32, 2u * file_channels);
              a733_put_le16(header + 34, 16);
              memcpy(header + 36, "data", 4);
              a733_put_le32(header + 40, produced * 2u);
              ret = a733_file_write_all(&wav, header, sizeof(header));
            }

          if (ret >= 0)
            {
              ret = a733_file_write_all(&wav, capture_buffer,
                                        (size_t)produced *
                                        (raw32 ? sizeof(uint32_t) :
                                                 sizeof(int16_t)));
            }

          close_ret = file_close(&wav);
          if (ret >= 0 && close_ret < 0)
            {
              ret = close_ret;
            }

          if (ret >= 0 && rename(temporary_path, capture_path) < 0)
            {
              ret = -get_errno();
            }
          else if (ret >= 0)
            {
              g_i2s0.last_capture_committed = true;
            }
        }

      if (ret < 0)
        {
          (void)nx_unlink(temporary_path);
        }
    }

  kmm_free(capture_buffer);
  if (ret >= 0)
    {
      g_i2s0.captures++;
    }

  g_i2s0.last_status = ret;
  g_i2s0.last_capture_status = ret;
  a733_i2s0_probe();
  return ret;
}

static int a733_i2s0_capture_ex(unsigned int duration_ms, bool raw32,
                               unsigned int slot, unsigned int bclk,
                               unsigned int pull, bool matrix, bool ics,
                               bool voice, unsigned int gain)
{
  return a733_i2s0_capture_into(duration_ms, raw32, slot, bclk, pull,
                               matrix, ics, voice, gain, NULL, NULL);
}

static int a733_i2s0_capture(unsigned int duration_ms, bool raw32,
                            unsigned int slot, unsigned int bclk,
                            unsigned int pull, bool matrix, bool ics)
{
  /* Legacy/diagnostic captures remain unamplified and retain their paths. */

  return a733_i2s0_capture_ex(duration_ms, raw32, slot, bclk, pull, matrix,
                             ics, false, 1);
}

#include "a733_i2s0_playback.inc"

static ssize_t a733_i2s0_read(struct file *filep, char *buffer,
                              size_t buflen)
{
  /* Entire formatting/copy operation is protected by g_i2s0.lock below.
   * Avoid placing this diagnostic buffer plus varargs on the caller stack.
   */

  static char report[4096];
  size_t length;
  size_t offset;
  int ret;

  if (buffer == NULL)
    {
      return -EINVAL;
    }

    ret = nxmutex_trylock(&g_i2s0.lock);
  if (ret < 0)
    {
      return ret;
    }

  a733_i2s0_probe();
  length = (size_t)snprintf(report, sizeof(report),
    "A733 I2S0 MAX98357A/INMP441/ICS43434 acceptance driver v212\n"
    "tx-proof-v212: status=%d samples=%lu nonzero=%lu peak=%lu active-pb=%08lx active-ctl=%08lx fmt=%08lx clock=%08lx div=%08lx; electrical/acoustic output NOT measured\n"
    "playback: status=%d plays=%u volume=%u%% rate=%lu channels=%lu "
    "frames=%lu sent=%lu elapsed=%lu ms intsta=%08lx\n"
    "state=%s initialized=%u pll-locked=%u last=%d last-op=%s probes=%u "
    "tones=%u captures=%u timeouts=%u samples=%lu\n"
    "stream-proof: tx-count=%lu rx-count=%lu capture-nonzero=%lu "
    "raw-nonzero=%lu warmup=%lu first-raw=%08lx last-raw=%08lx "
    "capture-min=%d capture-max=%d\n"
    "capture-proof: mode=%s slot=%lu(%s) status=%d committed=%u requested=%lu ms "
    "elapsed=%lu ms target-rx=%lu consumed-rx=%lu hw-rx=%lu "
    "fifo-peak=%lu intsta=%08lx\n"
    "capture-config: bclk-polarity=%lu lrck-polarity=%lu "
    "PB8-pull=%lu(%s) fmt0=%08lx rxmap3=%08lx pb-pull=%08lx\n"
    "capture-stats: units=S16 zero=%lu sign-transitions=%lu "
    "sum-square=%llu raw-low55=%lu raw-lowaa=%lu\n"
    "capture-output: %s\n"
    "processing: voice=%u applied=%u gain=%lu clipped=%lu input-min=%d "
    "input-max=%d; capture-stats/min/max describe output\n"
    "capture-profile: mic=%s rx-channels=%lu file-channels=%lu "
    "chcfg=%08lx rxchsel=%08lx; transport status is NOT acoustic acceptance\n"
    "counter-policy=frame-candidate expected-hw=%lu actual-hw=%lu "
    "counter-status=%d points=%lu FIFO-words/HW="
    "%lu/%lu,%lu/%lu,%lu/%lu,%lu/%lu,%lu/%lu,%lu/%lu\n"
    "capture-plan: rate=48000 slots=2 sample=24 slot=32 module=24.576MHz "
    "bclk=3.072MHz mclk=off\n"
    "ccu: pll-audio1=%08lx module=%08lx bus-reset=%08lx\n"
    "regs: ctl=%08lx fmt0=%08lx fifoctl=%08lx fifosta=%08lx "
    "clkdiv=%08lx chcfg=%08lx rev=%08lx\n"
    "channel-map: txsel=%08lx txmap=%08lx/%08lx "
    "rxsel=%08lx rxmap=%08lx/%08lx/%08lx/%08lx\n"
    "pins: PB5/BCLK=pin12 PB6/LRCK=pin35 PB7/DOUT0=pin40 "
    "PB8/DIN0=pin38; PB5/6/8 mux=3 PB7=GPIO-low idle/RX, I2S TX; cfg0=%08lx cfg1=%08lx pull=%08lx data=%08lx\n"
    "max98357a: BCLK=PB5 LRC=PB6 DIN=PB7; SD_MODE must be high; "
    "OUTP/OUTN are differential\n"
    "inmp441: SCK=PB5 WS=PB6 SD=PB8 VDD=3.3V; default-slot=right(1)\n"
    "ics43434: SCK=PB5 WS=PB6 SD=PB8; dual-slot, no LR strap assumed; "
    "bare VDD=1.65..3.63V; combined-module VCC per vendor, NOT bare VDD\n"
    "ics: icscapture=<ms> (stereo) | icsleft=<ms> | icsright=<ms> | "
    "icsraw=<ms> (stereo S24 in S32); 100..10000 ms\n"
    "voice: icsvoice=<ms>[,<gain:1..16>] (left mono S16, default x5; "
    "/data/ics43434-voice.wav; no AGC)\n"
    "speaker: play=/data/file.wav | volume=0..100 (default 25%%); PCM16 WAV, max10s\n"
    "control: probe | init | tone=<hz>,<ms> (official S16 path) | "
    "tone32=<hz>,<ms> (legacy compare) | capture=<ms> (default right) | "
    "captureleft=<ms> | captureright=<ms> | capture32=<ms> | off\n"
    "matrix: capturecfg=<slot:0/1>,<bclk:0/1>,<pull:0/2>,<ms:100..10000>; "
    "each config has its own WAV; repeating it replaces that config only\n"
    "capture-files: %s (48kHz mono signed-16 WAV), %s "
    "(sign-extended S24 FIFO words)\n",
    g_i2s0.tx_pin_status,(unsigned long)g_i2s0.tx_samples,
    (unsigned long)g_i2s0.tx_nonzero,(unsigned long)g_i2s0.tx_peak,
    (unsigned long)g_i2s0.tx_active_cfg,(unsigned long)g_i2s0.tx_active_ctl,
    (unsigned long)g_i2s0.tx_active_fmt,(unsigned long)g_i2s0.tx_active_clock,
    (unsigned long)g_i2s0.tx_active_div,
    g_i2s0.play_status, g_i2s0.plays, g_i2s0.playback_volume,
    (unsigned long)g_i2s0.play_rate, (unsigned long)g_i2s0.play_channels,
    (unsigned long)g_i2s0.play_frames, (unsigned long)g_i2s0.play_sent,
    (unsigned long)g_i2s0.play_elapsed, (unsigned long)g_i2s0.play_intsta,
    g_i2s0.stage, g_i2s0.initialized, g_i2s0.pll_locked,
    g_i2s0.last_status, g_i2s0.last_operation, g_i2s0.probes, g_i2s0.tones,
    g_i2s0.captures, g_i2s0.timeouts, (unsigned long)g_i2s0.samples,
    (unsigned long)g_i2s0.tx_count, (unsigned long)g_i2s0.rx_count,
    (unsigned long)g_i2s0.capture_nonzero,
    (unsigned long)g_i2s0.capture_raw_nonzero,
    (unsigned long)g_i2s0.capture_warmup,
    (unsigned long)g_i2s0.capture_first_raw,
    (unsigned long)g_i2s0.capture_last_raw, g_i2s0.capture_min,
    g_i2s0.capture_max,
    g_i2s0.last_capture_mode, (unsigned long)g_i2s0.capture_slot,
    g_i2s0.capture_slot == 2 ? "stereo" :
    (g_i2s0.capture_slot == 0 ? "left" : "right"),
    g_i2s0.last_capture_status,
    g_i2s0.last_capture_committed,
    (unsigned long)g_i2s0.capture_requested_ms,
    (unsigned long)g_i2s0.capture_elapsed_ms,
    (unsigned long)g_i2s0.capture_target_rx,
    (unsigned long)g_i2s0.capture_consumed_rx,
    (unsigned long)g_i2s0.rx_count,
    (unsigned long)g_i2s0.capture_fifo_peak,
    (unsigned long)g_i2s0.capture_intsta,
    (unsigned long)g_i2s0.capture_bclk,
    (unsigned long)((g_i2s0.capture_fmt0 & I2S_FMT0_LRCK_POLARITY) != 0),
    (unsigned long)g_i2s0.capture_pull,
    g_i2s0.capture_pull == 2 ? "down" : "disabled",
    (unsigned long)g_i2s0.capture_fmt0,
    (unsigned long)g_i2s0.capture_rxmap,
    (unsigned long)g_i2s0.capture_pb_pull,
    (unsigned long)g_i2s0.capture_zero,
    (unsigned long)g_i2s0.capture_sign_transitions,
    (unsigned long long)g_i2s0.capture_sum_square,
    (unsigned long)g_i2s0.capture_raw55,
    (unsigned long)g_i2s0.capture_rawaa,
    g_i2s0.last_capture_path,
    g_i2s0.capture_voice, g_i2s0.capture_processed,
    (unsigned long)g_i2s0.capture_gain,
    (unsigned long)g_i2s0.capture_clipped,
    g_i2s0.capture_input_min, g_i2s0.capture_input_max,
    g_i2s0.capture_ics ? "ICS43434" : "INMP441-legacy",
    (unsigned long)g_i2s0.capture_rx_channels,
    (unsigned long)g_i2s0.capture_file_channels,
    (unsigned long)g_i2s0.capture_chcfg,
    (unsigned long)g_i2s0.capture_rxchsel,
    (unsigned long)g_i2s0.capture_expected_counter,
    (unsigned long)g_i2s0.rx_count,
    g_i2s0.capture_counter_status,
    (unsigned long)g_i2s0.capture_counter_points,
    (unsigned long)g_i2s0.capture_counter_words[0],
    (unsigned long)g_i2s0.capture_counter_hw[0],
    (unsigned long)g_i2s0.capture_counter_words[1],
    (unsigned long)g_i2s0.capture_counter_hw[1],
    (unsigned long)g_i2s0.capture_counter_words[2],
    (unsigned long)g_i2s0.capture_counter_hw[2],
    (unsigned long)g_i2s0.capture_counter_words[3],
    (unsigned long)g_i2s0.capture_counter_hw[3],
    (unsigned long)g_i2s0.capture_counter_words[4],
    (unsigned long)g_i2s0.capture_counter_hw[4],
    (unsigned long)g_i2s0.capture_counter_words[5],
    (unsigned long)g_i2s0.capture_counter_hw[5],
    (unsigned long)g_i2s0.pll, (unsigned long)g_i2s0.clock,
    (unsigned long)g_i2s0.bus, (unsigned long)g_i2s0.ctl,
    (unsigned long)g_i2s0.fmt0, (unsigned long)g_i2s0.fifoctl,
    (unsigned long)g_i2s0.fifosta, (unsigned long)g_i2s0.clkdiv,
    (unsigned long)g_i2s0.chcfg, (unsigned long)g_i2s0.revision,
    (unsigned long)g_i2s0.txchsel, (unsigned long)g_i2s0.txmap0,
    (unsigned long)g_i2s0.txmap1, (unsigned long)g_i2s0.rxchsel,
    (unsigned long)g_i2s0.rxmap0, (unsigned long)g_i2s0.rxmap1,
    (unsigned long)g_i2s0.rxmap2, (unsigned long)g_i2s0.rxmap3,
    (unsigned long)g_i2s0.pb_cfg0, (unsigned long)g_i2s0.pb_cfg1,
    (unsigned long)g_i2s0.pb_pull, (unsigned long)g_i2s0.pb_data,
    A733_CAPTURE_PATH, A733_CAPTURE_RAW_PATH);

  if (length >= sizeof(report))
    {
      length = sizeof(report) - 1;
    }

  offset = (size_t)filep->f_pos;
  if (offset >= length)
    {
      nxmutex_unlock(&g_i2s0.lock);
      return 0;
    }

  if (buflen > length - offset)
    {
      buflen = length - offset;
    }

  memcpy(buffer, report + offset, buflen);
  filep->f_pos += (off_t)buflen;
  nxmutex_unlock(&g_i2s0.lock);
  return (ssize_t)buflen;
}

static int a733_parse_pair(const char *text, unsigned int *first,
                           unsigned int *second)
{
  char *end;
  unsigned long value;

  value = strtoul(text, &end, 10);
  if (end == text || *end != ',' || value > UINT_MAX)
    {
      return -EINVAL;
    }

  *first = (unsigned int)value;
  text = end + 1;
  value = strtoul(text, &end, 10);
  if (end == text || *end != '\0' || value > UINT_MAX)
    {
      return -EINVAL;
    }

  *second = (unsigned int)value;
  return OK;
}

static int a733_parse_duration(const char *text, unsigned int *duration);

static int a733_parse_voice(const char *text, unsigned int *duration,
                             unsigned int *gain)
{
  const char *comma = strchr(text, ',');
  char milliseconds[16];
  size_t length;
  unsigned int value = 0;
  int ret;

  *gain = 5;
  if (comma == NULL)
    {
      return a733_parse_duration(text, duration);
    }

  length = (size_t)(comma - text);
  if (length == 0 || length >= sizeof(milliseconds))
    {
      return -EINVAL;
    }

  memcpy(milliseconds, text, length);
  milliseconds[length] = '\0';
  ret = a733_parse_duration(milliseconds, duration);
  if (ret < 0)
    {
      return ret;
    }

  text = comma + 1;
  if (*text < '0' || *text > '9')
    {
      return -EINVAL;
    }

  while (*text >= '0' && *text <= '9')
    {
      value = value * 10u + (unsigned int)(*text++ - '0');
      if (value > 16)
        {
          return -ERANGE;
        }
    }

  if (*text != '\0' || value == 0)
    {
      return -EINVAL;
    }

  *gain = value;
  return OK;
}

static int a733_parse_capturecfg(const char *text, unsigned int values[4])
{
  unsigned int i;

  /* Strict decimal fields: reject signs, spaces, missing/extra fields,
   * trailing junk and overflow before any hardware or file operation.
   */

  for (i = 0; i < 4; i++)
    {
      unsigned int value = 0;

      if (*text < '0' || *text > '9')
        {
          return -EINVAL;
        }

      while (*text >= '0' && *text <= '9')
        {
          unsigned int digit = *text++ - '0';

          if (value > (UINT_MAX - digit) / 10u)
            {
              return -ERANGE;
            }

          value = value * 10u + digit;
        }

      values[i] = value;
      if (i < 3)
        {
          if (*text++ != ',')
            {
              return -EINVAL;
            }
        }
      else if (*text != '\0')
        {
          return -EINVAL;
        }
    }

  if (values[0] > 1 || values[1] > 1 ||
      (values[2] != 0 && values[2] != 2) ||
      values[3] < 100 || values[3] > 10000)
    {
      return -ERANGE;
    }

  return OK;
}

static int a733_parse_duration(const char *text, unsigned int *duration)
{
  unsigned int value = 0;

  if (*text < '0' || *text > '9')
    {
      return -EINVAL;
    }

  while (*text >= '0' && *text <= '9')
    {
      unsigned int digit = *text++ - '0';

      if (value > (UINT_MAX - digit) / 10u)
        {
          return -ERANGE;
        }

      value = value * 10u + digit;
    }

  if (*text != '\0')
    {
      return -EINVAL;
    }

  if (value < 100 || value > 10000)
    {
      return -ERANGE;
    }

  *duration = value;
  return OK;
}

static ssize_t a733_i2s0_write(struct file *filep, const char *buffer,
                               size_t buflen)
{
  char command[64];
  size_t length = buflen;
  unsigned int first;
  unsigned int second;
  int ret;

  (void)filep;
  if (buffer == NULL)
    {
      return -EINVAL;
    }

  /* Some NSH redirection paths finish with a zero-length write.  It is not
   * a second command and must not overwrite a successful operation with
   * -EINVAL.
   */

  if (buflen == 0)
    {
      return 0;
    }

  if (length >= sizeof(command))
    {
      return -E2BIG;
    }

  memcpy(command, buffer, length);
  while (length > 0 &&
         (command[length - 1] == '\n' || command[length - 1] == '\r'))
    {
      length--;
    }

  /* NSH can deliver the trailing newline as its own write.  Treat an empty
   * record after trimming exactly like the zero-length terminator above so
   * it cannot replace a successful tone/capture result with -EINVAL.
   */

  if (length == 0)
    {
      return (ssize_t)buflen;
    }

  command[length] = '\0';
  ret = nxmutex_trylock(&g_i2s0.lock);
  if (ret < 0)
    {
      return ret;
    }

  if (strncmp(command, "play=", 5) == 0)
    {
      ret = a733_i2s0_play_wav(command + 5);
    }
  else if (strncmp(command, "volume=", 7) == 0)
    {
      ret = a733_play_parse_volume(command + 7, &first);
      if (ret >= 0)
        {
          g_i2s0.playback_volume = first;
        }
    }
  else if (strcmp(command, "probe") == 0)
    {
      strlcpy(g_i2s0.last_operation, "probe",
              sizeof(g_i2s0.last_operation));
      a733_i2s0_probe();
      ret = OK;
      g_i2s0.last_status = OK;
    }
  else if (strcmp(command, "init") == 0)
    {
      strlcpy(g_i2s0.last_operation, "init",
              sizeof(g_i2s0.last_operation));
      ret = a733_i2s0_hw_initialize();
    }
  else if (strcmp(command, "off") == 0)
    {
      strlcpy(g_i2s0.last_operation, "off",
              sizeof(g_i2s0.last_operation));
      a733_i2s0_stream_stop();
      ret = OK;
      g_i2s0.last_status = OK;
    }
  else if (strncmp(command, "tone=", 5) == 0)
    {
      strlcpy(g_i2s0.last_operation, "tone",
              sizeof(g_i2s0.last_operation));
      ret = a733_parse_pair(command + 5, &first, &second);
      if (ret >= 0)
        {
          ret = a733_i2s0_tone(first, second, true);
        }
    }
  else if (strncmp(command, "tone32=", 7) == 0)
    {
      strlcpy(g_i2s0.last_operation, "tone32",
              sizeof(g_i2s0.last_operation));
      ret = a733_parse_pair(command + 7, &first, &second);
      if (ret >= 0)
        {
          ret = a733_i2s0_tone(first, second, false);
        }
    }
  else if (strncmp(command, "icsvoice=", 9) == 0)
    {
      g_i2s0.last_capture_committed = false;
      g_i2s0.capture_processed = false;
      strlcpy(g_i2s0.last_operation, "icsvoice",
              sizeof(g_i2s0.last_operation));
      ret = a733_parse_voice(command + 9, &first, &second);
      if (ret >= 0)
        {
          ret = a733_i2s0_capture_ex(first, false, 0, 0, 2, false,
                                    true, true, second);
        }
      else
        {
          g_i2s0.last_capture_status = ret;
        }
    }
  else if (strncmp(command, "icscapture=", 11) == 0 ||
           strncmp(command, "icsleft=", 8) == 0 ||
           strncmp(command, "icsright=", 9) == 0 ||
           strncmp(command, "icsraw=", 7) == 0)
    {
      const char *argument = strchr(command, '=') + 1;
      unsigned int slot = strncmp(command, "icsleft=", 8) == 0 ? 0u :
                          (strncmp(command, "icsright=", 9) == 0 ? 1u : 2u);
      bool raw = strncmp(command, "icsraw=", 7) == 0;

      g_i2s0.last_capture_committed = false;
      ret = a733_parse_duration(argument, &first);
      if (ret >= 0)
        {
          ret = a733_i2s0_capture(first, raw, slot, 0, 2, false, true);
        }
      else
        {
          strlcpy(g_i2s0.last_operation, "ics43434",
                  sizeof(g_i2s0.last_operation));
          g_i2s0.last_capture_status = ret;
        }
    }
  else if (strncmp(command, "capturecfg=", 11) == 0)
    {
      unsigned int values[4];

      strlcpy(g_i2s0.last_operation, "capturecfg",
              sizeof(g_i2s0.last_operation));
      g_i2s0.last_capture_committed = false;
      ret = a733_parse_capturecfg(command + 11, values);
      if (ret >= 0)
        {
          ret = a733_i2s0_capture(values[3], false, values[0],
                                  values[1], values[2], true, false);
        }
      else
        {
          g_i2s0.last_capture_status = ret;
        }
    }
  else if (strncmp(command, "capture=", 8) == 0)
    {
      char *end;
      unsigned long duration;

      strlcpy(g_i2s0.last_operation, "capture",
              sizeof(g_i2s0.last_operation));
      strlcpy(g_i2s0.last_capture_mode, "pcm16",
              sizeof(g_i2s0.last_capture_mode));
      g_i2s0.last_capture_committed = false;
      duration = strtoul(command + 8, &end, 10);
      if (end == command + 8 || *end != '\0' || duration > UINT_MAX)
        {
          ret = -EINVAL;
          g_i2s0.last_capture_status = ret;
        }
      else
        {
          ret = a733_i2s0_capture((unsigned int)duration, false,
                                  A733_INMP441_DEFAULT_SLOT, 0, 2, false, false);
        }
    }
  else if (strncmp(command, "captureleft=", 12) == 0 ||
           strncmp(command, "captureright=", 13) == 0)
    {
      const bool right = command[7] == 'r';
      const char *argument = command + (right ? 13 : 12);
      char *end;
      unsigned long duration;

      strlcpy(g_i2s0.last_operation, right ? "captureright" : "captureleft",
              sizeof(g_i2s0.last_operation));
      strlcpy(g_i2s0.last_capture_mode, "pcm16",
              sizeof(g_i2s0.last_capture_mode));
      g_i2s0.last_capture_committed = false;
      duration = strtoul(argument, &end, 10);
      if (end == argument || *end != '\0' || duration > UINT_MAX)
        {
          ret = -EINVAL;
          g_i2s0.last_capture_status = ret;
        }
      else
        {
          ret = a733_i2s0_capture((unsigned int)duration, false,
                                  right ? 1u : 0u, 0, 2, false, false);
        }
    }
  else if (strncmp(command, "capture32=", 10) == 0)
    {
      char *end;
      unsigned long duration;

      strlcpy(g_i2s0.last_operation, "capture32",
              sizeof(g_i2s0.last_operation));
      strlcpy(g_i2s0.last_capture_mode, "raw32",
              sizeof(g_i2s0.last_capture_mode));
      g_i2s0.last_capture_committed = false;
      duration = strtoul(command + 10, &end, 10);
      if (end == command + 10 || *end != '\0' || duration > UINT_MAX)
        {
          ret = -EINVAL;
          g_i2s0.last_capture_status = ret;
        }
      else
        {
          ret = a733_i2s0_capture((unsigned int)duration, true,
                                  A733_INMP441_DEFAULT_SLOT, 0, 2, false, false);
        }
    }
  else
    {
      ret = -EINVAL;
    }

  g_i2s0.last_status = ret;
  if (ret < 0)
    {
      a733_i2s0_probe();
    }

  nxmutex_unlock(&g_i2s0.lock);
  return ret < 0 ? ret : (ssize_t)buflen;
}

static int a733_i2s0_ioctl(struct file *filep, int cmd, unsigned long arg)
{
  int ret;
  (void)filep;
  if (arg == 0)
    {
      return -EINVAL;
    }

  if (cmd != A733_AUDIO_CAPTURE && cmd != A733_AUDIO_PLAY &&
      cmd != A733_AUDIO_FILE && cmd != A733_AUDIO_STREAM && cmd != A733_AUDIO_LIVE_CAPTURE &&
      cmd != A733_AUDIO_MONITOR)
    {
      return -ENOTTY;
    }

  /* Application calls fail busy rather than queue behind an unknown capture
   * or playback. Diagnostic reads/writes also fail busy: they must not wait
   * forever behind an explicitly continuous microphone owner.
   */
  ret = nxmutex_trylock(&g_i2s0.lock);
  if (ret < 0)
    {
      return ret == -EBUSY ? -EBUSY : ret;
    }

  if (cmd == A733_AUDIO_CAPTURE)
    {
      struct a733_audio_capture_s *out = (void *)(uintptr_t)arg;
      out->bytes = 0;
      if (out->version != A733_AUDIO_ABI || out->duration_ms < 100 ||
          out->duration_ms > 10000 || out->gain < 1 || out->gain > 16 ||
          out->pcm == NULL || out->capacity < out->duration_ms * 96u)
        {
          ret = -EINVAL;
        }
      else
        {
          ret = a733_i2s0_capture_into(out->duration_ms, false, 0, 0, 2,
                                      false, true, true, out->gain, out, NULL);
        }
    }
  else if (cmd == A733_AUDIO_PLAY)
    {
      const struct a733_audio_play_s *in = (void *)(uintptr_t)arg;
      if (in->version != A733_AUDIO_ABI || in->pcm == NULL ||
          (in->rate != 8000 && in->rate != 16000 && in->rate != 24000 &&
           in->rate != 48000) || (in->channels != 1 && in->channels != 2) ||
          in->volume > 100 || in->bytes == 0 ||
          in->bytes % (in->channels * 2u) != 0 ||
          in->bytes > in->rate * in->channels * 2u * 10u)
        {
          ret = -EINVAL;
        }
      else
        {
          struct a733_wav_s wav;
          /* Copy before TX: the caller's PCM is never consumed asynchronously. */
          wav.pcm = kmm_malloc(in->bytes);
          if (wav.pcm == NULL)
            {
              ret = -ENOMEM;
            }
          else
            {
              memcpy(wav.pcm, in->pcm, in->bytes);
              wav.bytes = in->bytes;
              wav.rate = in->rate;
              wav.channels = in->channels;
              wav.frames = in->bytes / (in->channels * 2u);
              g_i2s0.playback_volume = in->volume;
              ret = a733_i2s0_play_pcm(&wav);
              kmm_free(wav.pcm);
            }
        }
    }
  else if(cmd==A733_AUDIO_MONITOR)
    {
      struct a733_audio_monitor_s *in=(void *)(uintptr_t)arg;
      in->frames=0;in->ended=0;
      if(in->version!=A733_AUDIO_ABI || !in->consume || in->gain<1 || in->gain>16)ret=-EINVAL;
      else ret=a733_i2s0_monitor(in);
    }
  else if(cmd==A733_AUDIO_LIVE_CAPTURE)
    {
      struct a733_audio_live_s *in=(void *)(uintptr_t)arg;
      in->frames=in->ended=0;
      if(in->version!=A733_AUDIO_ABI || !in->consume || in->max_ms<100 ||
         in->max_ms>8000 || in->gain<1 || in->gain>16)ret=-EINVAL;
      else ret=a733_i2s0_capture_into(in->max_ms,false,0,0,2,false,true,true,in->gain,NULL,in);
    }
  else if(cmd==A733_AUDIO_STREAM)
    {
      ret=a733_i2s0_play_stream((void *)(uintptr_t)arg);
    }
  else
    {
      const struct a733_audio_file_s *in = (void *)(uintptr_t)arg;
      if (in->version != A733_AUDIO_ABI || in->volume > 100 || in->path == NULL)
        {
          ret = -EINVAL;
        }
      else
        {
          g_i2s0.playback_volume = in->volume;
          ret = a733_i2s0_play_wav(in->path);
        }
    }

  g_i2s0.last_status = ret;
  nxmutex_unlock(&g_i2s0.lock);
  return ret;
}

static const struct file_operations g_i2s0_fops =
{
  .read = a733_i2s0_read,
  .write = a733_i2s0_write,
  .ioctl = a733_i2s0_ioctl,
};

int a733_i2s0_audio_initialize(void)
{
  /* Keep optional audio hardware completely outside the boot-critical path.
   * In particular, do not touch an unclocked I2S/PLL/PIO register while
   * nx_bringup() is creating the initial tasks on an eight-core system.
   * The read method and every explicit control operation perform a probe
   * before reporting or using the hardware.
   */

  return register_driver("/dev/a733-audio", &g_i2s0_fops, 0666, NULL);
}

#endif /* CONFIG_A733_I2S0_AUDIO */
