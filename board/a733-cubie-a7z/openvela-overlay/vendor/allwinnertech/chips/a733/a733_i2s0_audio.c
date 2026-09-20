/****************************************************************************
 * vendor/allwinnertech/chips/a733/a733_i2s0_audio.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/* Non-destructive A733 I2S0 hardware checkpoint.
 *
 * This node deliberately does not enable an audio PLL or change PB4..PB8.
 * MAX98357A and INMP441 require an exact sample clock; the active lower-half
 * is enabled only after the clock tree has been measured on real hardware.
 */

#include <nuttx/config.h>

#ifdef CONFIG_A733_I2S0_AUDIO

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <nuttx/arch.h>
#include <nuttx/fs/fs.h>
#include <nuttx/mutex.h>

#include "arm64_internal.h"

#define A733_CCU_BASE          UINT64_C(0x02002000)
#define A733_I2S0_BASE         UINT64_C(0x02532000)
#define A733_I2S0_CLK          (A733_CCU_BASE + UINT64_C(0x1200))
#define A733_I2S0_BGR          (A733_CCU_BASE + UINT64_C(0x120c))

#define I2S_CTL                0x00
#define I2S_FMT0               0x04
#define I2S_FIFOCTL            0x14
#define I2S_FIFOSTA            0x18
#define I2S_CLKDIV             0x24
#define I2S_CHCFG              0x30
#define I2S_REV                0x7c

struct a733_i2s0_diag_s
{
  mutex_t lock;
  unsigned int probes;
  uint32_t clock;
  uint32_t bus;
  uint32_t ctl;
  uint32_t fmt0;
  uint32_t fifoctl;
  uint32_t fifosta;
  uint32_t clkdiv;
  uint32_t chcfg;
  uint32_t revision;
};

static struct a733_i2s0_diag_s g_i2s0 =
{
  .lock = NXMUTEX_INITIALIZER,
};

static void a733_i2s0_probe(void)
{
  g_i2s0.clock = getreg32(A733_I2S0_CLK);
  g_i2s0.bus = getreg32(A733_I2S0_BGR);
  g_i2s0.ctl = getreg32(A733_I2S0_BASE + I2S_CTL);
  g_i2s0.fmt0 = getreg32(A733_I2S0_BASE + I2S_FMT0);
  g_i2s0.fifoctl = getreg32(A733_I2S0_BASE + I2S_FIFOCTL);
  g_i2s0.fifosta = getreg32(A733_I2S0_BASE + I2S_FIFOSTA);
  g_i2s0.clkdiv = getreg32(A733_I2S0_BASE + I2S_CLKDIV);
  g_i2s0.chcfg = getreg32(A733_I2S0_BASE + I2S_CHCFG);
  g_i2s0.revision = getreg32(A733_I2S0_BASE + I2S_REV);
  g_i2s0.probes++;
}

static ssize_t a733_i2s0_read(struct file *filep, char *buffer,
                              size_t buflen)
{
  char report[768];
  size_t length;
  size_t offset;
  int ret;

  if (buffer == NULL)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&g_i2s0.lock);
  if (ret < 0)
    {
      return ret;
    }

  length = (size_t)snprintf(report, sizeof(report),
    "A733 I2S0 audio checkpoint (non-destructive)\n"
    "state=clock-validation-pending probes=%u base=02532000\n"
    "ccu: module=%08lx bus-reset=%08lx\n"
    "regs: ctl=%08lx fmt0=%08lx fifoctl=%08lx fifosta=%08lx "
    "clkdiv=%08lx chcfg=%08lx rev=%08lx\n"
    "pins: PB4=MCLK PB5=BCLK PB6=LRCK PB7=DOUT0 PB8=DIN0 mux=3\n"
    "max98357: BCLK=PB5 LRC=PB6 DIN=PB7; MCLK not required\n"
    "inmp441: SCK=PB5 WS=PB6 SD=PB8 L/R=GND; VDD=3.3V only\n"
    "agent: UART4 TTS active path; official PCM adapter reserved for I2S\n"
    "control: echo probe > /dev/a733-audio\n",
    g_i2s0.probes, (unsigned long)g_i2s0.clock,
    (unsigned long)g_i2s0.bus, (unsigned long)g_i2s0.ctl,
    (unsigned long)g_i2s0.fmt0, (unsigned long)g_i2s0.fifoctl,
    (unsigned long)g_i2s0.fifosta, (unsigned long)g_i2s0.clkdiv,
    (unsigned long)g_i2s0.chcfg, (unsigned long)g_i2s0.revision);

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

static ssize_t a733_i2s0_write(struct file *filep, const char *buffer,
                               size_t buflen)
{
  char command[16];
  size_t length = buflen;
  int ret;

  (void)filep;

  if (buffer == NULL)
    {
      return -EINVAL;
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

  command[length] = '\0';
  if (strcmp(command, "probe") != 0)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&g_i2s0.lock);
  if (ret < 0)
    {
      return ret;
    }

  a733_i2s0_probe();
  nxmutex_unlock(&g_i2s0.lock);
  return (ssize_t)buflen;
}

static const struct file_operations g_i2s0_fops =
{
  .read = a733_i2s0_read,
  .write = a733_i2s0_write,
};

int a733_i2s0_audio_initialize(void)
{
  a733_i2s0_probe();
  return register_driver("/dev/a733-audio", &g_i2s0_fops, 0666, NULL);
}

#endif
