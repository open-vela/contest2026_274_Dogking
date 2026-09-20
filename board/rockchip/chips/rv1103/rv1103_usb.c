/****************************************************************************
 * vendor/rockchip/chips/rv1103/rv1103_usb.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <errno.h>
#include <syslog.h>
#include <unistd.h>

#include <nuttx/arch.h>

#include <arch/chip/rv1103_usb.h>

#include "arm_internal.h"

#ifdef CONFIG_RV1103_USB_DWC3_CHECKPOINT

/* These addresses and bits are taken from the official RV1106 common DTS,
 * clk-rv1106.c and phy-rockchip-inno-usb2.c.  RV1103 uses the same block.
 */

#define RV1103_CRU_BASE                  0xff3a0000u
#define RV1103_GRF_BASE                  0xff000000u
#define RV1103_USB2PHY_BASE              0xff3e0000u
#define RV1103_DWC3_BASE                 0xffb00000u

#define RV1103_PERICLKGATE_CON4          (RV1103_CRU_BASE + 0x12810u)
#define RV1103_PERICLKGATE_CON5          (RV1103_CRU_BASE + 0x12814u)
#define RV1103_PERISOFTRST_CON4          (RV1103_CRU_BASE + 0x12a10u)
#define RV1103_PERISOFTRST_CON5          (RV1103_CRU_BASE + 0x12a14u)

#define RV1103_USBOTG_CLOCKS             ((1u << 7) | (1u << 8) | (1u << 9))
#define RV1103_USBPHY_CLOCKS             ((1u << 1) | (1u << 2))
#define RV1103_SRST_A_USBOTG             (1u << 7)
#define RV1103_USBPHY_RESETS             ((1u << 1) | (1u << 2) | (1u << 3))

#define RV1103_GRF_USBPHY_CON0           (RV1103_GRF_BASE + 0x0050u)
#define RV1103_GRF_USBPHY_CON1           (RV1103_GRF_BASE + 0x0058u)
#define RV1103_GRF_USBPHY_STATUS         (RV1103_GRF_BASE + 0x0060u)
#define RV1103_USBPHY_SUSPEND_MASK       0x01ffu
#define RV1103_USBPHY_480M_CLK_BIT       (1u << 4)

#define DWC3_GCTL                        (RV1103_DWC3_BASE + 0xc110u)
#define DWC3_GSTS                        (RV1103_DWC3_BASE + 0xc118u)
#define DWC3_GSNPSID                     (RV1103_DWC3_BASE + 0xc120u)
#define DWC3_GHWPARAMS0                  (RV1103_DWC3_BASE + 0xc140u)
#define DWC3_GHWPARAMS1                  (RV1103_DWC3_BASE + 0xc144u)
#define DWC3_GUSB2PHYCFG0                (RV1103_DWC3_BASE + 0xc200u)
#define DWC3_DSTS                        (RV1103_DWC3_BASE + 0xc70cu)

#define DWC3_GSNPSID_MASK                0xffff0000u
#define DWC3_GSNPSID_DWC3                0x55330000u
#define DWC3_GCTL_PRTCAP_MASK            (3u << 12)
#define DWC3_GCTL_PRTCAP_DEVICE          (2u << 12)
#define DWC3_GUSB2PHYCFG_SUSPHY          (1u << 6)
#define DWC3_GUSB2PHYCFG_U2_FREECLK      (1u << 30)
#define DWC3_GUSB2PHYCFG_ENBLSLPM        (1u << 8)

static void rv1103_hiword_update(uintptr_t addr, uint32_t mask,
                                 uint32_t value)
{
  putreg32((mask << 16) | (value & mask), addr);
}

static void rv1103_usb2phy_tune(void)
{
  uint32_t regval;

  /* Match the official Linux RV1106 tuning table. */

  regval  = getreg32(RV1103_USB2PHY_BASE + 0x30u);
  regval  = (regval & ~7u) | 7u;
  putreg32(regval, RV1103_USB2PHY_BASE + 0x30u);

  regval  = getreg32(RV1103_USB2PHY_BASE + 0x40u);
  regval  = (regval & ~(7u << 3)) | (3u << 3);
  putreg32(regval, RV1103_USB2PHY_BASE + 0x40u);

  regval  = getreg32(RV1103_USB2PHY_BASE + 0x64u);
  regval &= ~(15u << 3);
  putreg32(regval, RV1103_USB2PHY_BASE + 0x64u);

  modifyreg32(RV1103_USB2PHY_BASE + 0x100u, 1u << 6, 0);

  regval  = getreg32(RV1103_USB2PHY_BASE + 0x11cu);
  regval  = (regval & ~31u) | 0x17u;
  putreg32(regval, RV1103_USB2PHY_BASE + 0x11cu);

  regval  = getreg32(RV1103_USB2PHY_BASE + 0x124u);
  regval  = (regval & ~(7u << 2)) | (3u << 2);
  putreg32(regval, RV1103_USB2PHY_BASE + 0x124u);

  regval  = getreg32(RV1103_USB2PHY_BASE + 0x1a4u);
  regval  = (regval & ~(15u << 4)) | (1u << 4);
  putreg32(regval, RV1103_USB2PHY_BASE + 0x1a4u);

  regval  = getreg32(RV1103_USB2PHY_BASE + 0x1b4u);
  regval  = (regval & ~(15u << 4)) | (1u << 4);
  putreg32(regval, RV1103_USB2PHY_BASE + 0x1b4u);

  modifyreg32(RV1103_USB2PHY_BASE + 0x70u, 0, 1u << 2);
}

int rv1103_usb_dwc3_checkpoint(void)
{
  uint32_t snpsid;
  uint32_t gctl;
  uint32_t phycfg;
  uint32_t utmi;

  /* Rockchip clock gates are active high: writing zero with a high-word
   * mask enables the gate.  Assert all USB resets only after clocks exist.
   */

  rv1103_hiword_update(RV1103_PERICLKGATE_CON4,
                       RV1103_USBOTG_CLOCKS, 0);
  rv1103_hiword_update(RV1103_PERICLKGATE_CON5,
                       RV1103_USBPHY_CLOCKS, 0);
  rv1103_hiword_update(RV1103_PERISOFTRST_CON4,
                       RV1103_SRST_A_USBOTG, RV1103_SRST_A_USBOTG);
  rv1103_hiword_update(RV1103_PERISOFTRST_CON5,
                       RV1103_USBPHY_RESETS, RV1103_USBPHY_RESETS);
  up_udelay(20);

  rv1103_hiword_update(RV1103_PERISOFTRST_CON5,
                       RV1103_USBPHY_RESETS, 0);
  up_udelay(100);

  rv1103_usb2phy_tune();

  /* Enable the PHY 480 MHz output and leave suspend mode. */

  rv1103_hiword_update(RV1103_GRF_USBPHY_CON1,
                       RV1103_USBPHY_480M_CLK_BIT,
                       RV1103_USBPHY_480M_CLK_BIT);
  rv1103_hiword_update(RV1103_GRF_USBPHY_CON0,
                       RV1103_USBPHY_SUSPEND_MASK, 0);
  usleep(2000);

  rv1103_hiword_update(RV1103_PERISOFTRST_CON4,
                       RV1103_SRST_A_USBOTG, 0);
  up_udelay(100);

  snpsid = getreg32(DWC3_GSNPSID);
  if ((snpsid & DWC3_GSNPSID_MASK) != DWC3_GSNPSID_DWC3)
    {
      syslog(LOG_ERR, "ERROR: USB: invalid DWC3 GSNPSID=%08lx\n",
             (unsigned long)snpsid);
      return -ENODEV;
    }

  /* Pico Mini is wired and described as a USB peripheral.  Select device
   * capability without starting the gadget controller in this checkpoint.
   */

  gctl  = getreg32(DWC3_GCTL);
  gctl &= ~DWC3_GCTL_PRTCAP_MASK;
  gctl |= DWC3_GCTL_PRTCAP_DEVICE;
  putreg32(gctl, DWC3_GCTL);

  phycfg  = getreg32(DWC3_GUSB2PHYCFG0);
  phycfg &= ~(DWC3_GUSB2PHYCFG_SUSPHY |
              DWC3_GUSB2PHYCFG_U2_FREECLK |
              DWC3_GUSB2PHYCFG_ENBLSLPM);
  putreg32(phycfg, DWC3_GUSB2PHYCFG0);
  up_udelay(100);

  utmi = getreg32(RV1103_GRF_USBPHY_STATUS);
  syslog(LOG_INFO,
         "USB: DWC3 peripheral checkpoint passed, id=%08lx"
         " gctl=%08lx gsts=%08lx\n",
         (unsigned long)snpsid,
         (unsigned long)getreg32(DWC3_GCTL),
         (unsigned long)getreg32(DWC3_GSTS));
  syslog(LOG_INFO,
         "USB: PHY utmi=%08lx bvalid=%lu id=%lu linestate=%lu"
         " phycfg=%08lx dsts=%08lx\n",
         (unsigned long)utmi, (unsigned long)((utmi >> 9) & 1u),
         (unsigned long)((utmi >> 6) & 1u),
         (unsigned long)((utmi >> 4) & 3u),
         (unsigned long)getreg32(DWC3_GUSB2PHYCFG0),
         (unsigned long)getreg32(DWC3_DSTS));
  syslog(LOG_INFO,
         "USB: hwparams=%08lx/%08lx"
         "; gadget endpoint stage ready\n",
         (unsigned long)getreg32(DWC3_GHWPARAMS0),
         (unsigned long)getreg32(DWC3_GHWPARAMS1));
  return 0;
}

#endif
