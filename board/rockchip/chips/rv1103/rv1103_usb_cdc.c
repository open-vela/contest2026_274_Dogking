/****************************************************************************
 * vendor/rockchip/chips/rv1103/rv1103_usb_cdc.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Minimal RV1103 Synopsys DWC3 USB2 device controller for CDC ACM.  This is
 * deliberately self-contained: openvela has USB class drivers but no generic
 * DWC3 device-controller layer.  The implementation follows the official
 * Luckfox U-Boot DWC3 endpoint command and TRB protocol.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_RV1103_USB_CDCACM

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>

#include <nuttx/arch.h>
#include <nuttx/fs/fs.h>
#include <nuttx/kthread.h>
#include <nuttx/mutex.h>
#include <nuttx/signal.h>

#include "arm_internal.h"

#define DWC3_BASE                   0xffb00000u
#define DWC3_REG(o)                 (DWC3_BASE + (o))
#define DWC3_GEVNTADRLO             DWC3_REG(0xc400)
#define DWC3_GEVNTADRHI             DWC3_REG(0xc404)
#define DWC3_GEVNTSIZ               DWC3_REG(0xc408)
#define DWC3_GEVNTCOUNT             DWC3_REG(0xc40c)
#define DWC3_DCFG                   DWC3_REG(0xc700)
#define DWC3_DCTL                   DWC3_REG(0xc704)
#define DWC3_DEVTEN                 DWC3_REG(0xc708)
#define DWC3_DSTS                   DWC3_REG(0xc70c)
#define DWC3_DALEPENA               DWC3_REG(0xc720)
#define DWC3_DEPCMDPAR2(n)          DWC3_REG(0xc800 + (n) * 0x10)
#define DWC3_DEPCMDPAR1(n)          DWC3_REG(0xc804 + (n) * 0x10)
#define DWC3_DEPCMDPAR0(n)          DWC3_REG(0xc808 + (n) * 0x10)
#define DWC3_DEPCMD(n)              DWC3_REG(0xc80c + (n) * 0x10)

#define DWC3_DCTL_RUN_STOP          (1u << 31)
#define DWC3_DCTL_CSFTRST           (1u << 30)
#define DWC3_DCFG_DEVADDR_MASK      (0x7fu << 3)
#define DWC3_DEVTEN_DISCONN         (1u << 0)
#define DWC3_DEVTEN_RESET           (1u << 1)
#define DWC3_DEVTEN_CONNECT         (1u << 2)
#define DWC3_DEVTEN_LINK            (1u << 3)

#define DEP_CMD_ACT                 (1u << 10)
#define DEP_CMD_SETCFG              1u
#define DEP_CMD_SETRES              2u
#define DEP_CMD_START               6u
#define DEP_CMD_STARTCFG            9u
#define DEP_CFG_XFER_COMPLETE       (1u << 8)
#define DEP_CFG_XFER_NOT_READY      (1u << 10)
#define DEP_CFG_MPS(n)              ((uint32_t)(n) << 3)
#define DEP_CFG_TYPE(n)             ((uint32_t)(n) << 1)
#define DEP_CFG_FIFO(n)             ((uint32_t)(n) << 17)
#define DEP_CFG_EPNUM(n)            ((uint32_t)(n) << 25)
#define DEP_CFG_ACTION_MODIFY       (2u << 30)

#define TRB_HWO                     (1u << 0)
#define TRB_LST                     (1u << 1)
#define TRB_ISP                     (1u << 10)
#define TRB_IOC                     (1u << 11)
#define TRBCTL_NORMAL               (1u << 4)
#define TRBCTL_SETUP                (2u << 4)
#define TRBCTL_STATUS2              (3u << 4)
#define TRBCTL_STATUS3              (4u << 4)
#define TRBCTL_CONTROL_DATA         (5u << 4)

#define USB_DIR_IN                  0x80
#define USB_TYPE_MASK               0x60
#define USB_TYPE_STANDARD           0x00
#define USB_TYPE_CLASS              0x20
#define USB_REQ_GET_STATUS          0x00
#define USB_REQ_CLEAR_FEATURE       0x01
#define USB_REQ_SET_ADDRESS         0x05
#define USB_REQ_GET_DESCRIPTOR      0x06
#define USB_REQ_GET_CONFIGURATION   0x08
#define USB_REQ_SET_CONFIGURATION   0x09
#define USB_REQ_GET_INTERFACE       0x0a
#define USB_REQ_SET_INTERFACE       0x0b
#define USB_DT_DEVICE               1
#define USB_DT_CONFIG               2
#define USB_DT_STRING               3
#define CDC_SET_LINE_CODING         0x20
#define CDC_GET_LINE_CODING         0x21
#define CDC_SET_CONTROL_LINE_STATE  0x22
#define CDC_SEND_BREAK              0x23

#define EP0_OUT                     0
#define EP0_IN                      1
#define CDC_INT_IN                  3       /* USB endpoint 0x81 */
#define CDC_BULK_OUT                4       /* USB endpoint 0x02 */
#define CDC_BULK_IN                 5       /* USB endpoint 0x82 */
#define EVENT_SIZE                  1024
#define BULK_SIZE                   512
#define RX_RING_SIZE                4096
#define TX_RING_SIZE                4096

struct dwc3_trb_s
{
  uint32_t bpl;
  uint32_t bph;
  uint32_t size;
  uint32_t ctrl;
} __attribute__((packed));

struct usb_setup_s
{
  uint8_t type;
  uint8_t request;
  uint16_t value;
  uint16_t index;
  uint16_t length;
} __attribute__((packed));

enum ep0_phase_e
{
  EP0_SETUP,
  EP0_DATA_IN,
  EP0_DATA_OUT,
  EP0_STATUS_IN,
  EP0_STATUS_OUT
};

struct rv1103_cdc_s
{
  mutex_t lock;
  uint32_t event[EVENT_SIZE / 4] __attribute__((aligned(64)));
  struct dwc3_trb_s trb[6] __attribute__((aligned(64)));
  struct usb_setup_s setup __attribute__((aligned(64)));
  uint8_t control[256] __attribute__((aligned(64)));
  uint8_t rx_dma[BULK_SIZE] __attribute__((aligned(64)));
  uint8_t tx_dma[BULK_SIZE] __attribute__((aligned(64)));
  uint8_t rx_ring[RX_RING_SIZE];
  uint8_t tx_ring[TX_RING_SIZE];
  size_t event_pos;
  size_t rx_get;
  size_t rx_put;
  size_t tx_get;
  size_t tx_put;
  size_t control_length;
  enum ep0_phase_e ep0_phase;
  uint8_t pending_address;
  uint8_t configuration;
  bool address_pending;
  bool out_active;
  bool in_active;
  bool running;
  bool connected_logged;
  unsigned int ep_debug_count;
  uint8_t line_coding[7];
};

static struct rv1103_cdc_s g_cdc =
{
  .lock = NXMUTEX_INITIALIZER,
  .line_coding = {0x00, 0xc2, 0x01, 0x00, 0, 0, 8}, /* 115200 8N1 */
};

/* 1209 is pid.codes' shared VID for open-source prototypes.  0x1103 is a
 * project-local development PID and must be replaced before a product ships.
 */

static const uint8_t g_device_desc[] =
{
  18, 1, 0x00, 0x02, 0xef, 0x02, 0x01, 64,
  0x09, 0x12, 0x03, 0x11, 0x00, 0x01, 1, 2, 3, 1
};

static const uint8_t g_config_desc[] =
{
  9, 2, 75, 0, 2, 1, 0, 0x80, 50,
  8, 11, 0, 2, 2, 2, 1, 0,
  9, 4, 0, 0, 1, 2, 2, 1, 0,
  5, 0x24, 0, 0x10, 0x01,
  5, 0x24, 1, 0, 1,
  4, 0x24, 2, 2,
  5, 0x24, 6, 0, 1,
  7, 5, 0x81, 3, 16, 0, 16,
  9, 4, 1, 0, 2, 0x0a, 0, 0, 0,
  7, 5, 0x02, 2, 0x00, 0x02, 0,
  7, 5, 0x82, 2, 0x00, 0x02, 0
};

static uint16_t getle16(uint16_t value)
{
  const uint8_t *p = (const uint8_t *)&value;
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static void cache_clean(const void *p, size_t n)
{
  up_clean_dcache((uintptr_t)p, (uintptr_t)p + n);
}

static void cache_invalidate(void *p, size_t n)
{
  up_invalidate_dcache((uintptr_t)p, (uintptr_t)p + n);
}

static int dep_cmd(unsigned int ep, uint32_t cmd, uint32_t p0,
                   uint32_t p1, uint32_t p2)
{
  unsigned int retry;
  uint32_t value;

  putreg32(p2, DWC3_DEPCMDPAR2(ep));
  putreg32(p1, DWC3_DEPCMDPAR1(ep));
  putreg32(p0, DWC3_DEPCMDPAR0(ep));
  putreg32(cmd | DEP_CMD_ACT, DWC3_DEPCMD(ep));
  for (retry = 0; retry < 1000; retry++)
    {
      value = getreg32(DWC3_DEPCMD(ep));
      if ((value & DEP_CMD_ACT) == 0)
        {
          return (value & (1u << 15)) != 0 ? -EIO : 0;
        }

      up_udelay(1);
    }

  return -ETIMEDOUT;
}

static int dep_config(unsigned int ep, unsigned int type, unsigned int mps)
{
  uint32_t p0 = DEP_CFG_TYPE(type) | DEP_CFG_MPS(mps);
  uint32_t p1 = DEP_CFG_XFER_COMPLETE | DEP_CFG_XFER_NOT_READY |
                DEP_CFG_EPNUM(ep);
  int ret;

  if ((ep & 1) != 0)
    {
      p0 |= DEP_CFG_FIFO(ep >> 1);
    }

  ret = dep_cmd(ep, DEP_CMD_SETCFG, p0, p1, 0);
  if (ret == 0)
    {
      ret = dep_cmd(ep, DEP_CMD_SETRES, 1, 0, 0);
    }

  return ret;
}

static int dep_reconfig_ep0(unsigned int ep, unsigned int mps)
{
  uint32_t p0 = DEP_CFG_TYPE(0) | DEP_CFG_MPS(mps) |
                DEP_CFG_ACTION_MODIFY;
  uint32_t p1 = DEP_CFG_XFER_COMPLETE | DEP_CFG_XFER_NOT_READY |
                DEP_CFG_EPNUM(ep);

  if ((ep & 1) != 0)
    {
      p0 |= DEP_CFG_FIFO(ep >> 1);
    }

  return dep_cmd(ep, DEP_CMD_SETCFG, p0, p1, 0);
}

static int start_transfer(unsigned int ep, void *buffer, size_t length,
                          uint32_t type)
{
  struct dwc3_trb_s *trb = &g_cdc.trb[ep];
  uintptr_t addr = (uintptr_t)buffer;

  memset(trb, 0, sizeof(*trb));
  trb->bpl = (uint32_t)addr;
  trb->bph = 0;
  trb->size = (uint32_t)length;
  trb->ctrl = type | TRB_HWO | TRB_LST | TRB_IOC | TRB_ISP;
  if ((ep & 1) != 0 && length > 0)
    {
      cache_clean(buffer, length);
    }

  cache_clean(trb, sizeof(*trb));
  addr = (uintptr_t)trb;
  int ret = dep_cmd(ep, DEP_CMD_START, 0, (uint32_t)addr, 0);
  if (ret < 0 && ep <= EP0_IN)
    {
      syslog(LOG_ERR,
             "ERROR: USB CDC: EP%u start failed phase=%u len=%u ret=%d cmd=%08lx\n",
             ep, g_cdc.ep0_phase, (unsigned int)length, ret,
             (unsigned long)getreg32(DWC3_DEPCMD(ep)));
    }

  return ret;
}

static int arm_setup(void)
{
  g_cdc.ep0_phase = EP0_SETUP;
  memset(&g_cdc.setup, 0, sizeof(g_cdc.setup));
  cache_clean(&g_cdc.setup, sizeof(g_cdc.setup));
  return start_transfer(EP0_OUT, &g_cdc.setup, 8, TRBCTL_SETUP);
}

static int start_status(bool in, bool three_stage)
{
  g_cdc.ep0_phase = in ? EP0_STATUS_IN : EP0_STATUS_OUT;
  return start_transfer(in ? EP0_IN : EP0_OUT, g_cdc.control, 0,
                        three_stage ? TRBCTL_STATUS3 : TRBCTL_STATUS2);
}

static size_t make_string(uint8_t index)
{
  const char *ascii;
  size_t i;

  if (index == 0)
    {
      g_cdc.control[0] = 4;
      g_cdc.control[1] = USB_DT_STRING;
      g_cdc.control[2] = 0x09;
      g_cdc.control[3] = 0x04;
      return 4;
    }

  ascii = index == 1 ? "openvela" :
          index == 2 ? "RV1103 CDC ACM" :
          index == 3 ? "RV1103-PICO-MINI" : NULL;
  if (ascii == NULL)
    {
      return 0;
    }

  for (i = 0; ascii[i] != '\0' && i < 126; i++)
    {
      g_cdc.control[2 + i * 2] = ascii[i];
      g_cdc.control[3 + i * 2] = 0;
    }

  g_cdc.control[0] = 2 + i * 2;
  g_cdc.control[1] = USB_DT_STRING;
  return g_cdc.control[0];
}

static int ep0_reply(const void *data, size_t length, size_t requested)
{
  if (length > requested)
    {
      length = requested;
    }

  if (data != g_cdc.control)
    {
      memcpy(g_cdc.control, data, length);
    }

  g_cdc.control_length = length;
  g_cdc.ep0_phase = EP0_DATA_IN;
  return start_transfer(EP0_IN, g_cdc.control, length,
                        TRBCTL_CONTROL_DATA);
}

static int handle_setup(void)
{
  struct usb_setup_s *s = &g_cdc.setup;
  uint16_t value;
  uint16_t length;
  uint8_t byte;
  size_t n;

  cache_invalidate(s, sizeof(*s));
  value = getle16(s->value);
  length = getle16(s->length);

  if ((s->type & USB_TYPE_MASK) == USB_TYPE_STANDARD)
    {
      switch (s->request)
        {
          case USB_REQ_GET_DESCRIPTOR:
            switch (value >> 8)
              {
                case USB_DT_DEVICE:
                  return ep0_reply(g_device_desc, sizeof(g_device_desc),
                                   length);
                case USB_DT_CONFIG:
                  return ep0_reply(g_config_desc, sizeof(g_config_desc),
                                   length);
                case USB_DT_STRING:
                  n = make_string(value & 0xff);
                  return n == 0 ? -EINVAL :
                         ep0_reply(g_cdc.control, n, length);
                default:
                  return -EINVAL;
              }

          case USB_REQ_SET_ADDRESS:
            g_cdc.pending_address = value & 0x7f;
            g_cdc.address_pending = true;
            return start_status(true, false);

          case USB_REQ_SET_CONFIGURATION:
            g_cdc.configuration = value & 0xff;
            syslog(LOG_INFO, "USB CDC: host set configuration %u\n",
                   g_cdc.configuration);
            return start_status(true, false);

          case USB_REQ_GET_CONFIGURATION:
            byte = g_cdc.configuration;
            return ep0_reply(&byte, 1, length);

          case USB_REQ_GET_STATUS:
            g_cdc.control[0] = 0;
            g_cdc.control[1] = 0;
            return ep0_reply(g_cdc.control, 2, length);

          case USB_REQ_CLEAR_FEATURE:
          case USB_REQ_SET_INTERFACE:
            return start_status(true, false);

          case USB_REQ_GET_INTERFACE:
            byte = 0;
            return ep0_reply(&byte, 1, length);

          default:
            return -EINVAL;
        }
    }

  if ((s->type & USB_TYPE_MASK) == USB_TYPE_CLASS)
    {
      switch (s->request)
        {
          case CDC_GET_LINE_CODING:
            return ep0_reply(g_cdc.line_coding, sizeof(g_cdc.line_coding),
                             length);
          case CDC_SET_LINE_CODING:
            if (length != sizeof(g_cdc.line_coding))
              {
                return -EINVAL;
              }

            g_cdc.control_length = length;
            g_cdc.ep0_phase = EP0_DATA_OUT;
            return start_transfer(EP0_OUT, g_cdc.control, length,
                                  TRBCTL_CONTROL_DATA);
          case CDC_SET_CONTROL_LINE_STATE:
          case CDC_SEND_BREAK:
            return start_status(true, false);
          default:
            return -EINVAL;
        }
    }

  return -EINVAL;
}

static size_t ring_count(size_t get, size_t put, size_t size)
{
  return put >= get ? put - get : size - get + put;
}

static void arm_bulk_out(void)
{
  if (g_cdc.configuration != 0 && !g_cdc.out_active &&
      start_transfer(CDC_BULK_OUT, g_cdc.rx_dma, BULK_SIZE,
                     TRBCTL_NORMAL) == 0)
    {
      g_cdc.out_active = true;
    }
}

static void start_bulk_in(void)
{
  size_t count;
  size_t n;

  if (g_cdc.configuration == 0 || g_cdc.in_active)
    {
      return;
    }

  count = ring_count(g_cdc.tx_get, g_cdc.tx_put, TX_RING_SIZE);
  if (count == 0)
    {
      return;
    }

  n = 0;
  while (n < BULK_SIZE && n < count)
    {
      g_cdc.tx_dma[n++] = g_cdc.tx_ring[g_cdc.tx_get];
      g_cdc.tx_get = (g_cdc.tx_get + 1) % TX_RING_SIZE;
    }

  if (start_transfer(CDC_BULK_IN, g_cdc.tx_dma, n, TRBCTL_NORMAL) == 0)
    {
      g_cdc.in_active = true;
    }
}

static void handle_ep_event(uint32_t event)
{
  unsigned int ep = (event >> 1) & 0x1f;
  unsigned int type = (event >> 6) & 0x0f;
  size_t remaining;
  size_t actual;
  size_t i;
  size_t next;

  if (ep <= EP0_IN && g_cdc.ep_debug_count < 24)
    {
      syslog(LOG_INFO,
             "USB CDC: EP0 event raw=%08lx ep=%u type=%u status=%u phase=%u\n",
             (unsigned long)event, ep, type,
             (unsigned int)((event >> 12) & 0x0f),
             g_cdc.ep0_phase);
      g_cdc.ep_debug_count++;
    }

  if (type != 1) /* transfer complete */
    {
      return;
    }

  cache_invalidate(&g_cdc.trb[ep], sizeof(g_cdc.trb[ep]));
  if (ep == EP0_OUT && g_cdc.ep0_phase == EP0_SETUP)
    {
      if (handle_setup() < 0)
        {
          syslog(LOG_WARNING, "USB CDC: unsupported request %02x/%02x\n",
                 g_cdc.setup.type, g_cdc.setup.request);
          arm_setup();
        }
    }
  else if (g_cdc.ep0_phase == EP0_DATA_IN && ep == EP0_IN)
    {
      start_status(false, true);
    }
  else if (g_cdc.ep0_phase == EP0_DATA_OUT && ep == EP0_OUT)
    {
      cache_invalidate(g_cdc.control, g_cdc.control_length);
      memcpy(g_cdc.line_coding, g_cdc.control,
             sizeof(g_cdc.line_coding));
      start_status(true, true);
    }
  else if ((g_cdc.ep0_phase == EP0_STATUS_IN && ep == EP0_IN) ||
           (g_cdc.ep0_phase == EP0_STATUS_OUT && ep == EP0_OUT))
    {
      if (g_cdc.address_pending)
        {
          modifyreg32(DWC3_DCFG, DWC3_DCFG_DEVADDR_MASK,
                      (uint32_t)g_cdc.pending_address << 3);
          g_cdc.address_pending = false;
        }

      arm_setup();
      arm_bulk_out();
    }
  else if (ep == CDC_BULK_OUT)
    {
      g_cdc.out_active = false;
      remaining = g_cdc.trb[ep].size & 0x00ffffffu;
      actual = remaining <= BULK_SIZE ? BULK_SIZE - remaining : 0;
      cache_invalidate(g_cdc.rx_dma, actual);
      for (i = 0; i < actual; i++)
        {
          next = (g_cdc.rx_put + 1) % RX_RING_SIZE;
          if (next == g_cdc.rx_get)
            {
              break;
            }

          g_cdc.rx_ring[g_cdc.rx_put] = g_cdc.rx_dma[i];
          g_cdc.rx_put = next;
        }

      arm_bulk_out();
    }
  else if (ep == CDC_BULK_IN)
    {
      g_cdc.in_active = false;
      start_bulk_in();
    }
}

static void handle_device_event(uint32_t event)
{
  unsigned int type = (event >> 8) & 0x0f;

  if (type == 1) /* USB reset */
    {
      g_cdc.configuration = 0;
      g_cdc.address_pending = false;
      g_cdc.out_active = false;
      g_cdc.in_active = false;
      modifyreg32(DWC3_DCFG, DWC3_DCFG_DEVADDR_MASK, 0);
      if (arm_setup() < 0)
        {
          syslog(LOG_ERR, "ERROR: USB CDC: setup arm failed after reset\n");
        }
      syslog(LOG_INFO, "USB CDC: bus reset\n");
    }
  else if (type == 2 && !g_cdc.connected_logged)
    {
      unsigned int speed = getreg32(DWC3_DSTS) & 7;
      unsigned int mps = speed == 2 ? 8 : 64;
      int ret0;
      int ret1;

      /* DWC3 requires EP0 to start from the SuperSpeed default MPS=512.
       * Only ConnectDone reveals the negotiated bus speed; modify both EP0
       * directions at that point, exactly as the official gadget driver.
       */

      ret0 = dep_reconfig_ep0(EP0_OUT, mps);
      ret1 = dep_reconfig_ep0(EP0_IN, mps);
      if (ret0 < 0 || ret1 < 0)
        {
          syslog(LOG_ERR,
                 "ERROR: USB CDC: EP0 ConnectDone reconfig failed %d/%d\n",
                 ret0, ret1);
        }

      g_cdc.connected_logged = true;
      syslog(LOG_INFO, "USB CDC: connected at %s speed\n",
             speed == 0 ? "high" : speed == 2 ? "low" : "full");
    }
}

static void poll_events(void)
{
  uint32_t count;
  uint32_t event;

  count = getreg32(DWC3_GEVNTCOUNT) & 0xffff;
  if (count == 0)
    {
      return;
    }

  cache_invalidate(g_cdc.event, sizeof(g_cdc.event));
  while (count >= 4)
    {
      event = g_cdc.event[g_cdc.event_pos / 4];
      g_cdc.event_pos = (g_cdc.event_pos + 4) % EVENT_SIZE;
      if ((event & 1) != 0)
        {
          handle_device_event(event);
        }
      else
        {
          handle_ep_event(event);
        }

      putreg32(4, DWC3_GEVNTCOUNT);
      count -= 4;
    }
}

static int cdc_worker(int argc, char *argv[])
{
  (void)argc;
  (void)argv;

  while (g_cdc.running)
    {
      nxmutex_lock(&g_cdc.lock);
      poll_events();
      arm_bulk_out();
      start_bulk_in();
      nxmutex_unlock(&g_cdc.lock);
      nxsig_usleep(1000);
    }

  return 0;
}

static ssize_t cdc_read(struct file *filep, char *buffer, size_t buflen)
{
  size_t n = 0;
  (void)filep;

  nxmutex_lock(&g_cdc.lock);
  while (n < buflen && g_cdc.rx_get != g_cdc.rx_put)
    {
      buffer[n++] = g_cdc.rx_ring[g_cdc.rx_get];
      g_cdc.rx_get = (g_cdc.rx_get + 1) % RX_RING_SIZE;
    }

  nxmutex_unlock(&g_cdc.lock);
  return n > 0 ? (ssize_t)n : -EAGAIN;
}

static ssize_t cdc_write(struct file *filep, const char *buffer,
                         size_t buflen)
{
  size_t n = 0;
  size_t next;
  (void)filep;

  nxmutex_lock(&g_cdc.lock);
  while (n < buflen)
    {
      next = (g_cdc.tx_put + 1) % TX_RING_SIZE;
      if (next == g_cdc.tx_get)
        {
          break;
        }

      g_cdc.tx_ring[g_cdc.tx_put] = buffer[n++];
      g_cdc.tx_put = next;
    }

  start_bulk_in();
  nxmutex_unlock(&g_cdc.lock);
  return n > 0 ? (ssize_t)n : -EAGAIN;
}

static const struct file_operations g_cdc_fops =
{
  .read = cdc_read,
  .write = cdc_write,
};

int rv1103_usb_cdcacm_initialize(void)
{
  uintptr_t event_addr = (uintptr_t)g_cdc.event;
  uint32_t dctl;
  int ret;

  memset(g_cdc.event, 0, sizeof(g_cdc.event));
  memset(g_cdc.trb, 0, sizeof(g_cdc.trb));
  cache_clean(g_cdc.event, sizeof(g_cdc.event));
  cache_clean(g_cdc.trb, sizeof(g_cdc.trb));

  dctl = getreg32(DWC3_DCTL);
  putreg32(dctl | DWC3_DCTL_CSFTRST, DWC3_DCTL);
  for (unsigned int retry = 0; retry < 10000; retry++)
    {
      if ((getreg32(DWC3_DCTL) & DWC3_DCTL_CSFTRST) == 0)
        {
          break;
        }

      up_udelay(1);
    }

  putreg32((uint32_t)event_addr, DWC3_GEVNTADRLO);
  putreg32(0, DWC3_GEVNTADRHI);
  putreg32(EVENT_SIZE, DWC3_GEVNTSIZ);
  putreg32(0, DWC3_GEVNTCOUNT);
  putreg32(0, DWC3_DCFG);                 /* force USB2 high speed */
  putreg32(DWC3_DEVTEN_DISCONN | DWC3_DEVTEN_RESET |
           DWC3_DEVTEN_CONNECT | DWC3_DEVTEN_LINK, DWC3_DEVTEN);

  ret = dep_cmd(EP0_OUT, DEP_CMD_STARTCFG, 0, 0, 0);
  if (ret == 0)
    {
      ret = dep_config(EP0_OUT, 0, 512);
    }

  if (ret == 0)
    {
      ret = dep_config(EP0_IN, 0, 512);
    }

  if (ret == 0)
    {
      ret = dep_cmd(EP0_OUT, DEP_CMD_STARTCFG | (2u << 16), 0, 0, 0);
    }

  if (ret == 0)
    {
      ret = dep_config(CDC_INT_IN, 3, 16);
    }

  if (ret == 0)
    {
      ret = dep_config(CDC_BULK_OUT, 2, BULK_SIZE);
    }

  if (ret == 0)
    {
      ret = dep_config(CDC_BULK_IN, 2, BULK_SIZE);
    }

  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: USB CDC: endpoint setup failed: %d\n", ret);
      return ret;
    }

  putreg32((1u << EP0_OUT) | (1u << EP0_IN) | (1u << CDC_INT_IN) |
           (1u << CDC_BULK_OUT) | (1u << CDC_BULK_IN), DWC3_DALEPENA);
  ret = arm_setup();
  if (ret < 0)
    {
      return ret;
    }

  ret = register_driver("/dev/ttyACM0", &g_cdc_fops, 0666, NULL);
  if (ret < 0)
    {
      return ret;
    }

  g_cdc.running = true;
  ret = kthread_create("usb-cdc", 120, 4096, cdc_worker, NULL);
  if (ret < 0)
    {
      g_cdc.running = false;
      unregister_driver("/dev/ttyACM0");
      return ret;
    }

  modifyreg32(DWC3_DCTL, 0, DWC3_DCTL_RUN_STOP);
  syslog(LOG_INFO,
         "USB CDC: DWC3 gadget started; /dev/ttyACM0 ready, VID:PID=1209:1103\n");
  return 0;
}

#endif
