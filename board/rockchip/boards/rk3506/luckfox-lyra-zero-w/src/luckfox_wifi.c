/****************************************************************************
 * vendor/rockchip/boards/rk3506/luckfox-lyra-zero-w/src/luckfox_wifi.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_RK3506_AIC8800DC_PROBE

#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>

#include <nuttx/arch.h>
#include <nuttx/signal.h>

#include <arch/board/board.h>

#include <arch/chip/rk3506_gpio.h>

#include "arm_internal.h"

/* The Lyra Zero W connects AIC8800DC to USB2 OTG1.  GPIO1_C5 is the
 * active-high module power input documented by the vendor device tree.
 */

#define RK3506_WIFI_POWER_PIN          21u /* GPIO1_C5 */

#define RK3506_CRU_BASE                0xff9a0000u
#define RK3506_CRU_GATE_CON01          (RK3506_CRU_BASE + 0x0804u)
#define RK3506_CRU_GATE_CON07          (RK3506_CRU_BASE + 0x081cu)

#define RK3506_USBPHY_BASE             0xff2b0000u
#define RK3506_GRF_BASE                0xff288000u
#define RK3506_USBOTG1_BASE            0xff780000u

#define RK3506_PHY1_TUNE               (RK3506_USBPHY_BASE + 0x0430u)
#define RK3506_PHY1_LINESEL            (RK3506_USBPHY_BASE + 0x0494u)
#define RK3506_PHY_CLKOUT              (RK3506_USBPHY_BASE + 0x041cu)
#define RK3506_GRF_USBPHY_CON1         (RK3506_GRF_BASE + 0x0070u)
#define RK3506_GRF_USBPHY_STATUS       (RK3506_GRF_BASE + 0x0118u)

#define DWC2_GSNPSID                   (RK3506_USBOTG1_BASE + 0x0040u)
#define DWC2_GINTSTS                   (RK3506_USBOTG1_BASE + 0x0014u)
#define DWC2_HPRT0                     (RK3506_USBOTG1_BASE + 0x0440u)

#define DWC2_GAHBCFG                   (RK3506_USBOTG1_BASE + 0x0008u)
#define DWC2_GUSBCFG                   (RK3506_USBOTG1_BASE + 0x000cu)
#define DWC2_GRSTCTL                   (RK3506_USBOTG1_BASE + 0x0010u)
#define DWC2_GHWCFG2                   (RK3506_USBOTG1_BASE + 0x0048u)
#define DWC2_HCFG                      (RK3506_USBOTG1_BASE + 0x0400u)
#define DWC2_PCGCCTL                   (RK3506_USBOTG1_BASE + 0x0e00u)

#define DWC2_HC_BASE(ch)               (RK3506_USBOTG1_BASE + 0x0500u + \
                                        (uintptr_t)(ch) * 0x20u)
#define DWC2_HCCHAR(ch)                (DWC2_HC_BASE(ch) + 0x00u)
#define DWC2_HCSPLT(ch)                (DWC2_HC_BASE(ch) + 0x04u)
#define DWC2_HCINT(ch)                 (DWC2_HC_BASE(ch) + 0x08u)
#define DWC2_HCINTMSK(ch)              (DWC2_HC_BASE(ch) + 0x0cu)
#define DWC2_HCTSIZ(ch)                (DWC2_HC_BASE(ch) + 0x10u)
#define DWC2_HCDMA(ch)                 (DWC2_HC_BASE(ch) + 0x14u)

#define DWC2_ID_MASK                   0xffff0000u
#define DWC2_OTG_ID                    0x4f540000u

#define DWC2_GAHBCFG_INCR4             (3u << 1)
#define DWC2_GAHBCFG_DMAEN             (1u << 5)
#define DWC2_GUSBCFG_FORCEHOST         (1u << 29)
#define DWC2_GUSBCFG_FORCEDEV          (1u << 30)
#define DWC2_GRSTCTL_CSFTRST           (1u << 0)
#define DWC2_GRSTCTL_RXFFLSH           (1u << 4)
#define DWC2_GRSTCTL_TXFFLSH           (1u << 5)
#define DWC2_GRSTCTL_TXFNUM_ALL        (0x10u << 6)
#define DWC2_GRSTCTL_AHBIDLE           (1u << 31)

#define DWC2_HPRT_CONN                 (1u << 0)
#define DWC2_HPRT_CONNDET              (1u << 1)
#define DWC2_HPRT_ENA                  (1u << 2)
#define DWC2_HPRT_ENACHG               (1u << 3)
#define DWC2_HPRT_OVRCURRCHG           (1u << 5)
#define DWC2_HPRT_RESET                (1u << 8)
#define DWC2_HPRT_POWER                (1u << 12)
#define DWC2_HPRT_SPEED_SHIFT          17
#define DWC2_HPRT_SPEED_MASK           (3u << DWC2_HPRT_SPEED_SHIFT)

#define DWC2_HCCHAR_MPS_MASK           0x7ffu
#define DWC2_HCCHAR_EPNUM_SHIFT        11
#define DWC2_HCCHAR_DIRIN              (1u << 15)
#define DWC2_HCCHAR_LSDEV              (1u << 17)
#define DWC2_HCCHAR_EPTYPE_SHIFT       18
#define DWC2_HCCHAR_MULTICNT_1         (1u << 20)
#define DWC2_HCCHAR_DEVADDR_SHIFT      22
#define DWC2_HCCHAR_CHDIS              (1u << 30)
#define DWC2_HCCHAR_CHEN               (1u << 31)

#define DWC2_HCINT_XFERCOMP            (1u << 0)
#define DWC2_HCINT_CHHLTD              (1u << 1)
#define DWC2_HCINT_NAK                 (1u << 4)
#define DWC2_HCINT_ALL                 0x3fffu

#define DWC2_HCTSIZ_PKTCNT_SHIFT       19
#define DWC2_HCTSIZ_PID_SHIFT          29
#define DWC2_PID_DATA0                 0u
#define DWC2_PID_DATA1                 2u
#define DWC2_PID_SETUP                 3u
#define DWC2_EPTYPE_CONTROL            0u
#define DWC2_EPTYPE_BULK               2u

#define USB_REQ_GET_DESCRIPTOR         6u
#define USB_REQ_SET_ADDRESS            5u
#define USB_REQ_GET_STATUS             0u
#define USB_REQ_CLEAR_FEATURE          1u
#define USB_REQ_SET_FEATURE            3u
#define USB_REQ_SET_CONFIGURATION      9u
#define USB_DESC_DEVICE                1u
#define USB_DESC_CONFIG                2u
#define USB_DESC_HUB                   0x29u

#define USB_HUB_PORT_CONNECTION        (1u << 0)
#define USB_HUB_PORT_ENABLE            (1u << 1)
#define USB_HUB_PORT_RESET             (1u << 4)
#define USB_HUB_PORT_POWER             (1u << 8)
#define USB_HUB_PORT_LOW_SPEED         (1u << 9)
#define USB_HUB_PORT_HIGH_SPEED        (1u << 10)
#define USB_HUB_FEAT_PORT_RESET        4u
#define USB_HUB_FEAT_PORT_POWER        8u
#define USB_HUB_FEAT_C_CONNECTION      16u
#define USB_HUB_FEAT_C_ENABLE          17u
#define USB_HUB_FEAT_C_RESET           20u

#define RK3506_USB_CHANNEL             0
#define RK3506_USB_DMA_SIZE            256

#define RK3506_WRITE_MASK(mask, value) \
  ((((uint32_t)(mask)) << 16) | ((uint32_t)(value) & (mask)))

#ifdef CONFIG_RK3506_AIC8800DC_ENUM_PROBE
static uint8_t g_wifi_usb_dma[RK3506_USB_DMA_SIZE]
  __attribute__((aligned(RK3506_USB_DMA_SIZE)));
static uint8_t g_aic_data_in_ep;
static uint8_t g_aic_data_out_ep;
static uint8_t g_aic_msg_in_ep;
static uint8_t g_aic_msg_out_ep;

static int luckfox_dwc2_wait(uintptr_t reg, uint32_t mask, bool set,
                             unsigned int timeout_ms)
{
  unsigned int elapsed;

  for (elapsed = 0; elapsed < timeout_ms; elapsed++)
    {
      if (((getreg32(reg) & mask) != 0) == set)
        {
          return 0;
        }

      nxsig_usleep(1000);
    }

  return -ETIMEDOUT;
}

static void luckfox_dwc2_hprt_write(uint32_t set, uint32_t clear)
{
  uint32_t hprt;

  hprt = getreg32(DWC2_HPRT0);

  /* PRTENA is disabled by writing one; the change bits are W1C. */

  hprt &= ~(DWC2_HPRT_ENA | DWC2_HPRT_CONNDET |
            DWC2_HPRT_ENACHG | DWC2_HPRT_OVRCURRCHG | clear);
  hprt |= set;
  putreg32(hprt, DWC2_HPRT0);
}

static int luckfox_dwc2_core_initialize(void)
{
  uint32_t regval;
  uint32_t hprt;
  const char *speed;
  unsigned int channels;
  unsigned int i;
  int ret;

  ret = luckfox_dwc2_wait(DWC2_GRSTCTL, DWC2_GRSTCTL_AHBIDLE, true, 100);
  if (ret < 0)
    {
      syslog(LOG_ERR, "WIFI: DWC2 AHB did not become idle\n");
      return ret;
    }

  putreg32(DWC2_GRSTCTL_CSFTRST, DWC2_GRSTCTL);
  ret = luckfox_dwc2_wait(DWC2_GRSTCTL, DWC2_GRSTCTL_CSFTRST, false, 100);
  if (ret < 0)
    {
      syslog(LOG_ERR, "WIFI: DWC2 soft reset timed out\n");
      return ret;
    }

  nxsig_usleep(100 * 1000);

  regval = getreg32(DWC2_GUSBCFG);
  regval &= ~DWC2_GUSBCFG_FORCEDEV;
  regval |= DWC2_GUSBCFG_FORCEHOST;
  putreg32(regval, DWC2_GUSBCFG);
  nxsig_usleep(30 * 1000);

  if ((getreg32(DWC2_GINTSTS) & 1u) == 0)
    {
      syslog(LOG_ERR, "WIFI: DWC2 did not enter host mode\n");
      return -EIO;
    }

  putreg32(DWC2_GAHBCFG_INCR4 | DWC2_GAHBCFG_DMAEN, DWC2_GAHBCFG);
  putreg32(0, DWC2_PCGCCTL);

  regval = getreg32(DWC2_HCFG);
  regval = (regval & ~3u) | 1u; /* FS/LS clock is 48 MHz. */
  putreg32(regval, DWC2_HCFG);

  putreg32(DWC2_GRSTCTL_TXFFLSH | DWC2_GRSTCTL_TXFNUM_ALL,
           DWC2_GRSTCTL);
  ret = luckfox_dwc2_wait(DWC2_GRSTCTL, DWC2_GRSTCTL_TXFFLSH, false, 100);
  if (ret < 0)
    {
      return ret;
    }

  putreg32(DWC2_GRSTCTL_RXFFLSH, DWC2_GRSTCTL);
  ret = luckfox_dwc2_wait(DWC2_GRSTCTL, DWC2_GRSTCTL_RXFFLSH, false, 100);
  if (ret < 0)
    {
      return ret;
    }

  channels = ((getreg32(DWC2_GHWCFG2) >> 14) & 15u) + 1u;
  for (i = 0; i < channels; i++)
    {
      putreg32(DWC2_HCCHAR_CHDIS, DWC2_HCCHAR(i));
      putreg32(DWC2_HCCHAR_CHDIS | DWC2_HCCHAR_CHEN, DWC2_HCCHAR(i));
      luckfox_dwc2_wait(DWC2_HCCHAR(i), DWC2_HCCHAR_CHEN, false, 10);
      putreg32(0, DWC2_HCINTMSK(i));
      putreg32(DWC2_HCINT_ALL, DWC2_HCINT(i));
    }

  luckfox_dwc2_hprt_write(DWC2_HPRT_POWER, 0);
  nxsig_usleep(20 * 1000);
  luckfox_dwc2_hprt_write(DWC2_HPRT_POWER | DWC2_HPRT_RESET, 0);
  nxsig_usleep(50 * 1000);
  luckfox_dwc2_hprt_write(DWC2_HPRT_POWER, DWC2_HPRT_RESET);
  nxsig_usleep(100 * 1000);

  hprt = getreg32(DWC2_HPRT0);
  switch ((hprt & DWC2_HPRT_SPEED_MASK) >> DWC2_HPRT_SPEED_SHIFT)
    {
      case 0:
        speed = "high";
        break;
      case 1:
        speed = "full";
        break;
      case 2:
        speed = "low";
        break;
      default:
        speed = "invalid";
        break;
    }

  syslog(LOG_INFO, "WIFI: DWC2 host initialized, HPRT=%08" PRIx32
         " speed=%s\n", hprt, speed);

  if ((hprt & (DWC2_HPRT_CONN | DWC2_HPRT_ENA)) !=
      (DWC2_HPRT_CONN | DWC2_HPRT_ENA))
    {
      syslog(LOG_ERR, "WIFI: USB port reset did not enable the device\n");
      return -ENODEV;
    }

  return 0;
}

static int luckfox_dwc2_transfer_ep(unsigned int devaddr, unsigned int mps,
                                    unsigned int epnum,
                                    unsigned int eptype, bool in,
                                    unsigned int pid, void *buffer,
                                    unsigned int length,
                                    unsigned int *actual)
{
  uintptr_t hcchar_reg = DWC2_HCCHAR(RK3506_USB_CHANNEL);
  uintptr_t hcint_reg = DWC2_HCINT(RK3506_USB_CHANNEL);
  uint32_t hcchar;
  uint32_t hcint;
  uint32_t remaining;
  unsigned int packets;
  unsigned int retry;
  int ret;

  if (length > RK3506_USB_DMA_SIZE || mps == 0 || mps > 1024 ||
      epnum > 15 || eptype > 3)
    {
      return -EINVAL;
    }

  packets = length == 0 ? 1 : (length + mps - 1) / mps;
  if (packets > 1023)
    {
      return -E2BIG;
    }

  if (!in && length > 0)
    {
      memcpy(g_wifi_usb_dma, buffer, length);
      up_clean_dcache((uintptr_t)g_wifi_usb_dma,
                      (uintptr_t)g_wifi_usb_dma + RK3506_USB_DMA_SIZE);
    }
  else if (in && length > 0)
    {
      up_invalidate_dcache((uintptr_t)g_wifi_usb_dma,
                           (uintptr_t)g_wifi_usb_dma +
                           RK3506_USB_DMA_SIZE);
    }

  for (retry = 0; retry < 100; retry++)
    {
      putreg32(0, DWC2_HCSPLT(RK3506_USB_CHANNEL));
      putreg32(DWC2_HCINT_ALL, hcint_reg);
      putreg32((length & 0x7ffffu) |
               (packets << DWC2_HCTSIZ_PKTCNT_SHIFT) |
               (pid << DWC2_HCTSIZ_PID_SHIFT),
               DWC2_HCTSIZ(RK3506_USB_CHANNEL));
      putreg32((uint32_t)(uintptr_t)g_wifi_usb_dma,
               DWC2_HCDMA(RK3506_USB_CHANNEL));

      hcchar = (mps & DWC2_HCCHAR_MPS_MASK) |
               (epnum << DWC2_HCCHAR_EPNUM_SHIFT) |
               (eptype << DWC2_HCCHAR_EPTYPE_SHIFT) |
               (devaddr << DWC2_HCCHAR_DEVADDR_SHIFT) |
               DWC2_HCCHAR_MULTICNT_1;
      if (in)
        {
          hcchar |= DWC2_HCCHAR_DIRIN;
        }

      if (((getreg32(DWC2_HPRT0) & DWC2_HPRT_SPEED_MASK) >>
           DWC2_HPRT_SPEED_SHIFT) == 2u)
        {
          hcchar |= DWC2_HCCHAR_LSDEV;
        }

      putreg32(hcchar | DWC2_HCCHAR_CHEN, hcchar_reg);
      ret = luckfox_dwc2_wait(hcint_reg, DWC2_HCINT_CHHLTD, true, 500);
      if (ret < 0)
        {
          putreg32(hcchar | DWC2_HCCHAR_CHDIS | DWC2_HCCHAR_CHEN,
                   hcchar_reg);
          syslog(LOG_ERR, "WIFI: USB channel halt timeout\n");
          return ret;
        }

      hcint = getreg32(hcint_reg);
      if ((hcint & DWC2_HCINT_XFERCOMP) != 0)
        {
          remaining = getreg32(DWC2_HCTSIZ(RK3506_USB_CHANNEL)) & 0x7ffffu;

          /* Some DWC2 revisions do not update the residual byte count for
           * successful OUT DMA transactions.  XFERCOMP means the complete
           * programmed OUT packet was accepted; only IN needs residual-byte
           * accounting for a possible short packet.
           */

          *actual = in ? length - remaining : length;

          if (in && *actual > 0)
            {
              up_invalidate_dcache((uintptr_t)g_wifi_usb_dma,
                                   (uintptr_t)g_wifi_usb_dma +
                                   RK3506_USB_DMA_SIZE);
              memcpy(buffer, g_wifi_usb_dma, *actual);
            }

          return 0;
        }

      if ((hcint & DWC2_HCINT_NAK) == 0)
        {
          syslog(LOG_ERR, "WIFI: USB transaction failed HCINT=%08" PRIx32
                 " HCTSIZ=%08" PRIx32 "\n", hcint,
                 getreg32(DWC2_HCTSIZ(RK3506_USB_CHANNEL)));
          return -EIO;
        }

      nxsig_usleep(1000);
    }

  syslog(LOG_ERR, "WIFI: USB transaction remained NAKed\n");
  return -ETIMEDOUT;
}

static int luckfox_dwc2_transfer(unsigned int devaddr, unsigned int mps,
                                 bool in, unsigned int pid,
                                 void *buffer, unsigned int length,
                                 unsigned int *actual)
{
  return luckfox_dwc2_transfer_ep(devaddr, mps, 0, DWC2_EPTYPE_CONTROL,
                                  in, pid, buffer, length, actual);
}

static int luckfox_dwc2_bulk(unsigned int devaddr, unsigned int epaddr,
                             unsigned int mps, void *buffer,
                             unsigned int length, unsigned int *actual)
{
  return luckfox_dwc2_transfer_ep(devaddr, mps, epaddr & 0x0fu,
                                  DWC2_EPTYPE_BULK,
                                  (epaddr & 0x80u) != 0,
                                  DWC2_PID_DATA0, buffer, length, actual);
}

static int luckfox_dwc2_control(unsigned int devaddr, unsigned int mps,
                                const uint8_t setup[8], void *buffer,
                                unsigned int length, unsigned int *actual)
{
  unsigned int transferred;
  bool datain;
  int ret;

  datain = (setup[0] & 0x80u) != 0;
  *actual = 0;
  ret = luckfox_dwc2_transfer(devaddr, mps, false, DWC2_PID_SETUP,
                              (void *)setup, 8, &transferred);
  if (ret < 0 || transferred != 8)
    {
      syslog(LOG_ERR,
             "WIFI: USB SETUP stage failed ret=%d actual=%u HCINT=%08" PRIx32
             " HCTSIZ=%08" PRIx32 "\n", ret, transferred,
             getreg32(DWC2_HCINT(RK3506_USB_CHANNEL)),
             getreg32(DWC2_HCTSIZ(RK3506_USB_CHANNEL)));
      return ret < 0 ? ret : -EIO;
    }

  if (length > 0)
    {
      ret = luckfox_dwc2_transfer(devaddr, mps, datain, DWC2_PID_DATA1,
                                  buffer, length, actual);
      if (ret < 0)
        {
          return ret;
        }
    }

  ret = luckfox_dwc2_transfer(devaddr, mps,
                              length > 0 ? !datain : true,
                              DWC2_PID_DATA1, g_wifi_usb_dma, 0,
                              &transferred);
  return ret;
}

static int luckfox_usb_no_data_request(unsigned int devaddr,
                                       unsigned int mps,
                                       uint8_t requesttype,
                                       uint8_t request,
                                       uint16_t value,
                                       uint16_t index)
{
  uint8_t setup[8];
  unsigned int actual;

  memset(setup, 0, sizeof(setup));
  setup[0] = requesttype;
  setup[1] = request;
  setup[2] = value & 0xffu;
  setup[3] = value >> 8;
  setup[4] = index & 0xffu;
  setup[5] = index >> 8;
  return luckfox_dwc2_control(devaddr, mps, setup, NULL, 0, &actual);
}

static int luckfox_usb_hub_port_status(unsigned int hubaddr,
                                       unsigned int hubmps,
                                       unsigned int port,
                                       uint32_t *status)
{
  uint8_t setup[8];
  uint8_t data[4];
  unsigned int actual;
  int ret;

  memset(setup, 0, sizeof(setup));
  setup[0] = 0xa3; /* Class, other recipient, device-to-host. */
  setup[1] = USB_REQ_GET_STATUS;
  setup[4] = port;
  setup[6] = sizeof(data);
  ret = luckfox_dwc2_control(hubaddr, hubmps, setup, data,
                              sizeof(data), &actual);
  if (ret < 0 || actual != sizeof(data))
    {
      return ret < 0 ? ret : -EIO;
    }

  *status = data[0] | ((uint32_t)data[1] << 8) |
            ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
  return 0;
}

static int luckfox_usb_hub_feature(unsigned int hubaddr,
                                   unsigned int hubmps,
                                   unsigned int port,
                                   bool set, uint16_t feature)
{
  return luckfox_usb_no_data_request(hubaddr, hubmps, 0x23,
                                     set ? USB_REQ_SET_FEATURE :
                                           USB_REQ_CLEAR_FEATURE,
                                     feature, port);
}

static int luckfox_wifi_parse_aic_config(const uint8_t *config,
                                         unsigned int length)
{
  unsigned int offset = 0;
  unsigned int interfaces = 0;
  unsigned int endpoints = 0;
  unsigned int vendor_interfaces = 0;
  bool vendor_alt0 = false;

  g_aic_data_in_ep = 0;
  g_aic_data_out_ep = 0;
  g_aic_msg_in_ep = 0;
  g_aic_msg_out_ep = 0;

  while (offset + 2 <= length)
    {
      unsigned int dlen = config[offset];
      unsigned int dtype = config[offset + 1];

      if (dlen < 2 || offset + dlen > length)
        {
          syslog(LOG_ERR,
                 "WIFI: malformed AIC config descriptor at %u len=%u\n",
                 offset, dlen);
          return -EPROTO;
        }

      if (dtype == 4 && dlen >= 9) /* Interface descriptor. */
        {
          vendor_alt0 = config[offset + 3] == 0 &&
                        config[offset + 5] == 0xff &&
                        config[offset + 6] == 0xff &&
                        config[offset + 7] == 0xff;

          /* bNumInterfaces counts unique interface numbers, not alternate
           * settings.  Every interface has an alternate setting zero.
           */

          if (config[offset + 3] == 0)
            {
              interfaces++;
              if (vendor_alt0)
                {
                  vendor_interfaces++;
                }
            }

          syslog(LOG_INFO,
                 "WIFI: AIC interface=%u alt=%u class=%02x/%02x/%02x "
                 "endpoints=%u\n",
                 config[offset + 2], config[offset + 3],
                 config[offset + 5], config[offset + 6],
                 config[offset + 7], config[offset + 4]);
        }
      else if (dtype == 5 && dlen >= 7) /* Endpoint descriptor. */
        {
          unsigned int maxpacket = config[offset + 4] |
                                   ((unsigned int)config[offset + 5] << 8);

          endpoints++;
          syslog(LOG_INFO,
                 "WIFI: AIC endpoint=%02x attributes=%02x maxpacket=%u "
                 "interval=%u\n",
                 config[offset + 2], config[offset + 3], maxpacket,
                 config[offset + 6]);

          if (vendor_alt0 && (config[offset + 3] & 3u) == 2u &&
              maxpacket == 512)
            {
              uint8_t epaddr = config[offset + 2];

              if ((epaddr & 0x80u) != 0)
                {
                  if (g_aic_data_in_ep == 0)
                    {
                      g_aic_data_in_ep = epaddr;
                    }
                  else if (g_aic_msg_in_ep == 0)
                    {
                      g_aic_msg_in_ep = epaddr;
                    }
                }
              else
                {
                  if (g_aic_data_out_ep == 0)
                    {
                      g_aic_data_out_ep = epaddr;
                    }
                  else if (g_aic_msg_out_ep == 0)
                    {
                      g_aic_msg_out_ep = epaddr;
                    }
                }
            }
        }

      offset += dlen;
    }

  if (offset != length || interfaces != config[4] ||
      interfaces != 3 || vendor_interfaces == 0 || endpoints == 0)
    {
      syslog(LOG_ERR,
             "WIFI: unexpected AIC topology parsed=%u/%u interfaces=%u/%u "
             "vendor=%u endpoints=%u\n",
             offset, length, interfaces, config[4], vendor_interfaces,
             endpoints);
      return -EPROTO;
    }

  if (g_aic_data_in_ep == 0 || g_aic_data_out_ep == 0 ||
      g_aic_msg_in_ep == 0 || g_aic_msg_out_ep == 0)
    {
      syslog(LOG_ERR, "WIFI: incomplete AIC bulk endpoint map\n");
      return -ENODEV;
    }

  syslog(LOG_INFO,
         "WIFI: AIC endpoint map passed, interfaces=%u endpoints=%u "
         "data=%02x/%02x msg=%02x/%02x\n",
         interfaces, endpoints, g_aic_data_out_ep, g_aic_data_in_ep,
         g_aic_msg_out_ep, g_aic_msg_in_ep);
  return 0;
}

static int luckfox_wifi_aic_read_chipid(unsigned int address)
{
  uint8_t request[20];
  uint8_t response[RK3506_USB_DMA_SIZE];
  unsigned int actual;
  unsigned int packet_length;
  uint16_t response_id;
  uint16_t parameter_length;
  uint32_t memory_address;
  uint32_t memory_value;
  int ret;

  /* Match aicwf_set_cmd_tx() from the vendor driver.  The outer USB message
   * has a four-byte header and a dummy word, followed by an lmac_msg.  This
   * DBG_MEM_READ_REQ is read-only and is the first operation used by the
   * vendor's AIC8800DC system_config_8800dc() path.
   */

  memset(request, 0, sizeof(request));
  request[0] = 16;                 /* lmac message length + dummy word. */
  request[2] = 0x11;               /* USB_TYPE_CFG_CMD_RSP. */
  request[8] = 0x00;               /* DBG_MEM_READ_REQ = 0x0400. */
  request[9] = 0x04;
  request[10] = 0x01;              /* TASK_DBG. */
  request[12] = 100;               /* DRV_TASK_ID. */
  request[14] = 4;                 /* sizeof(dbg_mem_read_req). */
  request[16] = 0x00;              /* 0x40500000, little endian. */
  request[17] = 0x00;
  request[18] = 0x50;
  request[19] = 0x40;

  ret = luckfox_dwc2_bulk(address, g_aic_msg_out_ep, 512, request,
                           sizeof(request), &actual);
  if (ret < 0 || actual != sizeof(request))
    {
      syslog(LOG_ERR, "WIFI: AIC chip-id request failed: %d/%u\n",
             ret, actual);
      return ret < 0 ? ret : -EIO;
    }

  memset(response, 0, sizeof(response));
  ret = luckfox_dwc2_bulk(address, g_aic_msg_in_ep, 512, response,
                           sizeof(response), &actual);
  if (ret < 0)
    {
      syslog(LOG_ERR, "WIFI: AIC chip-id response failed: %d\n", ret);
      return ret;
    }

  packet_length = response[0] | ((unsigned int)response[1] << 8);
  response_id = response[4] | ((uint16_t)response[5] << 8);
  parameter_length = response[10] | ((uint16_t)response[11] << 8);
  if (actual < 24 || packet_length + 4 > actual ||
      (response[2] & 0x7fu) != 0x11 || response_id != 0x0401 ||
      parameter_length < 8)
    {
      syslog(LOG_ERR,
             "WIFI: invalid AIC chip-id response len=%u/%u type=%02x "
             "id=%04x params=%u\n",
             packet_length, actual, response[2], response_id,
             parameter_length);
      return -EPROTO;
    }

  memory_address = response[16] | ((uint32_t)response[17] << 8) |
                   ((uint32_t)response[18] << 16) |
                   ((uint32_t)response[19] << 24);
  memory_value = response[20] | ((uint32_t)response[21] << 8) |
                 ((uint32_t)response[22] << 16) |
                 ((uint32_t)response[23] << 24);
  if (memory_address != 0x40500000u)
    {
      syslog(LOG_ERR, "WIFI: AIC response address mismatch %08" PRIx32
             "\n", memory_address);
      return -EPROTO;
    }

  syslog(LOG_INFO,
         "WIFI: AIC bulk message passed, [40500000]=%08" PRIx32
         " chip-id=%02" PRIx32 "\n",
         memory_value, (memory_value >> 16) & 0xffu);
  return 0;
}

static int luckfox_wifi_enumerate_child(unsigned int port,
                                        unsigned int address,
                                        uint32_t portstatus)
{
  uint8_t setup[8];
  uint8_t descriptor[18];
  uint8_t config[9];
  uint8_t fullconfig[RK3506_USB_DMA_SIZE];
  unsigned int actual;
  unsigned int initial_mps;
  unsigned int mps;
  uint16_t vid;
  uint16_t pid;
  unsigned int totallength;
  int ret;

  if ((portstatus & USB_HUB_PORT_HIGH_SPEED) != 0)
    {
      initial_mps = 64;
    }
  else if ((portstatus & USB_HUB_PORT_LOW_SPEED) != 0)
    {
      syslog(LOG_WARNING,
             "WIFI: hub port %u is low speed; DWC2 split is not enabled\n",
             port);
      return -ENOTSUP;
    }
  else
    {
      syslog(LOG_WARNING,
             "WIFI: hub port %u is full speed; DWC2 split is not enabled\n",
             port);
      return -ENOTSUP;
    }

  memset(setup, 0, sizeof(setup));
  setup[0] = 0x80;
  setup[1] = USB_REQ_GET_DESCRIPTOR;
  setup[3] = USB_DESC_DEVICE;
  setup[6] = 8;
  ret = luckfox_dwc2_control(0, initial_mps, setup, descriptor, 8, &actual);
  if (ret < 0 || actual != 8 || descriptor[1] != USB_DESC_DEVICE)
    {
      syslog(LOG_ERR,
             "WIFI: hub port %u initial descriptor failed: %d/%u\n",
             port, ret, actual);
      return ret < 0 ? ret : -EIO;
    }

  mps = descriptor[7];
  if (mps != 64)
    {
      syslog(LOG_ERR, "WIFI: hub port %u invalid HS EP0 size %u\n",
             port, mps);
      return -EPROTO;
    }

  ret = luckfox_usb_no_data_request(0, mps, 0x00,
                                     USB_REQ_SET_ADDRESS, address, 0);
  if (ret < 0)
    {
      syslog(LOG_ERR, "WIFI: hub port %u SET_ADDRESS failed: %d\n",
             port, ret);
      return ret;
    }

  nxsig_usleep(10 * 1000);

  memset(setup, 0, sizeof(setup));
  setup[0] = 0x80;
  setup[1] = USB_REQ_GET_DESCRIPTOR;
  setup[3] = USB_DESC_DEVICE;
  setup[6] = sizeof(descriptor);
  ret = luckfox_dwc2_control(address, mps, setup, descriptor,
                              sizeof(descriptor), &actual);
  if (ret < 0 || actual != sizeof(descriptor) || descriptor[0] != 18 ||
      descriptor[1] != USB_DESC_DEVICE)
    {
      syslog(LOG_ERR, "WIFI: hub port %u full descriptor failed: %d/%u\n",
             port, ret, actual);
      return ret < 0 ? ret : -EIO;
    }

  vid = descriptor[8] | ((uint16_t)descriptor[9] << 8);
  pid = descriptor[10] | ((uint16_t)descriptor[11] << 8);
  syslog(LOG_INFO,
         "WIFI: child port=%u addr=%u VID:PID=%04x:%04x class=%02x "
         "bcdUSB=%x.%02x EP0=%u\n",
         port, address, vid, pid, descriptor[4], descriptor[3],
         descriptor[2], mps);
  syslog(LOG_INFO,
         "WIFI: child descriptor=%02x %02x %02x %02x %02x %02x %02x "
         "%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
         descriptor[0], descriptor[1], descriptor[2], descriptor[3],
         descriptor[4], descriptor[5], descriptor[6], descriptor[7],
         descriptor[8], descriptor[9], descriptor[10], descriptor[11],
         descriptor[12], descriptor[13], descriptor[14], descriptor[15],
         descriptor[16], descriptor[17]);

  memset(setup, 0, sizeof(setup));
  setup[0] = 0x80;
  setup[1] = USB_REQ_GET_DESCRIPTOR;
  setup[3] = USB_DESC_CONFIG;
  setup[6] = sizeof(config);
  ret = luckfox_dwc2_control(address, mps, setup, config,
                              sizeof(config), &actual);
  if (ret < 0 || actual != sizeof(config) || config[1] != USB_DESC_CONFIG)
    {
      syslog(LOG_ERR, "WIFI: hub port %u config header failed: %d/%u\n",
             port, ret, actual);
      return ret < 0 ? ret : -EIO;
    }

  syslog(LOG_INFO,
         "WIFI: child port=%u enumeration passed, config=%u interfaces=%u "
         "total=%u\n", port, config[5], config[4],
         config[2] | ((unsigned int)config[3] << 8));

  if (vid != 0xa69c || pid != 0x88dc)
    {
      syslog(LOG_WARNING,
             "WIFI: child port=%u is not the expected AIC8800DC\n", port);
      return 0;
    }

  totallength = config[2] | ((unsigned int)config[3] << 8);
  if (totallength < sizeof(config) || totallength > sizeof(fullconfig))
    {
      syslog(LOG_ERR, "WIFI: invalid AIC config length %u\n", totallength);
      return -E2BIG;
    }

  memset(setup, 0, sizeof(setup));
  setup[0] = 0x80;
  setup[1] = USB_REQ_GET_DESCRIPTOR;
  setup[3] = USB_DESC_CONFIG;
  setup[6] = totallength & 0xffu;
  setup[7] = totallength >> 8;
  ret = luckfox_dwc2_control(address, mps, setup, fullconfig,
                              totallength, &actual);
  if (ret < 0 || actual != totallength)
    {
      syslog(LOG_ERR, "WIFI: full AIC config read failed: %d/%u/%u\n",
             ret, actual, totallength);
      return ret < 0 ? ret : -EIO;
    }

  ret = luckfox_wifi_parse_aic_config(fullconfig, totallength);
  if (ret < 0)
    {
      return ret;
    }

  ret = luckfox_usb_no_data_request(address, mps, 0x00,
                                     USB_REQ_SET_CONFIGURATION,
                                     config[5], 0);
  if (ret < 0)
    {
      syslog(LOG_ERR, "WIFI: AIC SET_CONFIGURATION failed: %d\n", ret);
      return ret;
    }

  nxsig_usleep(10 * 1000);
  syslog(LOG_INFO,
         "WIFI: AIC8800DC configured at address %u; bulk stage ready\n",
         address);
  return luckfox_wifi_aic_read_chipid(address);
}

static int luckfox_wifi_scan_hub(unsigned int hubaddr, unsigned int hubmps,
                                 unsigned int configuration)
{
  uint8_t setup[8];
  uint8_t hubdesc[9];
  uint32_t status;
  unsigned int actual;
  unsigned int nports;
  unsigned int connected = 0;
  unsigned int address = hubaddr + 1;
  unsigned int port;
  unsigned int retry;
  int childret;
  int ret;

  ret = luckfox_usb_no_data_request(hubaddr, hubmps, 0x00,
                                     USB_REQ_SET_CONFIGURATION,
                                     configuration, 0);
  if (ret < 0)
    {
      syslog(LOG_ERR, "WIFI: hub SET_CONFIGURATION failed: %d\n", ret);
      return ret;
    }

  nxsig_usleep(10 * 1000);

  memset(setup, 0, sizeof(setup));
  setup[0] = 0xa0; /* Hub class descriptor. */
  setup[1] = USB_REQ_GET_DESCRIPTOR;
  setup[3] = USB_DESC_HUB;
  setup[6] = sizeof(hubdesc);
  ret = luckfox_dwc2_control(hubaddr, hubmps, setup, hubdesc,
                              sizeof(hubdesc), &actual);
  if (ret < 0 || actual < 7 || hubdesc[1] != USB_DESC_HUB)
    {
      syslog(LOG_ERR, "WIFI: hub descriptor read failed: %d/%u\n",
             ret, actual);
      return ret < 0 ? ret : -EIO;
    }

  nports = hubdesc[2];
  if (nports == 0 || nports > 8)
    {
      syslog(LOG_ERR, "WIFI: invalid hub port count %u\n", nports);
      return -EPROTO;
    }

  syslog(LOG_INFO,
         "WIFI: USB hub ports=%u characteristics=%02x%02x pwr-good=%u ms\n",
         nports, hubdesc[4], hubdesc[3], hubdesc[5] * 2u);

  for (port = 1; port <= nports; port++)
    {
      ret = luckfox_usb_hub_feature(hubaddr, hubmps, port, true,
                                     USB_HUB_FEAT_PORT_POWER);
      if (ret < 0)
        {
          syslog(LOG_ERR, "WIFI: hub port %u power failed: %d\n", port, ret);
          return ret;
        }
    }

  nxsig_usleep((hubdesc[5] * 2u + 20u) * 1000u);

  for (port = 1; port <= nports; port++)
    {
      ret = luckfox_usb_hub_port_status(hubaddr, hubmps, port, &status);
      if (ret < 0)
        {
          syslog(LOG_ERR, "WIFI: hub port %u status failed: %d\n", port, ret);
          return ret;
        }

      syslog(LOG_INFO, "WIFI: hub port %u status=%08" PRIx32 "\n",
             port, status);
      if ((status & USB_HUB_PORT_CONNECTION) == 0)
        {
          continue;
        }

      connected++;
      luckfox_usb_hub_feature(hubaddr, hubmps, port, false,
                               USB_HUB_FEAT_C_CONNECTION);
      ret = luckfox_usb_hub_feature(hubaddr, hubmps, port, true,
                                     USB_HUB_FEAT_PORT_RESET);
      if (ret < 0)
        {
          syslog(LOG_ERR, "WIFI: hub port %u reset request failed: %d\n",
                 port, ret);
          return ret;
        }

      for (retry = 0; retry < 50; retry++)
        {
          nxsig_usleep(10 * 1000);
          ret = luckfox_usb_hub_port_status(hubaddr, hubmps, port, &status);
          if (ret < 0)
            {
              return ret;
            }

          if ((status & USB_HUB_PORT_RESET) == 0 &&
              (status & USB_HUB_PORT_ENABLE) != 0)
            {
              break;
            }
        }

      syslog(LOG_INFO, "WIFI: hub port %u reset status=%08" PRIx32 "\n",
             port, status);
      if (retry == 50)
        {
          syslog(LOG_ERR, "WIFI: hub port %u reset timed out\n", port);
          return -ETIMEDOUT;
        }

      luckfox_usb_hub_feature(hubaddr, hubmps, port, false,
                               USB_HUB_FEAT_C_ENABLE);
      luckfox_usb_hub_feature(hubaddr, hubmps, port, false,
                               USB_HUB_FEAT_C_RESET);

      childret = luckfox_wifi_enumerate_child(port, address, status);
      if (childret == 0)
        {
          address++;
        }
      else if (childret != -ENOTSUP)
        {
          return childret;
        }
    }

  if (connected == 0)
    {
      syslog(LOG_ERR, "WIFI: USB hub has no connected downstream device\n");
      return -ENODEV;
    }

  syslog(LOG_INFO, "WIFI: USB hub scan passed, connected=%u enumerated=%u\n",
         connected, address - hubaddr - 1u);
  return 0;
}

static int luckfox_wifi_enumerate(void)
{
  uint8_t setup[8];
  uint8_t descriptor[18];
  uint8_t config[9];
  unsigned int actual;
  unsigned int initial_mps;
  unsigned int mps;
  uint16_t vid;
  uint16_t pid;
  int ret;

  ret = luckfox_dwc2_core_initialize();
  if (ret < 0)
    {
      return ret;
    }

  memset(setup, 0, sizeof(setup));
  setup[0] = 0x80;
  setup[1] = USB_REQ_GET_DESCRIPTOR;
  setup[3] = USB_DESC_DEVICE;
  setup[6] = 8;

  /* DWC2 HPRT speed encoding is 0=high, 1=full, 2=low.  USB 2.0 high-speed
   * EP0 is always 64 bytes.  Like U-Boot, use a 64-byte initial guess for
   * non-low-speed devices until bMaxPacketSize0 is available.
   */

  initial_mps = (((getreg32(DWC2_HPRT0) & DWC2_HPRT_SPEED_MASK) >>
                  DWC2_HPRT_SPEED_SHIFT) == 2u) ? 8u : 64u;
  ret = luckfox_dwc2_control(0, initial_mps, setup, descriptor, 8, &actual);
  if (ret < 0 || actual != 8 || descriptor[1] != USB_DESC_DEVICE)
    {
      syslog(LOG_ERR, "WIFI: first USB descriptor read failed: %d/%u\n",
             ret, actual);
      return ret < 0 ? ret : -EIO;
    }

  mps = descriptor[7];
  if (mps != 8 && mps != 16 && mps != 32 && mps != 64)
    {
      syslog(LOG_ERR, "WIFI: invalid endpoint-zero packet size %u\n", mps);
      return -EPROTO;
    }

  memset(setup, 0, sizeof(setup));
  setup[1] = USB_REQ_SET_ADDRESS;
  setup[2] = 1;
  ret = luckfox_dwc2_control(0, mps, setup, NULL, 0, &actual);
  if (ret < 0)
    {
      syslog(LOG_ERR, "WIFI: USB SET_ADDRESS failed: %d\n", ret);
      return ret;
    }

  nxsig_usleep(10 * 1000);

  memset(setup, 0, sizeof(setup));
  setup[0] = 0x80;
  setup[1] = USB_REQ_GET_DESCRIPTOR;
  setup[3] = USB_DESC_DEVICE;
  setup[6] = sizeof(descriptor);
  ret = luckfox_dwc2_control(1, mps, setup, descriptor,
                              sizeof(descriptor), &actual);
  if (ret < 0 || actual != sizeof(descriptor) || descriptor[0] != 18 ||
      descriptor[1] != USB_DESC_DEVICE)
    {
      syslog(LOG_ERR, "WIFI: full USB descriptor read failed: %d/%u\n",
             ret, actual);
      return ret < 0 ? ret : -EIO;
    }

  vid = descriptor[8] | ((uint16_t)descriptor[9] << 8);
  pid = descriptor[10] | ((uint16_t)descriptor[11] << 8);
  syslog(LOG_INFO,
         "WIFI: USB device VID:PID=%04x:%04x bcdUSB=%x.%02x EP0=%u\n",
         vid, pid, descriptor[3], descriptor[2], mps);
  syslog(LOG_INFO,
         "WIFI: descriptor=%02x %02x %02x %02x %02x %02x %02x %02x "
         "%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
         descriptor[0], descriptor[1], descriptor[2], descriptor[3],
         descriptor[4], descriptor[5], descriptor[6], descriptor[7],
         descriptor[8], descriptor[9], descriptor[10], descriptor[11],
         descriptor[12], descriptor[13], descriptor[14], descriptor[15],
         descriptor[16], descriptor[17]);

  memset(setup, 0, sizeof(setup));
  setup[0] = 0x80;
  setup[1] = USB_REQ_GET_DESCRIPTOR;
  setup[3] = USB_DESC_CONFIG;
  setup[6] = sizeof(config);
  ret = luckfox_dwc2_control(1, mps, setup, config, sizeof(config), &actual);
  if (ret < 0 || actual != sizeof(config) || config[1] != USB_DESC_CONFIG)
    {
      syslog(LOG_ERR, "WIFI: USB configuration header read failed: %d/%u\n",
             ret, actual);
      return ret < 0 ? ret : -EIO;
    }

  syslog(LOG_INFO,
         "WIFI: USB enumeration passed, config=%u interfaces=%u total=%u\n",
         config[5], config[4], config[2] | ((unsigned int)config[3] << 8));

  if (descriptor[4] == 0x09)
    {
      return luckfox_wifi_scan_hub(1, mps, config[5]);
    }

  return 0;
}
#endif

static void luckfox_wifi_enable_clocks(void)
{
  /* Gate bits are active high.  Rockchip write-mask writes with a zero
   * value therefore enable the selected clocks.
   */

  putreg32(RK3506_WRITE_MASK(1u << 4, 0), RK3506_CRU_GATE_CON01);
  putreg32(RK3506_WRITE_MASK(0x0f00u, 0), RK3506_CRU_GATE_CON07);
}

static void luckfox_wifi_enable_phy(void)
{
  uint32_t regval;

  /* Match the RK3506 Linux PHY tuning: keep the differential receiver on
   * while suspended, select the 425 mV HS eye setting, and use TX driver
   * FS/LS data for the PHY1 line-state indication.
   */

  regval = getreg32(RK3506_PHY1_TUNE);
  regval &= ~((1u << 2) | (7u << 4));
  regval |= 5u << 4;
  putreg32(regval, RK3506_PHY1_TUNE);

  regval = getreg32(RK3506_PHY1_LINESEL);
  regval &= ~(15u << 3);
  regval |= 3u << 3;
  putreg32(regval, RK3506_PHY1_LINESEL);

  /* Enable the PHY's 480 MHz clock output (field bits 7:2 = 0x27), then
   * leave USB2 host port suspend by writing enable value zero to GRF
   * USBPHY_CON1 bits 8:0.
   */

  regval = getreg32(RK3506_PHY_CLKOUT);
  regval &= ~(0x3fu << 2);
  regval |= 0x27u << 2;
  putreg32(regval, RK3506_PHY_CLKOUT);
  nxsig_usleep(1300);

  putreg32(RK3506_WRITE_MASK(0x01ffu, 0), RK3506_GRF_USBPHY_CON1);
}

int luckfox_wifi_probe_initialize(void)
{
  uint32_t snpsid;
  uint32_t status;
  uint32_t hprt;
  unsigned int linestate = 0;
  unsigned int hstdet;
  unsigned int i;
  int ret;

  ret = rk3506_gpio1_config_output(RK3506_WIFI_POWER_PIN, false);
  if (ret < 0)
    {
      return ret;
    }

  nxsig_usleep(20 * 1000);
  luckfox_wifi_enable_clocks();
  luckfox_wifi_enable_phy();

  rk3506_gpio1_write(RK3506_WIFI_POWER_PIN, true);
  syslog(LOG_INFO,
         "WIFI: AIC8800DC power enabled on GPIO1_C5 (USB2 OTG1)\n");

  /* AIC8800DC boots its USB ROM after power-on.  Poll the independent PHY
   * line-state indication so this checkpoint does not depend on a complete
   * DWC2 host driver yet.
   */

  for (i = 0; i < 100; i++)
    {
      status = getreg32(RK3506_GRF_USBPHY_STATUS);
      linestate = (status >> 12) & 3u;
      if (linestate != 0)
        {
          break;
        }

      nxsig_usleep(10 * 1000);
    }

  snpsid = getreg32(DWC2_GSNPSID);
  hprt = getreg32(DWC2_HPRT0);
  status = getreg32(DWC2_GINTSTS);
  hstdet = (getreg32(RK3506_GRF_USBPHY_STATUS) >> 15) & 1u;

  syslog(LOG_INFO,
         "WIFI: OTG1 GSNPSID=%08" PRIx32 " GINTSTS=%08" PRIx32
         " HPRT=%08" PRIx32 "\n",
         snpsid, status, hprt);
  syslog(LOG_INFO,
         "WIFI: USB PHY1 linestate=%u hstdet=%u after %u ms\n",
         linestate, hstdet, i * 10);

  if ((snpsid & DWC2_ID_MASK) != DWC2_OTG_ID)
    {
      syslog(LOG_ERR, "WIFI: invalid DWC2 core ID\n");
      return -ENODEV;
    }

  if (linestate == 0)
    {
      syslog(LOG_WARNING,
             "WIFI: no USB device pull-up detected; check module power\n");
      return -ENODEV;
    }

  syslog(LOG_INFO,
         "WIFI: hardware checkpoint passed; AIC USB device is present\n");

#ifdef CONFIG_RK3506_AIC8800DC_ENUM_PROBE
  return luckfox_wifi_enumerate();
#else
  return 0;
#endif
}

#endif /* CONFIG_RK3506_AIC8800DC_PROBE */
