/* SPDX-License-Identifier: Apache-2.0
 * Narrow USB HCI lower half for the official bt_driver_s / BTH4 upper half.
 * Host supplies HCI packets without H4 prefix. BTH4 adds it on receive.
 * Lock order: g_bt_lock -> g_wifi_usb_lock; never touch drv->priv/bt_net.
 */
#ifdef CONFIG_DRIVERS_BLUETOOTH
#include <nuttx/wireless/bluetooth/bt_driver.h>
#include "a733_hci_framer.h"

static int g_bt_registered = -EAGAIN;
static pid_t g_bt_worker = -1;
static int g_bt_host_error;
static uint32_t g_bt_tx_commands;
static uint32_t g_bt_tx_acl;
static uint32_t g_bt_rx_events;
static uint32_t g_bt_rx_acl;
static uint16_t g_bt_last_tx_opcode;
static uint16_t g_bt_last_rx_opcode;
static uint16_t g_bt_failed_opcode;
static uint8_t g_bt_last_hci_status = 0xff;
static uint8_t g_bt_failed_hci_status;
static uint32_t g_bt_host_failures;
static bool g_bt_acl_rx_toggle;
static bool g_bt_acl_tx_toggle;
static struct a733_hci_frame_s g_bt_evt_frame;
static struct a733_hci_frame_s g_bt_acl_frame;
static uint8_t g_bt_acl_rx[512] __attribute__((aligned(64)));
static uint8_t g_bt_acl_tx[2048] __attribute__((aligned(64)));

static int a733_bt_host_open(struct bt_driver_s *drv);
static int a733_bt_host_send(struct bt_driver_s *drv, enum bt_buf_type_e type,
                             void *data, size_t len);
static void a733_bt_host_close(struct bt_driver_s *drv);
static struct bt_driver_s g_bt_driver =
{
  .open = a733_bt_host_open,
  .send = a733_bt_host_send,
  .close = a733_bt_host_close,
};

#define A733_BT_HOST_REPORT \
  "host: registered=%d open=%u worker=%d error=%d needs-reboot=%u failures=%lu " \
  "tx-command=%lu tx-acl=%lu rx-event=%lu rx-acl=%lu\n" \
  "hci-proof: tx-op=%04x rx-op=%04x status=%02x failed-op=%04x failed-status=%02x\n" \
  "host-control: bluetoothd & then bttool; no probe while host open\n"
#define A733_BT_HOST_ARGS \
  , g_bt_registered, g_bt_host_open, (int)g_bt_worker, g_bt_host_error, \
    g_bt_resync_needed, \
    (unsigned long)g_bt_host_failures, (unsigned long)g_bt_tx_commands, \
    (unsigned long)g_bt_tx_acl, (unsigned long)g_bt_rx_events, \
    (unsigned long)g_bt_rx_acl, g_bt_last_tx_opcode, g_bt_last_rx_opcode, \
    g_bt_last_hci_status, g_bt_failed_opcode, g_bt_failed_hci_status

static int a733_bt_deliver_event(const uint8_t *data, size_t len)
{
  /* Decode only Command Complete/Status headers. No key, random, address
   * or ACL payload is logged. Advertising floods are not printed. */
  if (len >= 6 && (data[0] == 0x0e || data[0] == 0x0f))
    {
      if (data[0] == 0x0e)
        {
          g_bt_last_rx_opcode = data[3] | ((uint16_t)data[4] << 8);
          g_bt_last_hci_status = data[5];
        }
      else
        {
          g_bt_last_rx_opcode = data[4] | ((uint16_t)data[5] << 8);
          g_bt_last_hci_status = data[2];
        }

      if (g_bt_last_hci_status != 0)
        {
          g_bt_failed_opcode = g_bt_last_rx_opcode;
          g_bt_failed_hci_status = g_bt_last_hci_status;
        }

      if (g_bt_rx_events < 32 || g_bt_last_hci_status != 0)
        {
          syslog(g_bt_last_hci_status ? LOG_WARNING : LOG_INFO,
                 "A733 BT HCI: event=%02x opcode=%04x status=%02x\n",
                 data[0], g_bt_last_rx_opcode, g_bt_last_hci_status);
        }
    }

  int ret = bt_netdev_receive(&g_bt_driver, BT_EVT, (void *)data, len);
  if (ret >= 0)
    {
      g_bt_rx_events++;
    }

  return ret;
}

static int a733_bt_deliver_acl(const uint8_t *data, size_t len)
{
  int ret = bt_netdev_receive(&g_bt_driver, BT_ACL_IN, (void *)data, len);
  if (ret >= 0)
    {
      g_bt_rx_acl++;
    }

  return ret;
}

static int a733_bt_rx_worker(int argc, char **argv)
{
  size_t actual;
  int ret;
  bool idle;

  for (; ; )
    {
      ret = nxmutex_lock(&g_bt_lock);
      if (ret < 0)
        {
          nxsig_usleep(20000);
          continue;
        }

      idle = !g_bt_host_open || g_bt_host_error < 0;
      if (!idle)
        {
          ret = a733_bt_event_poll(&actual, 2);
          if (ret == -ETIMEDOUT && !g_bt_unsafe)
            {
              ret = OK;
            }
          else if (ret == OK && actual > 0)
            {
              ret = a733_hci_feed(&g_bt_evt_frame, 2, g_bt_event, actual,
                                   a733_bt_deliver_event);
            }

          if (ret == OK)
            {
              /* Bulk helper serializes controller access with WLAN. A
               * short poll bounds WLAN lock latency; no radio reset. */

              ret = a733_ehci_bulk_timeout(g_wifi.address,
                       g_wifi.bt_acl_in, 512, true, g_bt_acl_rx,
                       sizeof(g_bt_acl_rx), &actual, &g_bt_acl_rx_toggle, 1);
              if (ret == -ETIMEDOUT && actual == 0)
                {
                  ret = OK;
                }
              else if (ret == OK && actual > 0)
                {
                  ret = a733_hci_feed(&g_bt_acl_frame, 4, g_bt_acl_rx, actual,
                                       a733_bt_deliver_acl);
                }
            }

          if (ret < 0)
            {
              g_bt_host_error = ret;
              g_bt_host_failures++;
              syslog(LOG_ERR, "A733 BT: RX transport halted (%d)\n", ret);
            }
        }

      nxmutex_unlock(&g_bt_lock);
      nxsig_usleep(idle ? 20000 : 2000);
    }

  return 0;
}

static int a733_bt_host_open(struct bt_driver_s *drv)
{
  int ret = nxmutex_lock(&g_bt_lock);
  if (ret < 0)
    {
      return ret;
    }

  if (g_bt_resync_needed)
    {
      ret = -EPIPE;
      goto out;
    }

  if (g_bt_host_open || g_bt_unsafe || g_wifi.runtime_transport != OK ||
      g_wifi.bt_event_in == 0 || g_wifi.bt_acl_in == 0 ||
      g_wifi.bt_acl_out == 0 || g_wifi.bt_patch_port_readback != 1)
    {
      ret = g_bt_host_open ? -EBUSY : -ENODEV;
      goto out;
    }

  if (g_bt_worker < 0)
    {
      g_bt_worker = kthread_create("a733-bt-rx", 100, 8192,
                                    a733_bt_rx_worker, NULL);
      if (g_bt_worker < 0)
        {
          ret = g_bt_worker;
          goto out;
        }
    }

  /* Preserve USB DATA toggles across host close/reopen. Only
   * SET_CONFIGURATION (during boot) resets endpoint toggles. */

  memset(&g_bt_evt_frame, 0, sizeof(g_bt_evt_frame));
  memset(&g_bt_acl_frame, 0, sizeof(g_bt_acl_frame));
  g_bt_host_error = 0;
  g_bt_last_hci_status = 0xff;
  g_bt_failed_opcode = 0;
  g_bt_failed_hci_status = 0;
  g_bt_host_open = true;
  ret = OK;
  syslog(LOG_INFO, "A733 BT: official /dev/ttyHCI0 opened, worker=%d\n",
         (int)g_bt_worker);
out:
  nxmutex_unlock(&g_bt_lock);
  return ret;
}

static int a733_bt_host_send(struct bt_driver_s *drv, enum bt_buf_type_e type,
                             void *data, size_t len)
{
  uint8_t setup[8] = {0x20, 0, 0, 0, 0, 0, 0, 0};
  uint8_t *bytes = data;
  size_t actual;
  int ret;

  if (data == NULL ||
      (type == BT_CMD && (len < 3 || len != (size_t)bytes[2] + 3)) ||
      (type == BT_ACL_OUT && (len < 4 ||
       len != 4 + (size_t)bytes[2] + ((size_t)bytes[3] << 8))))
    {
      return -EINVAL;
    }

  if (type != BT_CMD && type != BT_ACL_OUT)
    {
      return -EOPNOTSUPP; /* No SCO/ISO/audio support in this milestone. */
    }

  if ((type == BT_CMD && len > sizeof(g_bt_command)) ||
      (type == BT_ACL_OUT && (len > sizeof(g_bt_acl_tx) || len % 512 == 0)))
    {
      /* ACL TX payload configured <=251, so a valid negotiated fragment
       * never needs an exact-512-byte USB ZLP. Reject rather than pretend
       * the unsupported zero-length termination was sent. */

      return -EMSGSIZE;
    }

  ret = nxmutex_lock(&g_bt_lock);
  if (ret < 0)
    {
      return ret;
    }

  if (!g_bt_host_open || g_bt_host_error < 0 || g_bt_unsafe)
    {
      ret = g_bt_host_error < 0 ? g_bt_host_error : -ENODEV;
      goto out;
    }

  if (type == BT_CMD)
    {
      g_bt_last_tx_opcode = bytes[0] | ((uint16_t)bytes[1] << 8);
      if (g_bt_tx_commands < 32)
        {
          syslog(LOG_INFO, "A733 BT HCI: TX opcode=%04x params=%u\n",
                 g_bt_last_tx_opcode, bytes[2]);
        }

      memcpy(g_bt_command, data, len);
      setup[6] = len & 0xff;
      setup[7] = len >> 8;
      ret = nxmutex_lock(&g_wifi_usb_lock);
      if (ret < 0)
        {
          goto out;
        }

      ret = a733_ehci_control(g_wifi.address, g_wifi.ep0_maxpacket,
                               setup, g_bt_command, len, &actual);
      nxmutex_unlock(&g_wifi_usb_lock);
      if (ret == OK && actual == len)
        {
          g_bt_tx_commands++;
        }
    }
  else
    {
      memcpy(g_bt_acl_tx, data, len);
      ret = a733_ehci_bulk_timeout(g_wifi.address, g_wifi.bt_acl_out, 512,
                                   false, g_bt_acl_tx, len, &actual,
                                   &g_bt_acl_tx_toggle, 500);
      if (ret == OK && actual == len)
        {
          g_bt_tx_acl++;
        }
    }

  if (ret == OK && actual != len)
    {
      ret = -EIO;
    }

  if (ret < 0)
    {
      g_bt_host_error = ret;
      g_bt_host_failures++;
      syslog(LOG_ERR, "A733 BT: TX transport halted (%d)\n", ret);
    }

out:
  nxmutex_unlock(&g_bt_lock);
  return ret < 0 ? ret : (int)len;
}

static void a733_bt_host_close(struct bt_driver_s *drv)
{
  nxmutex_lock(&g_bt_lock);
  g_bt_host_open = false;
  if (g_bt_evt_frame.used != 0 || g_bt_acl_frame.used != 0)
    {
      /* A session aborted mid-USB message cannot treat its tail as a new
       * HCI header. Do not reset the shared WLAN controller to hide it. */

      g_bt_resync_needed = true;
      g_bt_host_error = -EPIPE;
      syslog(LOG_WARNING, "A733 BT: partial frame on close; reboot required\n");
    }
  memset(&g_bt_evt_frame, 0, sizeof(g_bt_evt_frame));
  memset(&g_bt_acl_frame, 0, sizeof(g_bt_acl_frame));
  nxmutex_unlock(&g_bt_lock);
  /* Worker remains dormant for safe reopen; no thread cancellation during
   * DMA or upper-half callbacks, no repeated thread/stack allocation. */
}

static void a733_bt_host_register(void)
{
  g_bt_registered = bt_driver_register(&g_bt_driver);
  syslog(g_bt_registered < 0 ? LOG_WARNING : LOG_INFO,
         "A733 BT: official H4 transport registration (%d)\n",
         g_bt_registered);
}
#else
#define A733_BT_HOST_REPORT "host: not-built\n"
#define A733_BT_HOST_ARGS
static void a733_bt_host_register(void) {}
#endif
