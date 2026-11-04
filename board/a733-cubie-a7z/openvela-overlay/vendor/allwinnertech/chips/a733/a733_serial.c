/****************************************************************************
 * vendor/allwinnertech/chips/a733/a733_serial.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>

#include <nuttx/arch.h>
#include <nuttx/fs/fs.h>
#include <nuttx/kthread.h>
#include <nuttx/semaphore.h>
#include <nuttx/signal.h>
#include <nuttx/serial/uart_16550.h>

#include "arm64_internal.h"

extern void a733_boot_marker(const char *marker);

#define A733_UART0_BASE       0x02500000ul
#define A733_UART_RBR         (A733_UART0_BASE + 0x00)
#define A733_UART_IER         (A733_UART0_BASE + 0x04)
#define A733_UART_LSR         (A733_UART0_BASE + 0x14)
#define A733_UART_USR         (A733_UART0_BASE + 0x7c)
#define A733_UART_RFL         (A733_UART0_BASE + 0x84)
#define A733_UART_LSR_DR      (1u << 0)
#define A733_UART_USR_RFNE    (1u << 3)

static bool g_a733_read_started;
static bool g_a733_rx_seen;
static pid_t g_a733_rxpid = INVALID_PROCESS_ID;
static pid_t g_a733_ctty_pid = INVALID_PROCESS_ID;
static sem_t g_a733_rxsem;
static char g_a733_rxbuffer[256];
static uint16_t g_a733_rxhead;
static uint16_t g_a733_rxtail;

static bool a733_pollconsole_rxready(void)
{
  return (getreg32(A733_UART_LSR) & A733_UART_LSR_DR) != 0 ||
         (getreg32(A733_UART_USR) & A733_UART_USR_RFNE) != 0 ||
         (getreg32(A733_UART_RFL) & 0x3f) != 0;
}

static int a733_pollconsole_rxthread(int argc, FAR char *argv[])
{
  for (;;)
    {
      while (a733_pollconsole_rxready())
        {
          irqstate_t flags;
          uint16_t next;
          pid_t ctty;
          char ch = (char)getreg32(A733_UART_RBR);

          flags = enter_critical_section();
          ctty = g_a733_ctty_pid;
          leave_critical_section(flags);

#ifdef CONFIG_TTY_SIGINT
          if ((unsigned char)ch == CONFIG_TTY_SIGINT_CHAR && ctty > 0)
            {
              /* NSH is blocked in waitpid while a foreground command runs,
               * so this permanent polling receiver must deliver SIGINT.
               */

              nxsig_tgkill(-1, ctty, SIGINT);
              continue;
            }
#endif

          if (ch == '\r')
            {
              ch = '\n';
            }

          flags = enter_critical_section();
          next = (uint16_t)((g_a733_rxhead + 1) %
                            sizeof(g_a733_rxbuffer));
          if (next != g_a733_rxtail)
            {
              g_a733_rxbuffer[g_a733_rxhead] = ch;
              g_a733_rxhead = next;
              leave_critical_section(flags);
              nxsem_post(&g_a733_rxsem);
            }
          else
            {
              leave_critical_section(flags);
            }
        }

      nxsig_usleep(1000);
    }

  return 0;
}

/* The first-stage console deliberately uses polling.  Boot0/U-Boot have
 * already configured UART0, and arm64_lowputc() has proven that this exact
 * register path works after the EL1 handoff.  Keeping this device separate
 * from /dev/console lets NSH come up before UART0 interrupt delivery has
 * been validated on A733's GICv3 topology.
 */

static int a733_pollconsole_open(FAR struct file *filep)
{
  /* The generic 16550 console was opened earlier by nx_start().  It must
   * not compete with this polling console for RBR: that race can deliver
   * the first byte to one path and the rest to the other.  Disable every
   * UART interrupt and give the polling device exclusive ownership of RX.
   * USR.RFNE and RFL are used in addition to LSR.DR because the A733 UART
   * integration does not make LSR.DR reliable enough by itself here.
   */

  putreg32(0, A733_UART_IER);
  if (g_a733_rxpid == INVALID_PROCESS_ID)
    {
      g_a733_rxpid = kthread_create("a7z-uart-rx", 120, 4096,
                                    a733_pollconsole_rxthread, NULL);
      if (g_a733_rxpid < 0)
        {
          return (int)g_a733_rxpid;
        }
    }

  a733_boot_marker("PR\r\n");

  return OK;
}

static ssize_t a733_pollconsole_read(FAR struct file *filep,
                                     FAR char *buffer, size_t buflen)
{
  irqstate_t flags;
  size_t nread = 0;

  if (buflen == 0)
    {
      return 0;
    }

  if (!g_a733_read_started)
    {
      g_a733_read_started = true;
      a733_boot_marker("RI\r\n");
    }

  if ((filep->f_oflags & O_NONBLOCK) != 0)
    {
      if (nxsem_trywait(&g_a733_rxsem) < 0)
        {
          return -EAGAIN;
        }
    }
  else if (nxsem_wait_uninterruptible(&g_a733_rxsem) < 0)
    {
      return -EINTR;
    }

  for (;;)
    {
      flags = enter_critical_section();
      if (g_a733_rxtail == g_a733_rxhead)
        {
          leave_critical_section(flags);
          break;
        }

      buffer[nread++] = g_a733_rxbuffer[g_a733_rxtail];
      g_a733_rxtail = (uint16_t)((g_a733_rxtail + 1) %
                                 sizeof(g_a733_rxbuffer));
      leave_critical_section(flags);

      if (nread >= buflen || nxsem_trywait(&g_a733_rxsem) < 0)
        {
          break;
        }
    }

  if (!g_a733_rx_seen)
    {
      g_a733_rx_seen = true;
      a733_boot_marker("RX\r\n");
    }

  return nread;
}

static ssize_t a733_pollconsole_write(FAR struct file *filep,
                                      FAR const char *buffer,
                                      size_t buflen)
{
  size_t i;

  for (i = 0; i < buflen; i++)
    {
      if (buffer[i] == '\n')
        {
          arm64_lowputc('\r');
        }

      arm64_lowputc(buffer[i]);
    }

  return buflen;
}

static int a733_pollconsole_ioctl(FAR struct file *filep, int cmd,
                                  unsigned long arg)
{
  irqstate_t flags;
  int ret = 0;

  /* tcgetattr() is what isatty() uses to recognize a terminal.  NSH only
   * assigns the foreground command with TIOCSCTTY when isatty() succeeds,
   * so a polling console still has to provide the small termios surface
   * even though U-Boot already fixed the physical UART at 115200 8N1.
   */

  if (cmd == TCGETS)
    {
      FAR struct termios *termiosp = (FAR struct termios *)(uintptr_t)arg;

      if (termiosp == NULL)
        {
          return -EINVAL;
        }

      memset(termiosp, 0, sizeof(*termiosp));
      termiosp->c_iflag = ICRNL;
      termiosp->c_oflag = OPOST | ONLCR;
      termiosp->c_cflag = CS8 | CREAD | CLOCAL;
      termiosp->c_lflag = ISIG | ICANON | ECHO | ECHOE;
      termiosp->c_cc[VINTR] = CONFIG_TTY_SIGINT_CHAR;
      termiosp->c_speed = B115200;
      return OK;
    }

  if (cmd == TCSETS || cmd == TCSETSW || cmd == TCSETSF)
    {
      /* Accept software termios changes.  The stage-1 raw console cannot
       * safely retune the handoff UART yet, but accepting these operations
       * preserves normal tty/NSH semantics and keeps the link at 115200.
       */

      return arg == 0 ? -EINVAL : OK;
    }

  flags = enter_critical_section();
  switch (cmd)
    {
      case TIOCSCTTY:
        if ((int)arg < 0 || g_a733_ctty_pid >= 0)
          {
            ret = -EINVAL;
          }
        else
          {
            g_a733_ctty_pid = (pid_t)arg;
          }
        break;

      case TIOCNOTTY:
        g_a733_ctty_pid = INVALID_PROCESS_ID;
        break;

      default:
        ret = -ENOTTY;
        break;
    }

  leave_critical_section(flags);
  return ret;
}

static const struct file_operations g_a733_pollconsole_fops =
{
  .open  = a733_pollconsole_open,
  .read  = a733_pollconsole_read,
  .write = a733_pollconsole_write,
  .ioctl = a733_pollconsole_ioctl,
};

#ifdef USE_SERIALDRIVER

void arm64_earlyserialinit(void)
{
  u16550_earlyserialinit();
}

void arm64_serialinit(void)
{
  int ret;

  a733_boot_marker("S0\r\n");
  u16550_serialinit();
  a733_boot_marker("S1\r\n");

  nxsem_init(&g_a733_rxsem, 0, 0);

  ret = register_driver("/dev/a7zconsole", &g_a733_pollconsole_fops,
                        0666, NULL);
  a733_boot_marker(ret == OK ? "P1\r\n" : "PE\r\n");
}

#endif
