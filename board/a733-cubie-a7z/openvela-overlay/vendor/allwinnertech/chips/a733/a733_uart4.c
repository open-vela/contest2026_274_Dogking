/****************************************************************************
 * vendor/allwinnertech/chips/a733/a733_uart4.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/* A733 UART4 polling character driver for the Cubie A7Z header.
 *
 * The register address, reset/gate bit and PJ24/PJ25 mux are taken from the
 * official sun60iw2 BSP.  Polling is intentional: TW-TTS traffic is tiny and
 * this keeps the first hardware acceptance test independent of GIC/DMA.
 */

#include <nuttx/config.h>

#ifdef CONFIG_A733_UART4

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>

#include <nuttx/arch.h>
#include <nuttx/clock.h>
#include <nuttx/fs/fs.h>
#include <nuttx/mutex.h>
#include <nuttx/signal.h>

#include "arm64_internal.h"

#define A733_CCU_BASE          UINT64_C(0x02002000)
#define A733_PIO_BASE          UINT64_C(0x02000000)
#define A733_PIO_FIRST_BANK    UINT64_C(0x80)
#define A733_PIO_STRIDE        UINT64_C(0x80)
#define A733_UART4_BGR         (A733_CCU_BASE + UINT64_C(0x0e10))
#define A733_APB_UART_CLK      (A733_CCU_BASE + UINT64_C(0x0538))
#define A733_UART4_BASE        UINT64_C(0x02504000)

#define UART_RBR               0x00
#define UART_THR               0x00
#define UART_DLL               0x00
#define UART_DLH               0x04
#define UART_IER               0x04
#define UART_FCR               0x08
#define UART_LCR               0x0c
#define UART_MCR               0x10
#define UART_LSR               0x14
#define UART_USR               0x7c

#define UART_LCR_DLAB          (1u << 7)
#define UART_LSR_DR            (1u << 0)
#define UART_LSR_THRE          (1u << 5)
#define UART_USR_TFNF          (1u << 1)

#define A733_UART4_CLOCK       24000000u
#define A733_UART4_BAUD        9600u
#define A733_UART4_TIMEOUT     MSEC2TICK(1000)
#define A733_UART4_GATE        (1u << 0)
#define A733_UART4_RESET       (1u << 16)

#define A733_PJ_CFG3           (A733_PIO_BASE + A733_PIO_FIRST_BANK + \
                                9u * A733_PIO_STRIDE + 0x0c)

static mutex_t g_uart4_lock = NXMUTEX_INITIALIZER;

static ssize_t a733_uart4_diag_read(struct file *filep, char *buffer,
                                    size_t buflen)
{
  char report[512];
  size_t length;
  size_t offset;
  size_t copy;
  uint32_t pjcfg = getreg32(A733_PJ_CFG3);
  uint32_t bgr = getreg32(A733_UART4_BGR);
  uint32_t apb = getreg32(A733_APB_UART_CLK);
  uint32_t lcr = getreg32(A733_UART4_BASE + UART_LCR);
  uint32_t lsr = getreg32(A733_UART4_BASE + UART_LSR);
  uint32_t usr = getreg32(A733_UART4_BASE + UART_USR);

  if (buffer == NULL)
    {
      return -EINVAL;
    }

  length = (size_t)snprintf(report, sizeof(report),
    "A733 UART4 TW-TTS checkpoint\n"
    "node: /dev/ttyS4 baud=%u format=8N1 polling=1\n"
    "clock: apb-uart=%08lx source=%lu divider=%lu "
    "bgr=%08lx gate=%lu reset=%lu\n"
    "pins: PJ24=TX/function%lu PJ25=RX/function%lu cfg3=%08lx\n"
    "regs: lcr=%08lx lsr=%08lx thre=%lu usr=%08lx tfnf=%lu\n"
    "expected: source=0 divider=0 gate=1 reset=1 functions=4/4 "
    "and thre-or-tfnf=1\n",
    A733_UART4_BAUD,
    (unsigned long)apb,
    (unsigned long)((apb >> 24) & 7u),
    (unsigned long)(apb & 0x1fu),
    (unsigned long)bgr,
    (unsigned long)(bgr & A733_UART4_GATE ? 1 : 0),
    (unsigned long)(bgr & A733_UART4_RESET ? 1 : 0),
    (unsigned long)((pjcfg >> 0) & 0xfu),
    (unsigned long)((pjcfg >> 4) & 0xfu),
    (unsigned long)pjcfg,
    (unsigned long)lcr,
    (unsigned long)lsr,
    (unsigned long)(lsr & UART_LSR_THRE ? 1 : 0),
    (unsigned long)usr,
    (unsigned long)(usr & UART_USR_TFNF ? 1 : 0));

  offset = (size_t)filep->f_pos;
  if (offset >= length)
    {
      return 0;
    }

  copy = length - offset;
  if (copy > buflen)
    {
      copy = buflen;
    }

  memcpy(buffer, report + offset, copy);
  filep->f_pos += (off_t)copy;
  return (ssize_t)copy;
}

static const struct file_operations g_uart4_diag_fops =
{
  .read = a733_uart4_diag_read,
};

static void a733_pinmux(unsigned int bank, unsigned int pin,
                        unsigned int function)
{
  /* sun60iw2 uses pinctrl HW type 4, not the legacy Allwinner layout:
   * bank A starts at +0x80, each bank occupies 0x80 bytes, drive registers
   * start at +0x20 and pull registers at +0x30.
   */

  uintptr_t base = A733_PIO_BASE + A733_PIO_FIRST_BANK +
                   bank * A733_PIO_STRIDE;
  uintptr_t cfg = base + (pin / 8) * 4;
  uintptr_t drv = base + 0x20 + (pin / 8) * 4;
  uintptr_t pul = base + 0x30 + (pin / 16) * 4;
  unsigned int cfgshift = (pin % 8) * 4;
  unsigned int drvshift = (pin % 8) * 4;
  unsigned int pulshift = (pin % 16) * 2;
  uint32_t value;

  value = getreg32(cfg);
  putreg32((value & ~(0xfu << cfgshift)) | (function << cfgshift), cfg);
  value = getreg32(drv);
  putreg32((value & ~(0xfu << drvshift)) | (1u << drvshift), drv);
  value = getreg32(pul);
  putreg32(value & ~(3u << pulshift), pul);
}

static int a733_uart4_open(struct file *filep)
{
  return OK;
}

static ssize_t a733_uart4_read(struct file *filep, char *buffer,
                               size_t buflen)
{
  size_t nread = 0;

  if (buffer == NULL)
    {
      return -EINVAL;
    }

  while (nread < buflen)
    {
      if ((getreg32(A733_UART4_BASE + UART_LSR) & UART_LSR_DR) == 0)
        {
          if (nread != 0)
            {
              break;
            }

          if ((filep->f_oflags & O_NONBLOCK) != 0)
            {
              return -EAGAIN;
            }

          nxsig_usleep(1000);
          continue;
        }

      buffer[nread++] = (char)getreg32(A733_UART4_BASE + UART_RBR);
    }

  return (ssize_t)nread;
}

static ssize_t a733_uart4_write(struct file *filep, const char *buffer,
                                size_t buflen)
{
  size_t written = 0;
  int ret;

  if (buffer == NULL)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&g_uart4_lock);
  if (ret < 0)
    {
      return ret;
    }

  while (written < buflen)
    {
      clock_t start = clock_systime_ticks();

      while ((getreg32(A733_UART4_BASE + UART_LSR) & UART_LSR_THRE) == 0 &&
             (getreg32(A733_UART4_BASE + UART_USR) & UART_USR_TFNF) == 0)
        {
          if (clock_systime_ticks() - start > A733_UART4_TIMEOUT)
            {
              nxmutex_unlock(&g_uart4_lock);
              return written != 0 ? (ssize_t)written : -ETIMEDOUT;
            }
        }

      putreg32((uint8_t)buffer[written++], A733_UART4_BASE + UART_THR);
    }

  nxmutex_unlock(&g_uart4_lock);
  return (ssize_t)written;
}

static int a733_uart4_ioctl(struct file *filep, int cmd,
                            unsigned long arg)
{
  struct termios *termiosp = (struct termios *)(uintptr_t)arg;

  if (cmd == TCGETS)
    {
      if (termiosp == NULL)
        {
          return -EINVAL;
        }

      memset(termiosp, 0, sizeof(*termiosp));
      /* NuttX keeps the encoded baud selector in c_cflag and the numeric
       * baud rate in c_speed.  B9600 is an encoded selector, not 9600.
       * Mixing the two made cfsetospeed() turn c_speed into 9600 and the
       * following TCSETS comparison reject it with EINVAL.
       */

      termiosp->c_cflag = CS8 | CREAD | CLOCAL | B9600;
      termiosp->c_speed = A733_UART4_BAUD;
      return OK;
    }

  if (cmd == TCSETS || cmd == TCSETSW || cmd == TCSETSF)
    {
      if (termiosp == NULL)
        {
          return -EINVAL;
        }

      return cfgetospeed(termiosp) == A733_UART4_BAUD ? OK : -EINVAL;
    }

  return -ENOTTY;
}

static const struct file_operations g_uart4_fops =
{
  .open  = a733_uart4_open,
  .read  = a733_uart4_read,
  .write = a733_uart4_write,
  .ioctl = a733_uart4_ioctl,
};

int a733_uart4_initialize(void)
{
  uint32_t divisor = A733_UART4_CLOCK / (16u * A733_UART4_BAUD);
  uint32_t value;
  int ret;

  /* PJ24=TX, PJ25=RX, mux function 4. */

  a733_pinmux(9, 24, 4);
  a733_pinmux(9, 25, 4);

  /* The UART leaf gate is fed from the separate APB-UART clock.  Select the
   * 24 MHz oscillator with divide-by-one explicitly instead of depending on
   * the bootloader's clock-tree residue.  Then assert reset, enable the leaf
   * gate and only afterwards release reset.  Readbacks and short delays are
   * intentional MMIO ordering points; enabling gate and reset in one write
   * left the DesignWare FIFO status permanently at zero on the first board
   * test.
   */

  value = getreg32(A733_APB_UART_CLK);
  value &= ~((7u << 24) | 0x1fu);
  putreg32(value, A733_APB_UART_CLK);
  (void)getreg32(A733_APB_UART_CLK);

  value = getreg32(A733_UART4_BGR);
  putreg32((value | A733_UART4_GATE) & ~A733_UART4_RESET,
           A733_UART4_BGR);
  (void)getreg32(A733_UART4_BGR);
  up_udelay(10);

  putreg32(value | A733_UART4_GATE | A733_UART4_RESET, A733_UART4_BGR);
  (void)getreg32(A733_UART4_BGR);
  up_udelay(10);

  putreg32(0, A733_UART4_BASE + UART_IER);
  putreg32(UART_LCR_DLAB | 3u, A733_UART4_BASE + UART_LCR);
  putreg32(divisor & 0xff, A733_UART4_BASE + UART_DLL);
  putreg32((divisor >> 8) & 0xff, A733_UART4_BASE + UART_DLH);
  putreg32(3u, A733_UART4_BASE + UART_LCR); /* 8 data, no parity, 1 stop */
  putreg32(7u, A733_UART4_BASE + UART_FCR); /* enable/reset both FIFOs */
  putreg32(0, A733_UART4_BASE + UART_MCR);

  ret = register_driver("/dev/ttyS4", &g_uart4_fops, 0666, NULL);
  if (ret < 0)
    {
      return ret;
    }

  /* Keep a read-only hardware snapshot separate from the byte-stream node so
   * field diagnostics never consume received UART data.
   */

  ret = register_driver("/dev/a733-uart4", &g_uart4_diag_fops, 0444, NULL);
  return ret < 0 ? ret : OK;
}

#endif
