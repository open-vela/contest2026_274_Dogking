/* SPDX-License-Identifier: Apache-2.0
 * FCU760K USB HCI acceptance gate. Included by the USB1 owner so Wi-Fi and
 * Bluetooth cannot concurrently replace EHCI descriptors. This is NOT a
 * Bluetooth host stack. After hardware acceptance use official bt_driver_s
 * and openvela GATT, not a private ATT implementation.
 */

static mutex_t g_bt_lock = NXMUTEX_INITIALIZER;
static struct ehci_qh_s g_bt_qh __attribute__((aligned(64)));
static struct ehci_qtd_s g_bt_qtd __attribute__((aligned(64)));
static uint32_t g_bt_frames[1024] __attribute__((aligned(4096)));
static uint8_t g_bt_event[64] __attribute__((aligned(64)));
static uint8_t g_bt_command[512] __attribute__((aligned(64)));
static bool g_bt_host_open;
static bool g_bt_resync_needed;
static bool g_bt_toggle;
static bool g_bt_unsafe;
static int g_bt_last = -EAGAIN;
static const char *g_bt_stage = "not-probed";
static unsigned int g_bt_probes;
static unsigned int g_bt_events;
static uint8_t g_bt_status = 0xff;
static uint8_t g_bt_version[8];
static uint8_t g_bt_address[6];
static uint8_t g_bt_features[8];
static int g_bt_command_ret = -EAGAIN;
static int g_bt_poll_ret = -EAGAIN;
static size_t g_bt_command_actual;
static size_t g_bt_event_actual;
static unsigned int g_bt_polls;
static uint32_t g_bt_qtd_token;
static uint32_t g_bt_schedule_cmd;
static uint32_t g_bt_schedule_status;
static uint32_t g_bt_frame_start;
static uint32_t g_bt_frame_end;
static const char *g_bt_phase = "idle";

static int a733_bt_periodic_stop(uintptr_t opbase)
{
  unsigned int i;

  a733_modifyreg32(opbase + EHCI_USBCMD_OFFSET, EHCI_USBCMD_PSEN, 0);
  for (i = 0; i < 100; i++)
    {
      if ((getreg32(opbase + EHCI_USBSTS_OFFSET) & EHCI_USBSTS_PSS) == 0)
        {
          return OK;
        }

      nxsig_usleep(1000);
    }

  /* Dedicated static DMA descriptors remain valid even on stop failure.
   * Never rewrite them again until reboot. */

  g_bt_unsafe = true;
  return -ETIMEDOUT;
}

static int a733_bt_event_poll(size_t *actual, unsigned int timeout_ms)
{
  uintptr_t opbase = A733_EHCI1_BASE + g_wifi.caplength;
  uint32_t qhaddr;
  uint32_t qtdaddr;
  uint32_t frameaddr;
  uint32_t token;
  uint32_t residual;
  unsigned int i;
  int ret;
  int stopret;

  *actual = 0;
  ret = nxmutex_lock(&g_wifi_usb_lock);
  if (ret < 0)
    {
      return ret;
    }

  if (g_bt_unsafe)
    {
      ret = -EIO;
      goto out;
    }

  ret = a733_ehci_async_stop(opbase + EHCI_USBCMD_OFFSET,
                             opbase + EHCI_USBSTS_OFFSET);
  if (ret < 0)
    {
      goto out;
    }

  ret = a733_bt_periodic_stop(opbase);
  if (ret < 0)
    {
      goto out;
    }

  if (a733_dma32(&g_bt_qh, &qhaddr) < 0 ||
      a733_dma32(&g_bt_qtd, &qtdaddr) < 0 ||
      a733_dma32(g_bt_frames, &frameaddr) < 0)
    {
      ret = -EFAULT;
      goto out;
    }

  memset(&g_bt_qh, 0, sizeof(g_bt_qh));
  memset(&g_bt_qtd, 0, sizeof(g_bt_qtd));
  memset(g_bt_event, 0, sizeof(g_bt_event));
  for (i = 0; i < 1024; i++)
    {
      g_bt_frames[i] = qhaddr | QH_HLP_TYP_QH;
    }

  /* Runtime descriptor: high-speed interrupt IN, bInterval=4 (1 ms).
   * Unlike bulk, this belongs on the periodic schedule. */

  g_bt_qh.hlp = QH_HLP_T;
  g_bt_qh.epchar = ((uint32_t)g_wifi.address << QH_EPCHAR_DEVADDR_SHIFT) |
                   ((uint32_t)g_wifi.bt_event_in << QH_EPCHAR_ENDPT_SHIFT) |
                   QH_EPCHAR_EPS_HIGH | QH_EPCHAR_DTC |
                   (64u << QH_EPCHAR_MAXPKT_SHIFT);
  g_bt_qh.epcaps = QH_EPCAPS_MULT(1) | QH_EPCAPS_SSMASK(1);
  g_bt_qh.overlay.nqp = qtdaddr;
  g_bt_qh.overlay.alt = QH_AQP_T;
  g_bt_qtd.nqp = QTD_NQP_T;
  g_bt_qtd.alt = QTD_AQP_T;
  g_bt_qtd.token = QTD_TOKEN_ACTIVE | QTD_TOKEN_PID_IN |
                   (3u << QTD_TOKEN_CERR_SHIFT) | QTD_TOKEN_IOC |
                   (g_bt_toggle ? QTD_TOKEN_TOGGLE : 0) |
                   (64u << QTD_TOKEN_NBYTES_SHIFT);
  ret = a733_qtd_buffer(&g_bt_qtd, g_bt_event, sizeof(g_bt_event));
  if (ret < 0)
    {
      goto out;
    }

  up_flush_dcache((uintptr_t)g_bt_event,
                   (uintptr_t)g_bt_event + sizeof(g_bt_event));
  up_clean_dcache((uintptr_t)&g_bt_qtd,
                   (uintptr_t)&g_bt_qtd + sizeof(g_bt_qtd));
  up_clean_dcache((uintptr_t)&g_bt_qh,
                   (uintptr_t)&g_bt_qh + sizeof(g_bt_qh));
  up_clean_dcache((uintptr_t)g_bt_frames,
                   (uintptr_t)g_bt_frames + sizeof(g_bt_frames));
  putreg32(frameaddr, opbase + EHCI_PERIODICLISTBASE_OFFSET);
  a733_modifyreg32(opbase + EHCI_USBCMD_OFFSET,
                   EHCI_USBCMD_FLSIZE_MASK, EHCI_USBCMD_PSEN | EHCI_USBCMD_RUN);
  g_bt_frame_start = getreg32(opbase + EHCI_FRINDEX_OFFSET);
  for (i = 0; i < timeout_ms; i++)
    {
      up_invalidate_dcache((uintptr_t)&g_bt_qtd,
                            (uintptr_t)&g_bt_qtd + sizeof(g_bt_qtd));
      if ((g_bt_qtd.token & QTD_TOKEN_ACTIVE) == 0)
        {
          break;
        }

      nxsig_usleep(1000);
    }

  g_bt_schedule_cmd = getreg32(opbase + EHCI_USBCMD_OFFSET);
  g_bt_schedule_status = getreg32(opbase + EHCI_USBSTS_OFFSET);
  g_bt_frame_end = getreg32(opbase + EHCI_FRINDEX_OFFSET);
  stopret = a733_bt_periodic_stop(opbase);
  up_invalidate_dcache((uintptr_t)&g_bt_qtd,
                        (uintptr_t)&g_bt_qtd + sizeof(g_bt_qtd));
  token = g_bt_qtd.token;
  g_bt_qtd_token = token;
  residual = (token & QTD_TOKEN_NBYTES_MASK) >> QTD_TOKEN_NBYTES_SHIFT;
  ret = (token & QTD_TOKEN_ERRORS) != 0 ? -EIO :
         ((token & QTD_TOKEN_ACTIVE) != 0 ? -ETIMEDOUT : OK);
  if (stopret < 0)
    {
      ret = stopret;
    }

  if (ret == OK && residual <= sizeof(g_bt_event))
    {
      *actual = sizeof(g_bt_event) - residual;
      g_bt_toggle = (token & QTD_TOKEN_TOGGLE) != 0;
      up_invalidate_dcache((uintptr_t)g_bt_event,
                            (uintptr_t)g_bt_event + sizeof(g_bt_event));
    }

out:
  g_bt_polls++;
  g_bt_poll_ret = ret;
  g_bt_event_actual = *actual;
  nxmutex_unlock(&g_wifi_usb_lock);
  return ret;
}

static int a733_bt_hci_query(uint16_t opcode, uint8_t *result, size_t size)
{
  uint8_t setup[8] = {0x20, 0, 0, 0, 0, 0, 3, 0};
  clock_t deadline = clock_systime_ticks() + MSEC2TICK(2000);
  size_t actual;
  int ret;

  g_bt_command[0] = opcode & 0xff;
  g_bt_command[1] = opcode >> 8;
  g_bt_command[2] = 0; /* USB has no H4 packet-type byte. */
  g_bt_phase = "ep0-command";
  ret = nxmutex_lock(&g_wifi_usb_lock);
  if (ret < 0)
    {
      return ret;
    }

  ret = a733_ehci_control(g_wifi.address, g_wifi.ep0_maxpacket,
                           setup, g_bt_command, 3, &actual);
  g_bt_command_ret = ret;
  g_bt_command_actual = actual;
  nxmutex_unlock(&g_wifi_usb_lock);
  if (ret < 0 || actual != 3)
    {
      return ret < 0 ? ret : -EPROTO;
    }

  g_bt_phase = "interrupt-event";
  while ((sclock_t)(clock_systime_ticks() - deadline) < 0)
    {
      ret = a733_bt_event_poll(&actual, 10);
      if (ret == -ETIMEDOUT && !g_bt_unsafe)
        {
          nxsig_usleep(1000); /* Allow WLAN RX/TX between bounded polls. */
          continue;
        }

      if (ret < 0)
        {
          return ret;
        }

      g_bt_events++;
      if (actual < 2 || actual != (size_t)g_bt_event[1] + 2)
        {
          return -EPROTO;
        }

      if (g_bt_event[0] != 0x0e || actual < 6 ||
          g_bt_event[3] != (opcode & 0xff) || g_bt_event[4] != (opcode >> 8))
        {
          continue;
        }

      g_bt_status = g_bt_event[5];
      if (g_bt_status != 0)
        {
          return -EIO;
        }

      if (actual != size + 6)
        {
          return -EPROTO;
        }

      memcpy(result, g_bt_event + 6, size);
      g_bt_phase = "command-complete";
      return OK;
    }

  return -ETIMEDOUT;
}

static int a733_bt_probe(void)
{
  unsigned int i;
  bool descriptor_ok = false;
  int ret;

  if (g_bt_host_open)
    {
      return -EBUSY; /* Never steal the official host's command replies. */
    }
  if (g_bt_resync_needed)
    {
      return -EPIPE; /* Reboot, not a shared USB/WLAN reset. */
    }

  g_bt_probes++;
  g_bt_status = 0xff;
  g_bt_command_ret = -EAGAIN;
  g_bt_poll_ret = -EAGAIN;
  g_bt_command_actual = 0;
  g_bt_event_actual = 0;
  g_bt_polls = 0;
  g_bt_qtd_token = 0;
  g_bt_schedule_cmd = 0;
  g_bt_schedule_status = 0;
  g_bt_frame_start = 0;
  g_bt_frame_end = 0;
  g_bt_phase = "idle";
  memset(g_bt_version, 0, sizeof(g_bt_version));
  memset(g_bt_address, 0, sizeof(g_bt_address));
  memset(g_bt_features, 0, sizeof(g_bt_features));
  g_bt_stage = "usb-descriptor";
  if (g_bt_unsafe || g_wifi.runtime_pid != A733_AIC_RUNTIME_PID ||
      g_wifi.runtime_transport != OK)
    {
      return g_bt_unsafe ? -EIO : -ENODEV;
    }

  for (i = 0; i < g_wifi.endpoint_count; i++)
    {
      const struct a733_wifi_endpoint_s *ep = &g_wifi.endpoints[i];
      if (ep->interface == 0 && ep->alternate == 0 &&
          ep->address == (0x80 | g_wifi.bt_event_in) &&
          (ep->attributes & 3) == 3 && ep->maxpacket == 64 &&
          ep->interval == 4)
        {
          descriptor_ok = true;
        }
    }

  if (!descriptor_ok)
    {
      return -EOPNOTSUPP;
    }

  g_bt_stage = "read-local-version";
  ret = a733_bt_hci_query(0x1001, g_bt_version, sizeof(g_bt_version));
  if (ret < 0)
    {
      return ret;
    }

  g_bt_stage = "read-bd-address";
  ret = a733_bt_hci_query(0x1009, g_bt_address, sizeof(g_bt_address));
  if (ret < 0)
    {
      return ret;
    }

  g_bt_stage = "read-le-features";
  ret = a733_bt_hci_query(0x2003, g_bt_features, sizeof(g_bt_features));
  if (ret == OK)
    {
      g_bt_stage = "hci-query-passed";
    }

  return ret;
}

#include "a733_bt_transport.inc"

static ssize_t a733_bt_read(struct file *filep, char *buffer, size_t buflen)
{
  char report[1536];
  size_t len;
  size_t copy;
  int ret = nxmutex_lock(&g_bt_lock);

  if (ret < 0)
    {
      return ret;
    }

  ret = snprintf(report, sizeof(report),
    "A733 FCU760K USB HCI / official openvela host transport\n"
    "last=%d stage=%s probes=%u events=%u hci-status=%02x unsafe=%u\n"
    "runtime=%04x:%04x event=%02x ACL=%02x/%02x\n"
    "firmware: mode=%lu HCI-port=%lu readback=%lu expected-port=1(USB)\n"
    "transport: phase=%s command=%d/%lu event=%d/%lu polls=%u\n"
    "periodic: cmd=%08lx sts=%08lx qtd=%08lx frame=%04lx->%04lx\n"
    "version=%02x revision=%02x%02x manufacturer=%02x%02x\n"
    "address=%02x:%02x:%02x:%02x:%02x:%02x\n"
    "le-features=%02x%02x%02x%02x%02x%02x%02x%02x\n"
    "control: echo probe > /dev/a733-bt (read-only HCI queries; no reset)\n"
    "target: openvela BLE GATT central -> ESP32-S3 peripheral\n"
    A733_BT_HOST_REPORT,
    g_bt_last, g_bt_stage, g_bt_probes, g_bt_events, g_bt_status, g_bt_unsafe,
    g_wifi.runtime_vid, g_wifi.runtime_pid, g_wifi.bt_event_in,
    g_wifi.bt_acl_out, g_wifi.bt_acl_in,
    (unsigned long)g_wifi.bt_patch_mode,
    (unsigned long)g_wifi.bt_patch_port,
    (unsigned long)g_wifi.bt_patch_port_readback,
    g_bt_phase, g_bt_command_ret, (unsigned long)g_bt_command_actual,
    g_bt_poll_ret, (unsigned long)g_bt_event_actual, g_bt_polls,
    (unsigned long)g_bt_schedule_cmd, (unsigned long)g_bt_schedule_status,
    (unsigned long)g_bt_qtd_token,
    (unsigned long)g_bt_frame_start, (unsigned long)g_bt_frame_end,
    g_bt_version[0], g_bt_version[2], g_bt_version[1],
    g_bt_version[5], g_bt_version[4],
    g_bt_address[5], g_bt_address[4], g_bt_address[3],
    g_bt_address[2], g_bt_address[1], g_bt_address[0],
    g_bt_features[7], g_bt_features[6], g_bt_features[5], g_bt_features[4],
    g_bt_features[3], g_bt_features[2], g_bt_features[1], g_bt_features[0]
    A733_BT_HOST_ARGS);
  nxmutex_unlock(&g_bt_lock);
  if (ret < 0)
    {
      return ret;
    }

  len = (size_t)ret < sizeof(report) ? (size_t)ret : sizeof(report) - 1;
  if (filep->f_pos < 0 || (size_t)filep->f_pos >= len)
    {
      return 0;
    }

  copy = len - (size_t)filep->f_pos;
  if (copy > buflen)
    {
      copy = buflen;
    }

  memcpy(buffer, report + filep->f_pos, copy);
  filep->f_pos += copy;
  return copy;
}

static ssize_t a733_bt_write(struct file *filep, const char *buffer,
                              size_t buflen)
{
  char command[16];
  int ret;

  if (buflen == 0 || buflen >= sizeof(command))
    {
      return -EINVAL;
    }

  memcpy(command, buffer, buflen);
  command[buflen] = 0;
  command[strcspn(command, "\r\n")] = 0;
  if (strcmp(command, "probe") != 0)
    {
      return -EINVAL;
    }

  ret = nxmutex_trylock(&g_bt_lock);
  if (ret < 0)
    {
      return ret;
    }

  g_bt_last = a733_bt_probe();
  ret = g_bt_last;
  syslog(ret < 0 ? LOG_WARNING : LOG_INFO,
         "A733 BT: stage=%s result=%d events=%u\n",
         g_bt_stage, ret, g_bt_events);
  nxmutex_unlock(&g_bt_lock);
  return ret < 0 ? ret : (ssize_t)buflen;
}

static const struct file_operations g_a733_bt_fops =
{
  .read = a733_bt_read,
  .write = a733_bt_write,
};
