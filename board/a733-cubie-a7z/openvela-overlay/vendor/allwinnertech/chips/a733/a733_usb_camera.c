/****************************************************************************
 * vendor/allwinnertech/chips/a733/a733_usb_camera.c
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/* USB2/DWC3-xHCI and UVC discovery checkpoint for the Cubie A7Z external
 * USB-C 3.1 port.  USB0 is the power/device-only connector and USB1 belongs
 * to FCU760K, so neither controller may be used for an external camera.
 * Streaming is deliberately gated until the correct xHCI root port has been
 * validated with real hardware.
 */

#include <nuttx/config.h>

#ifdef CONFIG_A733_USB_CAMERA

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>

#include <nuttx/arch.h>
#include <nuttx/cache.h>
#include <nuttx/fs/fs.h>
#include <nuttx/mutex.h>
#include <nuttx/usb/ehci.h>

#include "arm64_internal.h"

#define CCU_BASE                 UINT64_C(0x02002000)
#define CCU_MSI_LITE2            (CCU_BASE + 0x05a4)
#define CCU_AHB_MASTER           (CCU_BASE + 0x05c0)
#define CCU_USB0_PHY             (CCU_BASE + 0x1300)
#define CCU_USB0_HCI             (CCU_BASE + 0x1304)
#define CCU_USB_REF              (CCU_BASE + 0x1340)
#define CCU_USB2_U2_REF          (CCU_BASE + 0x1348)
#define CCU_USB2_SUSPEND         (CCU_BASE + 0x1350)
#define CCU_USB2_MF              (CCU_BASE + 0x1354)
#define CCU_USB2_BGR             (CCU_BASE + 0x135c)
#define CCU_USB2_U3_UTMI         (CCU_BASE + 0x1360)
#define CCU_USB2_U2_PIPE         (CCU_BASE + 0x1364)
#define CCU_SERDES_PHY_CFG       (CCU_BASE + 0x13c0)
#define CCU_SERDES_BGR           (CCU_BASE + 0x13c4)
#define CCU_RES_DCAP             (CCU_BASE + 0x1a00)
#define RTC_DCXO_SERDES          UINT64_C(0x0709016c)
#define EHCI0_BASE               UINT64_C(0x04101000)
#define OHCI0_BASE               UINT64_C(0x04101400)
#define USB0_PMU                 (EHCI0_BASE + 0x800)
#define USB0_PHY_CTRL            (EHCI0_BASE + 0x810)
#define PCK_BASE                 UINT64_C(0x07060000)
#define PCK_USB2                 (PCK_BASE + 0x8000)
#define PCK_POWER                0x000
#define PCK_STATUS               0x008
#define PCK_DEVICE_DELAY0        0x170
#define PCK_DEVICE_DELAY1        0x174
#define PCK_LOGIC_DELAY0         0xc00
#define PCK_LOGIC_DELAY1         0xc04
#define PCK_OFF2ON_DELAY         0xc10
#define R_PIO_BASE               UINT64_C(0x07025000)
#define PL_CFG0                  (R_PIO_BASE + 0x00)
#define PL_CFG1                  (R_PIO_BASE + 0x04)
#define PL_DATA                  (R_PIO_BASE + 0x10)
#define PL_DRV1                  (R_PIO_BASE + 0x18)
#define PL_PULL0                 (R_PIO_BASE + 0x24)
#define R_CCU_BASE               UINT64_C(0x07010000)
#define R_CCU_TWI_BGR            (R_CCU_BASE + 0x019c)
#define S_TWI1_BASE              UINT64_C(0x07084000)
#define TWI_DATA                 (S_TWI1_BASE + 0x08)
#define TWI_CTL                  (S_TWI1_BASE + 0x0c)
#define TWI_STAT                 (S_TWI1_BASE + 0x10)
#define TWI_CLK                  (S_TWI1_BASE + 0x14)
#define TWI_SRST                 (S_TWI1_BASE + 0x18)
#define TWI_LCR                  (S_TWI1_BASE + 0x20)
#define TWI_DRV_CTRL             (S_TWI1_BASE + 0x200)
#define TWI_DRV_CFG              (S_TWI1_BASE + 0x204)
#define TWI_DRV_SLV              (S_TWI1_BASE + 0x208)
#define TWI_DRV_FMT              (S_TWI1_BASE + 0x20c)
#define TWI_DRV_BUS_CTRL         (S_TWI1_BASE + 0x210)
#define TWI_DRV_INT_CTRL         (S_TWI1_BASE + 0x214)
#define TWI_DRV_DMA_CFG          (S_TWI1_BASE + 0x218)
#define TWI_DRV_FIFO_CON         (S_TWI1_BASE + 0x21c)
#define TWI_DRV_SEND_FIFO        (S_TWI1_BASE + 0x300)
#define TWI_DRV_RECV_FIFO        (S_TWI1_BASE + 0x304)
#define TWI_CTL_EN               (1u << 6)
#define TWI_CTL_START            (1u << 5)
#define TWI_CTL_STOP             (1u << 4)
#define TWI_CTL_IFLG             (1u << 3)
#define TWI_CTL_ACK              (1u << 2)
#define TWI_DRV_ENABLE           (1u << 0)
#define TWI_DRV_SOFT_RESET       (1u << 1)
#define TWI_DRV_READ_MODE        (1u << 28)
#define TWI_DRV_RESTART_MODE     (1u << 29)
#define TWI_DRV_START            (1u << 31)
#define TWI_DRV_COMPLETE         (1u << 0)
#define TWI_DRV_ERROR            (1u << 1)
#define TWI_DRV_TX_REQUEST       (1u << 2)
#define TWI_DRV_RX_REQUEST       (1u << 3)
#define TWI_DRV_IRQ_ENABLE       (0x0fu << 16)
#define TWI_DRV_TX_CLEAR         (1u << 5)
#define TWI_DRV_RX_CLEAR         (1u << 22)
#define TWI_DRV_RX_COUNT_SHIFT   16
#define TCPC_ADDR                0x4eu
#define TCPC_VENDOR_ID           0x00u
#define TCPC_PRODUCT_ID          0x02u
#define TCPC_BCD_DEV             0x04u
#define TCPC_ALERT               0x10u
#define TCPC_ALERT_MASK          0x12u
#define TCPC_POWER_STATUS_MASK   0x14u
#define TCPC_FAULT_STATUS_MASK   0x15u
#define TCPC_TCPC_CTRL           0x19u
#define TCPC_ROLE_CTRL           0x1au
#define TCPC_CC_STATUS           0x1du
#define TCPC_POWER_STATUS        0x1eu
#define TCPC_FAULT_STATUS        0x1fu
#define TCPC_COMMAND             0x23u
#define TCPC_CMD_ENABLE_VBUS_DETECT 0x33u
#define ET7304_WORKING_MODE      0x9bu
#define ET7304_I2C_RESET         0x9eu
#define ET7304_SW_RESET          0xa0u
#define ET7304_TCPC_FILTER       0xa1u
#define ET7304_TDRP              0xa2u
#define ET7304_DRP_DUTY          0xa3u
#define USB2_PHY_BASE            UINT64_C(0x06b00000)
#define USB2_ISCR                (USB2_PHY_BASE + 0x00)
#define USB2_PHYCTL              (USB2_PHY_BASE + 0x10)
#define USB2_PHYTUNE             (USB2_PHY_BASE + 0x18)
#define USB2_PHYSTS              (USB2_PHY_BASE + 0x24)
#define USB2_ISCR_FORCE_ID_MASK  (3u << 14)
#define USB2_ISCR_FORCE_ID_HOST  (2u << 14)
#define USB2_ISCR_FORCE_VBUS     (3u << 12)
#define SERDES_SUBSYS_BASE       UINT64_C(0x06c00000)
#define SERDES_USB_BGR           (SERDES_SUBSYS_BASE + 0x08)
#define SERDES_USB_ACLK          (1u << 17)
#define SERDES_USB_HCLK          (1u << 16)
#define SERDES_USB2_PHY_RSTN     (1u << 4)
#define COMBO0_TOP               (SERDES_SUBSYS_BASE + 0x1000)
#define COMBO0_CTRL0             (COMBO0_TOP + 0x00)
#define COMBO0_CTRL1             (COMBO0_TOP + 0x04)
#define COMBO0_CTRL2             (COMBO0_TOP + 0x08)
#define COMBO0_CTRL3             (COMBO0_TOP + 0x0c)
#define COMBO0_STATUS            (COMBO0_TOP + 0x900)
#define COMBO0_PHY_BASE          UINT64_C(0x06c80000)
#define COMBO_PIPE_CLOCK_MAP     (SERDES_SUBSYS_BASE + 0x6100)
#define COMBO_PIPE_RX_MAP        (SERDES_SUBSYS_BASE + 0x6104)
#define XHCI2_BASE               UINT64_C(0x06a00000)
#define DWC3_GCTL                (XHCI2_BASE + 0xc110)
#define DWC3_GSNPSID             (XHCI2_BASE + 0xc120)
#define DWC3_GUSB2PHYCFG0        (XHCI2_BASE + 0xc200)
#define DWC3_GUSB3PIPECTL0       (XHCI2_BASE + 0xc2c0)
#define DWC3_GCTL_CORESOFTRESET  (1u << 11)
#define DWC3_GCTL_SOFITPSYNC     (1u << 10)
#define DWC3_PHYCFG_USBTRDTIM_MASK (0xfu << 10)
#define DWC3_PHYCFG_USBTRDTIM_8BIT (9u << 10)
#define DWC3_PHYCFG_ULPI_UTMI    (1u << 4)
#define DWC3_PHYCFG_PHYIF_16BIT  (1u << 3)
#define DWC3_PHYCFG_SUSPHY       (1u << 6)
#define DWC3_PHYCFG_ENBLSLPM     (1u << 8)
#define DWC3_PHY_SOFT_RESET      (1u << 31)
#define DWC3_PIPE_SUSPHY         (1u << 17)
#define DWC3_PHYCFG_PHYSOFTRST   (1u << 31)
#define XHCI_USBCMD              0x00
#define XHCI_USBSTS              0x04
#define XHCI_PAGESIZE            0x08
#define XHCI_CRCR                0x18
#define XHCI_DCBAAP              0x30
#define XHCI_CONFIG              0x38
#define XHCI_PORT_BASE           0x400
#define XHCI_PORT_STRIDE         0x10
#define XHCI_HCSPARAMS2          0x08
#define XHCI_RTSOFF              0x18
#define XHCI_RUNTIME_IRQ0        0x20
#define XHCI_IMAN                0x00
#define XHCI_ERSTSZ              0x08
#define XHCI_ERSTBA              0x10
#define XHCI_ERDP                0x18
#define XHCI_CMD_RUN             (1u << 0)
#define XHCI_CMD_RESET           (1u << 1)
#define XHCI_STS_HALTED          (1u << 0)
#define XHCI_STS_CNR             (1u << 11)
#define XHCI_STS_FATAL           ((1u << 2) | (1u << 12))
#define XHCI_BOOT_SLOTS          8u
#define XHCI_BOOT_TRBS           64u
#define XHCI_BOOT_SCRATCH        8u
#define XHCI_TRB_LINK            (6u << 10)
#define XHCI_TRB_TOGGLE_CYCLE    (1u << 1)
#define PORT_CONNECT             (1u << 0)
#define PORT_ENABLE              (1u << 2)
#define PORT_POWER               (1u << 12)
#define UVC_ADDRESS              1u
#define UVC_CONFIG_MAX           4096u
#define UVC_MODE_MAX             24u
#define UVC_ENDPOINT_MAX         24u
#define UVC_TIMEOUT_MS           750u
#define USB_GET_DESCRIPTOR       6u
#define USB_SET_ADDRESS          5u
#define USB_SET_CONFIGURATION    9u
#define USB_DT_DEVICE            1u
#define USB_DT_CONFIG            2u
#define USB_DT_INTERFACE         4u
#define USB_DT_ENDPOINT          5u
#define USB_DT_CS_INTERFACE      0x24u
#define USB_CLASS_VIDEO          0x0eu
#define USB_SC_VIDEOSTREAMING    0x02u
#define VS_FORMAT_UNCOMPRESSED   0x04u
#define VS_FRAME_UNCOMPRESSED    0x05u
#define VS_FORMAT_MJPEG          0x06u
#define VS_FRAME_MJPEG           0x07u

struct uvc_mode_s
{
  uint8_t format_index;
  uint8_t frame_index;
  uint16_t width;
  uint16_t height;
  uint32_t interval;
  char fourcc[5];
};

struct uvc_endpoint_s
{
  uint8_t interface;
  uint8_t alternate;
  uint8_t address;
  uint8_t type;
  uint16_t raw_maxpacket;
  uint16_t payload;
  uint8_t interval;
};

struct combo_phy_reg_s
{
  uint16_t offset;
  uint16_t value;
};

struct a733_xhci_trb_s
{
  uint64_t parameter;
  uint32_t status;
  uint32_t control;
};

struct a733_xhci_erst_s
{
  uint64_t base;
  uint32_t size;
  uint32_t reserved;
};

#include "a733_combo0_usb_tables.inc"

struct uvc_state_s
{
  mutex_t lock;
  int checkpoint;
  int enumeration;
  int parse;
  uint8_t caplength;
  uint16_t hciversion;
  uint32_t hcsparams;
  uint32_t hcsparams2;
  uint32_t usbcmd;
  uint32_t usbsts;
  int xhci_reset;
  int xhci_start;
  uint16_t xhci_run_polls;
  uint16_t xhci_scratchpads;
  uint32_t xhci_pagesize;
  uint32_t xhci_rtsoff;
  uint32_t xhci_config;
  uint32_t xhci_crcr_before_run;
  uint32_t xhci_crcr_lo;
  uint32_t xhci_crcr_hi;
  uint32_t xhci_dcbaap_lo;
  uint32_t xhci_dcbaap_hi;
  uint32_t xhci_erstsz;
  uint32_t xhci_erdp_lo;
  uint32_t xhci_mfindex;
  int combo_checkpoint;
  int phy_reset_checkpoint;
  uint16_t xhci_cnr_polls;
  uint32_t portsc;
  uint32_t ccu_phy;
  uint32_t ccu_hci;
  uint32_t ccu_u3_utmi;
  uint32_t ccu_u2_pipe;
  uint32_t pmu;
  uint32_t phy_ctrl;
  uint32_t phy_iscr;
  uint32_t pck_power;
  uint32_t pck_status;
  uint32_t ahb_master;
  uint32_t suspend_clock;
  uint32_t serdes_clock;
  uint32_t serdes_bgr;
  uint32_t serdes_usb_bgr;
  uint32_t rtc_serdes;
  uint32_t dwc3_gctl;
  uint32_t dwc3_id;
  uint32_t dwc3_usb2phycfg;
  uint32_t dwc3_usb3pipectl;
  uint32_t usb2_iscr;
  uint32_t usb2_physts;
  uint32_t portsc2;
  int typec_checkpoint;
  uint32_t typec_bgr;
  uint32_t typec_twi_status;
  uint32_t typec_twi_lcr;
  uint32_t typec_drv_ctrl;
  uint32_t typec_drv_int;
  uint32_t typec_drv_fifo;
  uint32_t typec_drv_bus;
  uint16_t typec_vid;
  uint16_t typec_pid;
  uint16_t typec_bcd;
  uint16_t typec_alert;
  uint16_t typec_alert_mask;
  uint8_t typec_role;
  uint8_t typec_cc;
  uint8_t typec_power;
  uint8_t typec_fault;
  uint8_t typec_working_mode;
  uint8_t typec_int_level;
  uint8_t typec_orientation;
  uint16_t typec_cc_polls;
  uint8_t max_ports;
  uint16_t vid;
  uint16_t pid;
  uint16_t bcdusb;
  uint8_t device_class;
  uint8_t configuration;
  uint8_t ep0_maxpacket;
  uint16_t config_length;
  uint8_t streaming_interfaces;
  uint8_t mode_count;
  uint8_t endpoint_count;
  struct uvc_mode_s modes[UVC_MODE_MAX];
  struct uvc_endpoint_s endpoints[UVC_ENDPOINT_MAX];
};

static struct uvc_state_s g_uvc =
{
  .lock = NXMUTEX_INITIALIZER,
  .checkpoint = -EAGAIN,
  .enumeration = -EAGAIN,
  .parse = -EAGAIN,
};

static struct ehci_qh_s g_qh __attribute__((aligned(64)));
static struct ehci_qtd_s g_qtd[3] __attribute__((aligned(64)));
static uint8_t g_setup[8] __attribute__((aligned(64)));
static uint8_t g_config[UVC_CONFIG_MAX] __attribute__((aligned(64)));
static uint64_t g_xhci_dcbaa[XHCI_BOOT_SLOTS + 1]
  __attribute__((aligned(64)));
static uint64_t g_xhci_scratch_array[XHCI_BOOT_SCRATCH]
  __attribute__((aligned(64)));
static uint8_t g_xhci_scratch[XHCI_BOOT_SCRATCH][4096]
  __attribute__((aligned(4096)));
static struct a733_xhci_trb_s g_xhci_command[XHCI_BOOT_TRBS]
  __attribute__((aligned(64)));
static struct a733_xhci_trb_s g_xhci_event[XHCI_BOOT_TRBS]
  __attribute__((aligned(64)));
static struct a733_xhci_erst_s g_xhci_erst __attribute__((aligned(64)));
static char g_report[8192];
static bool g_registered;

static inline void a733_uvc_modifyreg32(uintptr_t address,
                                       uint32_t clearbits,
                                       uint32_t setbits);
static void delay_ms(unsigned int milliseconds);

static void twi_drv_clear_irqs(uint32_t irqs)
{
  uint32_t reg = getreg32(TWI_DRV_INT_CTRL);
  putreg32(reg | (irqs & 0x0fu), TWI_DRV_INT_CTRL);
}

static void twi_drv_prepare(void)
{
  uint32_t reg;

  twi_drv_clear_irqs(0x0f);
  reg = getreg32(TWI_DRV_INT_CTRL);
  putreg32(reg & ~(TWI_DRV_IRQ_ENABLE | 0x0fu), TWI_DRV_INT_CTRL);
  putreg32(getreg32(TWI_DRV_DMA_CFG) & ~((1u << 8) | (1u << 24)),
           TWI_DRV_DMA_CFG);
  putreg32(getreg32(TWI_DRV_FIFO_CON) |
           TWI_DRV_TX_CLEAR | TWI_DRV_RX_CLEAR, TWI_DRV_FIFO_CON);
}

static int twi_drv_wait(uint8_t *data, size_t length)
{
  unsigned int timeout;
  size_t index = 0;

  for (timeout = 0; timeout < 100000; timeout++)
    {
      uint32_t pending = getreg32(TWI_DRV_INT_CTRL) & 0x0fu;
      uint32_t count = (getreg32(TWI_DRV_FIFO_CON) >>
                        TWI_DRV_RX_COUNT_SHIFT) & 0x3fu;

      if ((pending & TWI_DRV_ERROR) != 0)
        {
          uint32_t status = getreg32(TWI_STAT) & 0xffu;
          twi_drv_clear_irqs(pending);
          return status == 0x20 || status == 0x48 ? -ENXIO : -EIO;
        }

      while (data != NULL && index < length && count-- > 0)
        {
          data[index++] = getreg8(TWI_DRV_RECV_FIFO);
        }

      if (data != NULL && index == length &&
          (pending & (TWI_DRV_COMPLETE | TWI_DRV_RX_REQUEST)) != 0)
        {
          twi_drv_clear_irqs(pending);
          return OK;
        }

      if (data == NULL && (pending & TWI_DRV_COMPLETE) != 0)
        {
          twi_drv_clear_irqs(pending);
          return OK;
        }

      up_udelay(1);
    }

  return -ETIMEDOUT;
}

static int tcpc_write(const uint8_t *data, size_t length)
{
  size_t index;
  uint32_t reg;
  int ret;

  if (length == 0 || length > 32)
    {
      return -EINVAL;
    }

  twi_drv_prepare();
  reg = getreg32(TWI_DRV_CTRL);
  reg &= ~(TWI_DRV_READ_MODE | TWI_DRV_RESTART_MODE);
  reg |= TWI_DRV_ENABLE;
  putreg32(reg, TWI_DRV_CTRL);
  putreg32(TCPC_ADDR << 9, TWI_DRV_SLV);
  putreg32(length == 1 ? 1u : (1u << 16) | (length - 1), TWI_DRV_FMT);
  putreg32(1, TWI_DRV_CFG);

  for (index = 0; index < length; index++)
    {
      putreg8(data[index], TWI_DRV_SEND_FIFO);
    }

  putreg32((getreg32(TWI_DRV_INT_CTRL) & ~0x0fu) |
           TWI_DRV_IRQ_ENABLE, TWI_DRV_INT_CTRL);
  putreg32(getreg32(TWI_DRV_CTRL) | TWI_DRV_START, TWI_DRV_CTRL);
  ret = twi_drv_wait(NULL, 0);
  return ret;
}

static int tcpc_read(uint8_t reg, uint8_t *data, size_t length)
{
  uint32_t control;
  int ret;

  if (length == 0 || length > 32)
    {
      return -EINVAL;
    }

  twi_drv_prepare();
  control = getreg32(TWI_DRV_CTRL);
  control &= ~(TWI_DRV_READ_MODE | TWI_DRV_RESTART_MODE);
  control |= TWI_DRV_ENABLE;
  putreg32(control, TWI_DRV_CTRL);
  putreg32((TCPC_ADDR << 9) | (1u << 8), TWI_DRV_SLV);
  putreg32((1u << 16) | length, TWI_DRV_FMT);
  putreg32(1, TWI_DRV_CFG);
  putreg32((getreg32(TWI_DRV_DMA_CFG) & ~(0x3fu << 16)) |
           (32u << 16), TWI_DRV_DMA_CFG);
  putreg8(reg, TWI_DRV_SEND_FIFO);
  putreg32((getreg32(TWI_DRV_INT_CTRL) & ~0x0fu) |
           TWI_DRV_IRQ_ENABLE, TWI_DRV_INT_CTRL);
  putreg32(getreg32(TWI_DRV_CTRL) | TWI_DRV_START, TWI_DRV_CTRL);
  ret = twi_drv_wait(data, length);
  return ret;
}

static int tcpc_write8(uint8_t reg, uint8_t value)
{
  uint8_t data[2] = {reg, value};
  return tcpc_write(data, sizeof(data));
}

static int tcpc_write16(uint8_t reg, uint16_t value)
{
  uint8_t data[3] = {reg, value & 0xffu, value >> 8};
  return tcpc_write(data, sizeof(data));
}

static int typec_host_enable(void)
{
  uint8_t value[2];
  uint32_t reg;
  unsigned int timeout;
  int ret;

  /* Official Cubie A7Z wiring: PL12/PL13 function 3 is S-TWI1 and the
   * ET7304 TCPCI-compatible controller is at 0x4e. */

  reg = getreg32(PL_CFG1);
  reg = (reg & ~((0xfu << 16) | (0xfu << 20))) |
        (3u << 16) | (3u << 20);
  putreg32(reg, PL_CFG1);
  reg = getreg32(PL_DRV1);
  reg = (reg & ~((0xfu << 16) | (0xfu << 20))) |
        (2u << 16) | (2u << 20);
  putreg32(reg, PL_DRV1);
  putreg32(getreg32(PL_PULL0) & ~((3u << 24) | (3u << 26)), PL_PULL0);
  a733_uvc_modifyreg32(R_CCU_TWI_BGR, 0, (1u << 17) | (1u << 1));

  /* The official A733 Cubie device tree selects the v101 TWI packet/FIFO
   * engine (twi_drv_used=1) for S-TWI1.  Using the legacy byte state
   * machine leaves STAT at 0xf8 on this controller and never emits START. */

  reg = getreg32(TWI_DRV_CTRL) | TWI_DRV_ENABLE | TWI_DRV_SOFT_RESET;
  putreg32(reg, TWI_DRV_CTRL);
  up_udelay(5);
  putreg32(reg & ~TWI_DRV_SOFT_RESET, TWI_DRV_CTRL);
  reg = getreg32(TWI_DRV_BUS_CTRL);
  reg &= ~((0x0fu << 8) | (0x07u << 12) | (1u << 15));
  reg |= (2u << 8) | (3u << 12); /* 24 MHz / (10 * 8 * 3) = 100 kHz. */
  putreg32(reg, TWI_DRV_BUS_CTRL);
  twi_drv_prepare();

  ret = tcpc_read(TCPC_VENDOR_ID, value, 2);
  if (ret < 0)
    {
      goto snapshot;
    }

  g_uvc.typec_vid = value[0] | ((uint16_t)value[1] << 8);
  ret = tcpc_read(TCPC_PRODUCT_ID, value, 2);
  if (ret < 0)
    {
      goto snapshot;
    }

  g_uvc.typec_pid = value[0] | ((uint16_t)value[1] << 8);
  ret = tcpc_read(TCPC_BCD_DEV, value, 2);
  if (ret < 0)
    {
      goto snapshot;
    }

  g_uvc.typec_bcd = value[0] | ((uint16_t)value[1] << 8);

  if (g_uvc.typec_vid != 0x6dcf || g_uvc.typec_pid != 0x1711)
    {
      ret = -ENODEV;
      goto snapshot;
    }

  /* ET7304 is register-compatible with RT1715.  Follow the upstream
   * tcpci_rt1711h probe/init sequence: mask interrupts, issue a software
   * reset, leave shutdown mode, enable automatic idle and program the
   * TCPC filter/DRP timing.  Merely writing ROLE_CONTROL while the reset
   * WORKINGMODE value (0x80, SHUTDOWN_OFF=0) is active leaves both CC
   * comparators disabled and CC_STATUS permanently at zero.
   */

  ret = tcpc_write16(TCPC_ALERT_MASK, 0);
  if (ret >= 0)
    {
      ret = tcpc_write8(ET7304_SW_RESET, 1);
    }

  if (ret >= 0)
    {
      delay_ms(2);
      for (timeout = 0; timeout < 200; timeout++)
        {
          ret = tcpc_read(TCPC_POWER_STATUS, value, 1);
          if (ret < 0 || (value[0] & (1u << 6)) == 0)
            {
              break;
            }

          delay_ms(1);
        }

      if (ret >= 0 && timeout == 200)
        {
          ret = -ETIMEDOUT;
        }
    }

  if (ret >= 0)
    {
      ret = tcpc_write8(TCPC_FAULT_STATUS, 0xff);
    }

  if (ret >= 0)
    {
      ret = tcpc_write8(ET7304_WORKING_MODE, 0x2a);
    }

  if (ret >= 0)
    {
      ret = tcpc_write8(ET7304_I2C_RESET, 0x8f);
    }

  if (ret >= 0)
    {
      ret = tcpc_write8(ET7304_TCPC_FILTER, 0x0f);
    }

  if (ret >= 0)
    {
      ret = tcpc_write8(ET7304_TDRP, 0x04);
    }

  if (ret >= 0)
    {
      ret = tcpc_write16(ET7304_DRP_DUTY, 330);
    }

  /* Fixed DFP/host policy: advertise default-current Rp on CC1 and CC2.
   * ET7304 does not implement the generic TCPCI 0x77 SourceVbusDefault
   * command; VBUS is controlled by the board's PL2 load switch instead.
   */

  if (ret >= 0)
    {
      ret = tcpc_write16(TCPC_ALERT, 0xffff);
    }

  if (ret >= 0)
    {
      ret = tcpc_write8(TCPC_ROLE_CTRL, 0x05);
    }

  if (ret >= 0)
    {
      ret = tcpc_write8(TCPC_POWER_STATUS_MASK, 0);
    }

  if (ret >= 0)
    {
      ret = tcpc_write8(TCPC_COMMAND, TCPC_CMD_ENABLE_VBUS_DETECT);
    }

  if (ret >= 0)
    {
      /* CC_STATUS and POWER_STATUS alerts. */

      ret = tcpc_write16(TCPC_ALERT_MASK, 0x0003);
    }

  /* A passive Type-C plug presents Rd.  Poll until one CC comparator sees
   * it, rather than taking a single snapshot before tCCDebounce expires.
   */

  if (ret >= 0)
    {
      for (timeout = 0; timeout < 1000; timeout++)
        {
          ret = tcpc_read(TCPC_CC_STATUS, value, 1);
          if (ret < 0 || (value[0] & 0x0fu) != 0)
            {
              break;
            }

          delay_ms(1);
        }

      g_uvc.typec_cc_polls = timeout < 1000 ? timeout + 1 : timeout;
    }

snapshot:
  g_uvc.typec_bgr = getreg32(R_CCU_TWI_BGR);
  g_uvc.typec_twi_status = getreg32(TWI_STAT) & 0xffu;
  g_uvc.typec_twi_lcr = getreg32(TWI_LCR);
  g_uvc.typec_drv_ctrl = getreg32(TWI_DRV_CTRL);
  g_uvc.typec_drv_int = getreg32(TWI_DRV_INT_CTRL);
  g_uvc.typec_drv_fifo = getreg32(TWI_DRV_FIFO_CON);
  g_uvc.typec_drv_bus = getreg32(TWI_DRV_BUS_CTRL);
  if (tcpc_read(TCPC_ALERT, value, 2) >= 0)
    {
      g_uvc.typec_alert = value[0] | ((uint16_t)value[1] << 8);
    }

  if (tcpc_read(TCPC_ALERT_MASK, value, 2) >= 0)
    {
      g_uvc.typec_alert_mask = value[0] | ((uint16_t)value[1] << 8);
    }

  if (tcpc_read(TCPC_ROLE_CTRL, value, 1) >= 0)
    {
      g_uvc.typec_role = value[0];
    }

  if (tcpc_read(TCPC_CC_STATUS, value, 1) >= 0)
    {
      g_uvc.typec_cc = value[0];
      if ((value[0] & 3u) != 0 && ((value[0] >> 2) & 3u) == 0)
        {
          g_uvc.typec_orientation = 1;
          tcpc_write8(TCPC_TCPC_CTRL, 0);
        }
      else if (((value[0] >> 2) & 3u) != 0)
        {
          g_uvc.typec_orientation = 2;
          tcpc_write8(TCPC_TCPC_CTRL, 1);
        }
    }

  if (tcpc_read(TCPC_POWER_STATUS, value, 1) >= 0)
    {
      g_uvc.typec_power = value[0];
    }

  if (tcpc_read(TCPC_FAULT_STATUS, value, 1) >= 0)
    {
      g_uvc.typec_fault = value[0];
    }

  if (tcpc_read(ET7304_WORKING_MODE, value, 1) >= 0)
    {
      g_uvc.typec_working_mode = value[0];
    }

  g_uvc.typec_int_level = (getreg32(PL_DATA) >> 3) & 1u;

  return ret;
}

static uint16_t getle16(const uint8_t *p)
{
  return p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t getle32(const uint8_t *p)
{
  return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

static inline void a733_uvc_modifyreg32(uintptr_t address,
                                       uint32_t clearbits,
                                       uint32_t setbits)
{
  putreg32((getreg32(address) & ~clearbits) | setbits, address);
}

static void delay_ms(unsigned int milliseconds)
{
  while (milliseconds-- > 0)
    {
      up_udelay(1000);
    }
}

static int wait_clear(uintptr_t address, uint32_t mask,
                      unsigned int timeout_ms)
{
  while ((getreg32(address) & mask) != 0)
    {
      if (timeout_ms-- == 0)
        {
          return -ETIMEDOUT;
        }

      delay_ms(1);
    }

  return OK;
}

static int dma32(const void *pointer, uint32_t *address)
{
  uintptr_t value = (uintptr_t)pointer;
  if ((value >> 32) != 0)
    {
      return -EOVERFLOW;
    }

  *address = (uint32_t)value;
  return OK;
}

static int qtd_buffer(struct ehci_qtd_s *qtd, const void *buffer,
                      size_t length)
{
  uintptr_t current = (uintptr_t)buffer;
  size_t remaining = length;
  unsigned int index;
  uint32_t address;

  memset(qtd->bpl, 0, sizeof(qtd->bpl));
  for (index = 0; index < 5 && remaining > 0; index++)
    {
      if (dma32((const void *)current, &address) < 0)
        {
          return -EOVERFLOW;
        }

      qtd->bpl[index] = address;
      if (remaining <= 4096u - (current & 4095u))
        {
          return OK;
        }

      remaining -= 4096u - (current & 4095u);
      current = (current + 4096u) & ~(uintptr_t)4095u;
    }

  return remaining == 0 ? OK : -E2BIG;
}

static void usb0_clock_enable(void)
{
  uint32_t reg;

  a733_uvc_modifyreg32(CCU_RES_DCAP, 0, 1u << 3);
  a733_uvc_modifyreg32(CCU_MSI_LITE2, 0, 1u << 0);
  reg = getreg32(CCU_AHB_MASTER);
  putreg32(reg | UINT32_C(0x010000ff) | (1u << 9), CCU_AHB_MASTER);
  reg = getreg32(CCU_USB_REF) & ~(7u << 24);
  putreg32(reg | (1u << 31), CCU_USB_REF);
  a733_uvc_modifyreg32(CCU_USB0_PHY, 0, (1u << 30) | (1u << 31));
  a733_uvc_modifyreg32(CCU_USB0_HCI, 0,
              (1u << 20) | (1u << 16) | (1u << 4) | (1u << 0));
  up_udelay(20);
  a733_uvc_modifyreg32(USB0_PHY_CTRL, 1u << 3, 0);
  a733_uvc_modifyreg32(USB0_PMU, 0,
              (1u << 15) | (1u << 11) | (1u << 9) |
              (1u << 8) | (1u << 0));
  up_udelay(20);
}

static int usb2_power_on(void)
{
  uint32_t value;
  unsigned int retry;

  /* PCK600 USB2 domain timing values are the official sun60iw2 values. */

  putreg32(UINT32_C(0x001f1f1f), PCK_USB2 + PCK_DEVICE_DELAY0);
  putreg32(UINT32_C(0x00001f1f), PCK_USB2 + PCK_DEVICE_DELAY1);
  putreg32(UINT32_C(0x08080808), PCK_USB2 + PCK_LOGIC_DELAY0);
  putreg32(UINT32_C(0x00000808), PCK_USB2 + PCK_LOGIC_DELAY1);
  putreg32(UINT32_C(0x00000008), PCK_USB2 + PCK_OFF2ON_DELAY);

  if ((getreg32(PCK_USB2 + PCK_STATUS) & 0xfu) != 8u)
    {
      value = getreg32(PCK_USB2 + PCK_POWER);
      putreg32((value & ~0xfu) | 8u, PCK_USB2 + PCK_POWER);
      for (retry = 0; retry < 10000; retry++)
        {
          if ((getreg32(PCK_USB2 + PCK_STATUS) & 0xfu) == 8u)
            {
              break;
            }

          up_udelay(1);
        }

      if (retry == 10000)
        {
          return -ETIMEDOUT;
        }
    }

  /* PL2 enables the 5 V source on the USB-C 3.1 connector. */

  value = getreg32(PL_CFG0);
  value = (value & ~(0xfu << 8)) | (1u << 8);
  putreg32(value, PL_CFG0);
  putreg32(getreg32(PL_DATA) | (1u << 2), PL_DATA);

  /* The DWC3 register aperture is behind both the USB-system and SerDes
   * AHB gates.  The latter was missing in v58, which made the whole
   * 0x06a00000/0x06b00000 window read as zero even though PCK_USB2 was on.
   * SUN60IW2 CCU gate writes require the 0x010000ff key in the same write.
   */

  value = getreg32(CCU_AHB_MASTER);
  putreg32(value | UINT32_C(0x010000ff) | (1u << 9) | (1u << 8),
           CCU_AHB_MASTER);

  a733_uvc_modifyreg32(CCU_RES_DCAP, 0, 1u << 3);

  /* Keep U2_REF and MF on sys24M.  The DT explicitly assigns SUSPEND to
   * 24 MHz, so select sys24M (mux=1) and divisor=1 rather than inheriting
   * the 32 kHz reset parent.
   */

  value = getreg32(CCU_USB2_U2_REF);
  putreg32((value & ~(7u << 24)) | (1u << 31), CCU_USB2_U2_REF);
  value = getreg32(CCU_USB2_SUSPEND);
  putreg32((value & ~((1u << 24) | 0x1fu)) |
           (1u << 31) | (1u << 24), CCU_USB2_SUSPEND);
  value = getreg32(CCU_USB2_MF);
  putreg32((value & ~((7u << 24) | 0x1fu)) | (1u << 31), CCU_USB2_MF);

  /* Mirror the official sunxi-cadence-combophy parent initialization:
   * 100 MHz SerDes configuration clock, RTC DCXO_SERDES0, SerDes reset,
   * and the USB ACLK/HCLK plus USB2 PHY reset inside the subsystem top.
   * SuperSpeed lane tuning is intentionally deferred; a USB2 UVC camera
   * only needs the dedicated U2 PHY, while these gates are required for
   * the DWC3/xHCI register block itself to become visible.
   */

  putreg32((1u << 31) | (1u << 24) | 5u, CCU_SERDES_PHY_CFG);
  a733_uvc_modifyreg32(RTC_DCXO_SERDES, 0, 1u << 4);
  a733_uvc_modifyreg32(CCU_SERDES_BGR, 0, 1u << 16);
  a733_uvc_modifyreg32(SERDES_USB_BGR, SERDES_USB2_PHY_RSTN,
                       SERDES_USB_ACLK | SERDES_USB_HCLK);

  /* Apply a real reset edge after all parent clocks are stable. */

  a733_uvc_modifyreg32(CCU_USB2_BGR, 1u << 16, 0);
  up_udelay(10);
  a733_uvc_modifyreg32(CCU_USB2_BGR, 0, 1u << 16);
  up_udelay(20);

  /* Official USB2 PHY sequence: resistance calibration, leave SIDDQ,
   * program the A733 board tune value.
   */

  value = getreg32(UINT64_C(0x03000160));
  putreg32((value & ~1u) | (1u << 10), UINT64_C(0x03000160));
  value = getreg32(UINT64_C(0x03000168));
  putreg32(value & ~UINT32_C(0x0000ff00), UINT64_C(0x03000168));
  a733_uvc_modifyreg32(USB2_PHYCTL, 1u << 3, (1u << 10) | (1u << 5));
  putreg32(UINT32_C(0x143338d6), USB2_PHYTUNE);
  up_udelay(1000);
  return OK;
}

static void combo0_write_table(const struct combo_phy_reg_s *table,
                               size_t count)
{
  size_t index;

  for (index = 0; index < count; index++)
    {
      putreg16(table[index].value,
               COMBO0_PHY_BASE + ((uintptr_t)table[index].offset << 1));
    }
}

static void combo0_usb_analog_config(void)
{
  bool reverse = g_uvc.typec_orientation == 2;

  combo0_write_table(g_combo_phy_common0,
                     sizeof(g_combo_phy_common0) /
                     sizeof(g_combo_phy_common0[0]));
  combo0_write_table(reverse ? g_combo_phy_reverse0 : g_combo_phy_normal0,
                     reverse ? sizeof(g_combo_phy_reverse0) /
                               sizeof(g_combo_phy_reverse0[0]) :
                               sizeof(g_combo_phy_normal0) /
                               sizeof(g_combo_phy_normal0[0]));
  combo0_write_table(g_combo_phy_common1,
                     sizeof(g_combo_phy_common1) /
                     sizeof(g_combo_phy_common1[0]));
  combo0_write_table(reverse ? g_combo_phy_reverse1 : g_combo_phy_normal1,
                     reverse ? sizeof(g_combo_phy_reverse1) /
                               sizeof(g_combo_phy_reverse1[0]) :
                               sizeof(g_combo_phy_normal1) /
                               sizeof(g_combo_phy_normal1[0]));
  combo0_write_table(g_combo_phy_common2,
                     sizeof(g_combo_phy_common2) /
                     sizeof(g_combo_phy_common2[0]));
}

/* Bring up the complete USB3 reference/PIPE path before xHCI handoff. Register
 * meanings and analog parameters are cross-checked against the local A733
 * device tree and vendor Combo PHY driver. */

static int combo0_reference_enable(void)
{
  unsigned int retry;
  uint32_t value;

  /* The vendor Cadence PHY driver releases USB3P1_USB2P0_PHY_RSTN from
   * combo0 USB init, after the generic U2 PHY has left SIDDQ.  Do not inherit
   * a stale bootloader-high value: create the same explicit low-to-high edge
   * here before configuring the Combo PHY.
   */

  a733_uvc_modifyreg32(SERDES_USB_BGR, SERDES_USB2_PHY_RSTN, 0);
  up_udelay(10);
  a733_uvc_modifyreg32(SERDES_USB_BGR, 0, SERDES_USB2_PHY_RSTN);
  up_udelay(20);

  a733_uvc_modifyreg32(COMBO0_CTRL2, (1u << 1) | 1u, 0);
  a733_uvc_modifyreg32(COMBO0_CTRL1, 0,
                       (1u << 24) | (1u << 20) | 1u);
  a733_uvc_modifyreg32(COMBO_PIPE_CLOCK_MAP, 0xfu, 0);
  a733_uvc_modifyreg32(COMBO_PIPE_RX_MAP, 0xfu, 0);
  value = g_uvc.typec_orientation == 2 ? (1u << 12) : 0;
  a733_uvc_modifyreg32(COMBO0_CTRL0, 1u << 12, value);
  combo0_usb_analog_config();
  a733_uvc_modifyreg32(COMBO0_CTRL0, 0, 1u);
  up_udelay(1000);
  a733_uvc_modifyreg32(COMBO0_CTRL0, 0, 1u << 4);
  a733_uvc_modifyreg32(COMBO0_CTRL3, 0, 1u);
  a733_uvc_modifyreg32(COMBO0_CTRL3, 0x3fu << 4, 1u << 4);
  a733_uvc_modifyreg32(COMBO0_CTRL0, 0, 1u << 8);

  for (retry = 0; retry < 100; retry++)
    {
      if ((getreg32(COMBO0_STATUS) & 1u) != 0)
        {
          break;
        }

      delay_ms(1);
    }

  syslog(LOG_INFO, "A733 USB combo v114: ctrl=%08lx/%08lx/%08lx/%08lx status=%08lx maps=%08lx/%08lx ready=%u\n",
         (unsigned long)getreg32(COMBO0_CTRL0),
         (unsigned long)getreg32(COMBO0_CTRL1),
         (unsigned long)getreg32(COMBO0_CTRL2),
         (unsigned long)getreg32(COMBO0_CTRL3),
         (unsigned long)getreg32(COMBO0_STATUS),
         (unsigned long)getreg32(COMBO_PIPE_CLOCK_MAP),
         (unsigned long)getreg32(COMBO_PIPE_RX_MAP), retry < 100);
  return retry < 100 ? OK : -ETIMEDOUT;
}

static void dwc3_usb2_host_init(void)
{
  uint32_t value;

  /* The A733 manual places its application-register aperture at
   * XHCI2_BASE + 0x00100000, which is the dedicated USB2_PHY_BASE mapping.
   * Force ID low (host) and VBUS valid high for the SIE there.  A Type-C
   * receptacle has no legacy ID pin; its TCPC host decision must therefore
   * be reflected explicitly in this application register.  The legacy
   * vendor xHCI glue's
   * +0x10000 APP/PIPE/EXT offsets describe older SoCs and do not control the
   * A733 PHY.
   *
   * This helper only programs the UTMI/host settings.  The caller performs
   * the core reset after the complete Cadence Combo PHY reports ready.
   */

  a733_uvc_modifyreg32(USB2_ISCR, USB2_ISCR_FORCE_ID_MASK,
                       USB2_ISCR_FORCE_ID_HOST |
                       USB2_ISCR_FORCE_VBUS);

  /* The A733 DT carries snps,dis_u2_susphy_quirk.  Keep the USB2 PHY awake
   * during attach detection, then select host capability and the vendor
   * SOFITPSYNC requirement.
   */

  /* Honor both USB2 and USB3 suspend quirks in sun60iw2p1.dtsi. */

  a733_uvc_modifyreg32(DWC3_GUSB2PHYCFG0,
                       DWC3_PHYCFG_SUSPHY | DWC3_PHYCFG_ENBLSLPM |
                       DWC3_PHYCFG_USBTRDTIM_MASK |
                       DWC3_PHYCFG_ULPI_UTMI |
                       DWC3_PHYCFG_PHYIF_16BIT,
                       DWC3_PHYCFG_USBTRDTIM_8BIT);
  a733_uvc_modifyreg32(DWC3_GUSB3PIPECTL0, DWC3_PIPE_SUSPHY, 0);
  value = getreg32(DWC3_GCTL);
  value &= ~UINT32_C(0x00003000);
  value |= UINT32_C(0x00001000) | DWC3_GCTL_SOFITPSYNC;
  putreg32(value, DWC3_GCTL);
  delay_ms(20);
}

static int dwc3_host_handoff_validate(uintptr_t opbase)
{
  uint32_t id;

  /* The local Linux 5.15 dwc3_core_soft_reset() returns immediately when
   * current_dr_role is already HOST.  v112 confirmed why: asserting
   * GCTL.CORESOFTRESET on A733 leaves xHCI CNR set and clears its capability
   * and runtime windows.  Preserve the clean state produced by the CCU reset
   * edge and validate it after the U2 and Combo PHYs are ready.  Do not pulse
   * either DWC3 PHY reset and never issue xHCI HCRST.
   */

  if (wait_clear(opbase + XHCI_USBSTS, XHCI_STS_CNR, 1000) < 0)
    {
      syslog(LOG_ERR,
             "A733 USB host: preserved handoff CNR timeout gctl=%08lx sts=%08lx\n",
             (unsigned long)getreg32(DWC3_GCTL),
             (unsigned long)getreg32(opbase + XHCI_USBSTS));
      return -ETIMEDOUT;
    }

  id = getreg32(DWC3_GSNPSID);
  if (((id & UINT32_C(0xffff0000)) != UINT32_C(0x55330000) &&
       (id & UINT32_C(0xffff0000)) != UINT32_C(0x33310000)) ||
      (getreg32(COMBO0_STATUS) & 1u) == 0)
    {
      syslog(LOG_ERR,
             "A733 USB host: preserved handoff lost readiness id=%08lx combo=%08lx\n",
             (unsigned long)id,
             (unsigned long)getreg32(COMBO0_STATUS));
      return -EIO;
    }

  dwc3_usb2_host_init();
  syslog(LOG_INFO,
         "A733 USB PHY v114: reset-free host handoff passed id=%08lx gctl=%08lx u2=%08lx u3=%08lx iscr=%08lx physts=%08lx\n",
         (unsigned long)id,
         (unsigned long)getreg32(DWC3_GCTL),
         (unsigned long)getreg32(DWC3_GUSB2PHYCFG0),
         (unsigned long)getreg32(DWC3_GUSB3PIPECTL0),
         (unsigned long)getreg32(USB2_ISCR),
         (unsigned long)getreg32(USB2_PHYSTS));
  return OK;
}

static int xhci2_prepare_halted(uintptr_t opbase)
{
  uintptr_t command = opbase + XHCI_USBCMD;
  uintptr_t status = opbase + XHCI_USBSTS;
  unsigned int timeout;

  /* usb2_power_on() already applied the A733 CCU USB2 reset edge.  v106
   * hardware logs prove that a second reset through USBCMD.HCRST removes
   * the complete DWC3/xHCI register aperture.  Retain the clean, halted
   * CCU-reset state after checking CNR, HALTED and the DWC3 identity.
   */

  if (wait_clear(status, XHCI_STS_CNR, 1000) < 0)
    {
      syslog(LOG_ERR, "A733 USB host: handoff CNR timeout\n");
      return -ETIMEDOUT;
    }

  a733_uvc_modifyreg32(command, XHCI_CMD_RUN, 0);
  for (timeout = 0; timeout < 1000; timeout++)
    {
      if ((getreg32(status) & XHCI_STS_HALTED) != 0)
        {
          break;
        }

      delay_ms(1);
    }

  if (timeout == 1000)
    {
      syslog(LOG_ERR, "A733 USB host: halt timeout cmd=%08lx sts=%08lx\n",
             (unsigned long)getreg32(command),
             (unsigned long)getreg32(status));
      return -ETIMEDOUT;
    }

  for (timeout = 0; timeout < 2; timeout++)
    {
      uint32_t id = getreg32(DWC3_GSNPSID);
      uint32_t sts = getreg32(status);
      if ((id & UINT32_C(0xffff0000)) != UINT32_C(0x55330000) &&
          (id & UINT32_C(0xffff0000)) != UINT32_C(0x33310000))
        {
          syslog(LOG_ERR, "A733 USB host: halted handoff lost MMIO\n");
          return -EIO;
        }

      if ((sts & (XHCI_STS_CNR | XHCI_STS_HALTED)) == XHCI_STS_HALTED)
        {
          g_uvc.xhci_cnr_polls = timeout + 1;
          syslog(LOG_INFO,
                 "A733 USB host: CCU-reset halted handoff passed; HCRST suppressed cmd=%08lx sts=%08lx id=%08lx\n",
                 (unsigned long)getreg32(command),
                 (unsigned long)sts, (unsigned long)id);
          return OK;
        }

      delay_ms(1);
    }

  return -ETIMEDOUT;
}

static void xhci2_putreg64(uintptr_t address, uint64_t value)
{
  /* CRCR requires one naturally aligned 64-bit MMIO transaction on this
   * ARM64 controller.  v111 split it into two writes and real hardware read
   * CRCR back as zero even though DCBAAP and ERDP happened to accept it.
   */

  putreg64(value, address);
}

static unsigned int xhci2_scratchpad_count(uint32_t hcsparams2)
{
  return ((hcsparams2 >> 27) & 0x1fu) |
         (((hcsparams2 >> 21) & 0x1fu) << 5);
}

static int xhci2_start_minimal(uintptr_t opbase)
{
  uintptr_t runtime;
  uintptr_t interrupter;
  uintptr_t command = opbase + XHCI_USBCMD;
  uintptr_t status = opbase + XHCI_USBSTS;
  uintptr_t command_pa;
  uintptr_t event_pa;
  uintptr_t erst_pa;
  uintptr_t dcbaa_pa;
  unsigned int slots;
  unsigned int scratchpads;
  unsigned int timeout;
  unsigned int index;

  g_uvc.hcsparams2 = getreg32(XHCI2_BASE + XHCI_HCSPARAMS2);
  g_uvc.xhci_pagesize = getreg32(opbase + XHCI_PAGESIZE);
  g_uvc.xhci_rtsoff = getreg32(XHCI2_BASE + XHCI_RTSOFF) & ~0x1fu;
  scratchpads = xhci2_scratchpad_count(g_uvc.hcsparams2);
  g_uvc.xhci_scratchpads = scratchpads;
  if ((g_uvc.xhci_pagesize & 1u) == 0 ||
      g_uvc.xhci_rtsoff == 0 || scratchpads > XHCI_BOOT_SCRATCH)
    {
      syslog(LOG_ERR,
             "A733 xHCI v114: unsupported page=%08lx rtsoff=%08lx scratch=%u\n",
             (unsigned long)g_uvc.xhci_pagesize,
             (unsigned long)g_uvc.xhci_rtsoff, scratchpads);
      return -ENOTSUP;
    }

  slots = g_uvc.hcsparams & 0xffu;
  if (slots > XHCI_BOOT_SLOTS)
    {
      slots = XHCI_BOOT_SLOTS;
    }

  if (slots == 0)
    {
      return -ENODEV;
    }

  memset(g_xhci_dcbaa, 0, sizeof(g_xhci_dcbaa));
  memset(g_xhci_scratch_array, 0, sizeof(g_xhci_scratch_array));
  memset(g_xhci_command, 0, sizeof(g_xhci_command));
  memset(g_xhci_event, 0, sizeof(g_xhci_event));
  memset(&g_xhci_erst, 0, sizeof(g_xhci_erst));

  for (index = 0; index < scratchpads; index++)
    {
      memset(g_xhci_scratch[index], 0, sizeof(g_xhci_scratch[index]));
      g_xhci_scratch_array[index] =
        (uint64_t)(uintptr_t)g_xhci_scratch[index];
      up_clean_dcache((uintptr_t)g_xhci_scratch[index],
                      (uintptr_t)g_xhci_scratch[index] +
                      sizeof(g_xhci_scratch[index]));
    }

  if (scratchpads > 0)
    {
      g_xhci_dcbaa[0] =
        (uint64_t)(uintptr_t)g_xhci_scratch_array;
    }

  /* This A733 flat kernel maps its static low-RAM DMA objects one-to-one;
   * the existing EHCI path uses the same VA-as-bus-address contract. */

  command_pa = (uintptr_t)g_xhci_command;
  event_pa = (uintptr_t)g_xhci_event;
  erst_pa = (uintptr_t)&g_xhci_erst;
  dcbaa_pa = (uintptr_t)g_xhci_dcbaa;
  if (((command_pa | event_pa | erst_pa | dcbaa_pa) & 0x3fu) != 0)
    {
      return -EFAULT;
    }

  /* An empty command ring starts with cycle state one.  The Link TRB is
   * initially cycle zero, like the generic NuttX xHCI ring implementation,
   * and becomes visible only after commands fill the usable entries. */

  g_xhci_command[XHCI_BOOT_TRBS - 1].parameter = command_pa;
  g_xhci_command[XHCI_BOOT_TRBS - 1].control =
    XHCI_TRB_LINK | XHCI_TRB_TOGGLE_CYCLE;
  g_xhci_erst.base = event_pa;
  g_xhci_erst.size = XHCI_BOOT_TRBS;

  up_clean_dcache((uintptr_t)g_xhci_dcbaa,
                  (uintptr_t)g_xhci_dcbaa + sizeof(g_xhci_dcbaa));
  up_clean_dcache((uintptr_t)g_xhci_scratch_array,
                  (uintptr_t)g_xhci_scratch_array +
                  sizeof(g_xhci_scratch_array));
  up_clean_dcache((uintptr_t)g_xhci_command,
                  (uintptr_t)g_xhci_command + sizeof(g_xhci_command));
  up_clean_dcache((uintptr_t)g_xhci_event,
                  (uintptr_t)g_xhci_event + sizeof(g_xhci_event));
  up_clean_dcache((uintptr_t)&g_xhci_erst,
                  (uintptr_t)&g_xhci_erst + sizeof(g_xhci_erst));

  runtime = XHCI2_BASE + g_uvc.xhci_rtsoff;
  interrupter = runtime + XHCI_RUNTIME_IRQ0;
  putreg32(slots, opbase + XHCI_CONFIG);
  xhci2_putreg64(opbase + XHCI_DCBAAP, dcbaa_pa);
  putreg32(1, interrupter + XHCI_ERSTSZ);
  xhci2_putreg64(interrupter + XHCI_ERDP, event_pa);
  xhci2_putreg64(interrupter + XHCI_ERSTBA, erst_pa);
  xhci2_putreg64(opbase + XHCI_CRCR, command_pa | 1u);
  g_uvc.xhci_crcr_before_run = getreg32(opbase + XHCI_CRCR);
  putreg32(0, interrupter + XHCI_IMAN);

  /* RUN is sufficient for polled attach detection.  Leave interrupts and
   * system-error interrupts disabled until the complete NuttX host driver
   * owns this controller. */

  putreg32(XHCI_CMD_RUN, command);
  for (timeout = 0; timeout < 1000; timeout++)
    {
      uint32_t sts = getreg32(status);
      if ((sts & XHCI_STS_FATAL) != 0)
        {
          syslog(LOG_ERR,
                 "A733 xHCI v114: controller fault cmd=%08lx sts=%08lx\n",
                 (unsigned long)getreg32(command), (unsigned long)sts);
          return -EIO;
        }

      if ((sts & (XHCI_STS_CNR | XHCI_STS_HALTED)) == 0)
        {
          g_uvc.xhci_run_polls = timeout + 1;
          syslog(LOG_INFO,
                 "A733 xHCI v114: running slots=%u scratch=%u page=%08lx rtsoff=%08lx cmd=%08lx sts=%08lx\n",
                 slots, scratchpads,
                 (unsigned long)g_uvc.xhci_pagesize,
                 (unsigned long)g_uvc.xhci_rtsoff,
                 (unsigned long)getreg32(command), (unsigned long)sts);
          return OK;
        }

      delay_ms(1);
    }

  syslog(LOG_ERR, "A733 xHCI v114: RUN timeout cmd=%08lx sts=%08lx\n",
         (unsigned long)getreg32(command),
         (unsigned long)getreg32(status));
  return -ETIMEDOUT;
}

static int xhci2_checkpoint(void)
{
  uintptr_t opbase;
  unsigned int timeout;
  int ret;

  ret = usb2_power_on();
  if (ret < 0)
    {
      return ret;
    }

  g_uvc.caplength = getreg8(XHCI2_BASE);
  g_uvc.hciversion = getreg16(XHCI2_BASE + 2);
  g_uvc.hcsparams = getreg32(XHCI2_BASE + 4);
  g_uvc.max_ports = (uint8_t)((g_uvc.hcsparams >> 24) & 0xffu);
  g_uvc.dwc3_id = getreg32(DWC3_GSNPSID);
  syslog(LOG_INFO, "A733 USB host: before reset cap=%02x ver=%04x hcs=%08lx id=%08lx\n",
         g_uvc.caplength, g_uvc.hciversion,
         (unsigned long)g_uvc.hcsparams, (unsigned long)g_uvc.dwc3_id);
  if (g_uvc.caplength < 0x20 || g_uvc.caplength > 0x80 ||
      ((g_uvc.dwc3_id & UINT32_C(0xffff0000)) != UINT32_C(0x55330000) &&
       (g_uvc.dwc3_id & UINT32_C(0xffff0000)) != UINT32_C(0x33310000)))
    {
      return -ENODEV;
    }

  /* Establish the Type-C host role before exposing the external-VBUS state
   * to DWC3.  Keep xHCI stopped: USBCMD.RUN is only valid after DCBAA,
   * command and event rings exist.
   */

  g_uvc.typec_checkpoint = typec_host_enable();
  if (g_uvc.typec_checkpoint < 0)
    {
      return g_uvc.typec_checkpoint;
    }

  delay_ms(20);

  /* Open the wrapper clocks and disable PHY suspend before asking the
   * Combo PHY to become ready, matching DWC3 phy_setup before phy_init. */

  dwc3_usb2_host_init();
  g_uvc.combo_checkpoint = combo0_reference_enable();
  if (g_uvc.combo_checkpoint < 0)
    {
      syslog(LOG_ERR, "A733 USB host: Combo PHY not ready; handoff stopped\n");
      return g_uvc.combo_checkpoint;
    }

  opbase = XHCI2_BASE + g_uvc.caplength;
  g_uvc.phy_reset_checkpoint = dwc3_host_handoff_validate(opbase);
  if (g_uvc.phy_reset_checkpoint < 0)
    {
      return g_uvc.phy_reset_checkpoint;
    }

  g_uvc.xhci_reset = xhci2_prepare_halted(opbase);
  if (g_uvc.xhci_reset < 0)
    {
      return g_uvc.xhci_reset;
    }

  g_uvc.xhci_start = xhci2_start_minimal(opbase);
  if (g_uvc.xhci_start < 0)
    {
      return g_uvc.xhci_start;
    }

  /* Give the TCPC and camera time to complete CC attach and PHY debounce. */

  for (timeout = 0; timeout < 1500; timeout++)
    {
      g_uvc.portsc = getreg32(opbase + XHCI_PORT_BASE);
      g_uvc.portsc2 = g_uvc.max_ports > 1 ?
                      getreg32(opbase + XHCI_PORT_BASE +
                               XHCI_PORT_STRIDE) : 0;
      if (((g_uvc.portsc | g_uvc.portsc2) & PORT_CONNECT) != 0)
        {
          break;
        }

      delay_ms(1);
    }

  g_uvc.usbcmd = getreg32(opbase + XHCI_USBCMD);
  g_uvc.usbsts = getreg32(opbase + XHCI_USBSTS);
  g_uvc.portsc = getreg32(opbase + XHCI_PORT_BASE);
  g_uvc.portsc2 = g_uvc.max_ports > 1 ?
                  getreg32(opbase + XHCI_PORT_BASE + XHCI_PORT_STRIDE) : 0;
  return ((g_uvc.portsc | g_uvc.portsc2) & PORT_CONNECT) != 0 ?
         OK : -ENODEV;
}

static int async_stop(uintptr_t usbcmd, uintptr_t usbsts)
{
  unsigned int timeout;

  a733_uvc_modifyreg32(usbcmd, EHCI_USBCMD_ASEN, 0);
  for (timeout = 0; timeout < 100; timeout++)
    {
      if ((getreg32(usbsts) & EHCI_USBSTS_ASS) == 0)
        {
          return OK;
        }

      delay_ms(1);
    }

  return -ETIMEDOUT;
}

static int ehci0_checkpoint(void)
{
  uintptr_t opbase;
  uintptr_t usbcmd;
  uintptr_t usbsts;
  uintptr_t portsc;
  uint32_t reg;
  unsigned int timeout;
  int ret;

  g_uvc.caplength = getreg8(EHCI0_BASE);
  g_uvc.hciversion = getreg16(EHCI0_BASE + 2);
  g_uvc.hcsparams = getreg32(EHCI0_BASE + 4);
  if (g_uvc.caplength < 0x10 || g_uvc.caplength > 0x80 ||
      g_uvc.hciversion == 0 || g_uvc.hciversion == 0xffff)
    {
      return -ENODEV;
    }

  opbase = EHCI0_BASE + g_uvc.caplength;
  usbcmd = opbase + EHCI_USBCMD_OFFSET;
  usbsts = opbase + EHCI_USBSTS_OFFSET;
  portsc = opbase + EHCI_PORTSC1_OFFSET;
  a733_uvc_modifyreg32(usbcmd, EHCI_USBCMD_RUN, 0);
  for (timeout = 0; timeout < 100 &&
       (getreg32(usbsts) & EHCI_USBSTS_HALTED) == 0; timeout++)
    {
      delay_ms(1);
    }

  a733_uvc_modifyreg32(usbcmd, 0, EHCI_USBCMD_HCRESET);
  ret = wait_clear(usbcmd, EHCI_USBCMD_HCRESET, 100);
  if (ret < 0)
    {
      return ret;
    }

  putreg32(0, opbase + EHCI_USBINTR_OFFSET);
  putreg32(1, opbase + EHCI_CONFIGFLAG_OFFSET);
  reg = getreg32(portsc) & ~((1u << 1) | (1u << 3) | (1u << 5));
  putreg32(reg | PORT_POWER, portsc);
  a733_uvc_modifyreg32(usbcmd, 0, EHCI_USBCMD_RUN);
  for (timeout = 0; timeout < 1000 &&
       (getreg32(portsc) & PORT_CONNECT) == 0; timeout++)
    {
      delay_ms(1);
    }

  if ((getreg32(portsc) & PORT_CONNECT) == 0)
    {
      return -ENODEV;
    }

  reg = getreg32(portsc) & ~((1u << 1) | (1u << 3) | (1u << 5));
  putreg32(reg | PORT_POWER | EHCI_PORTSC_RESET, portsc);
  delay_ms(50);
  a733_uvc_modifyreg32(portsc, EHCI_PORTSC_RESET, 0);
  delay_ms(20);
  g_uvc.usbcmd = getreg32(usbcmd);
  g_uvc.usbsts = getreg32(usbsts);
  g_uvc.portsc = getreg32(portsc);
  if ((g_uvc.portsc & PORT_CONNECT) == 0)
    {
      return -ENODEV;
    }

  if ((g_uvc.portsc & EHCI_PORTSC_OWNER) != 0)
    {
      return -EOPNOTSUPP;
    }

  return (g_uvc.portsc & PORT_ENABLE) != 0 ? OK : -EIO;
}

static int control(uint8_t devaddr, uint16_t maxpacket,
                   const uint8_t setup[8], void *buffer,
                   size_t length, size_t *actual)
{
  struct ehci_qtd_s *sqtd = &g_qtd[0];
  struct ehci_qtd_s *dqtd = &g_qtd[1];
  struct ehci_qtd_s *zqtd = &g_qtd[2];
  uintptr_t opbase = EHCI0_BASE + g_uvc.caplength;
  uintptr_t usbcmd = opbase + EHCI_USBCMD_OFFSET;
  uintptr_t usbsts = opbase + EHCI_USBSTS_OFFSET;
  uint32_t qhaddr;
  uint32_t saddr;
  uint32_t daddr;
  uint32_t zaddr;
  uint32_t token;
  uint32_t residual = 0;
  unsigned int timeout;
  bool datain = (setup[0] & 0x80u) != 0;
  int ret;

  *actual = 0;
  if (length > UVC_CONFIG_MAX || maxpacket == 0 || maxpacket > 1024)
    {
      return -EINVAL;
    }

  if (dma32(&g_qh, &qhaddr) < 0 || dma32(sqtd, &saddr) < 0 ||
      dma32(dqtd, &daddr) < 0 || dma32(zqtd, &zaddr) < 0)
    {
      return -EOVERFLOW;
    }

  ret = async_stop(usbcmd, usbsts);
  if (ret < 0)
    {
      return ret;
    }

  memset(&g_qh, 0, sizeof(g_qh));
  memset(g_qtd, 0, sizeof(g_qtd));
  memcpy(g_setup, setup, sizeof(g_setup));
  g_qh.hlp = qhaddr | QH_HLP_TYP_QH;
  g_qh.epchar = ((uint32_t)devaddr << QH_EPCHAR_DEVADDR_SHIFT) |
                QH_EPCHAR_EPS_HIGH | QH_EPCHAR_DTC | QH_EPCHAR_H |
                ((uint32_t)maxpacket << QH_EPCHAR_MAXPKT_SHIFT) |
                (8u << QH_EPCHAR_RL_SHIFT);
  g_qh.epcaps = QH_EPCAPS_MULT(1);
  g_qh.overlay.nqp = saddr;
  g_qh.overlay.alt = QH_AQP_T;
  sqtd->nqp = length > 0 ? daddr : zaddr;
  sqtd->alt = QTD_AQP_T;
  sqtd->token = QTD_TOKEN_ACTIVE | QTD_TOKEN_PID_SETUP |
                 (3u << QTD_TOKEN_CERR_SHIFT) |
                 (8u << QTD_TOKEN_NBYTES_SHIFT);
  ret = qtd_buffer(sqtd, g_setup, sizeof(g_setup));
  if (ret < 0)
    {
      return ret;
    }

  if (length > 0)
    {
      dqtd->nqp = zaddr;
      dqtd->alt = datain ? zaddr : QTD_AQP_T;
      dqtd->token = QTD_TOKEN_ACTIVE |
                    (datain ? QTD_TOKEN_PID_IN : QTD_TOKEN_PID_OUT) |
                    (3u << QTD_TOKEN_CERR_SHIFT) |
                    ((uint32_t)length << QTD_TOKEN_NBYTES_SHIFT) |
                    QTD_TOKEN_TOGGLE;
      ret = qtd_buffer(dqtd, buffer, length);
      if (ret < 0)
        {
          return ret;
        }

      if (datain)
        {
          up_flush_dcache((uintptr_t)buffer, (uintptr_t)buffer + length);
        }
      else
        {
          up_clean_dcache((uintptr_t)buffer, (uintptr_t)buffer + length);
        }
    }

  zqtd->nqp = QTD_NQP_T;
  zqtd->alt = QTD_AQP_T;
  zqtd->token = QTD_TOKEN_ACTIVE |
                 (datain ? QTD_TOKEN_PID_OUT : QTD_TOKEN_PID_IN) |
                 (3u << QTD_TOKEN_CERR_SHIFT) | QTD_TOKEN_IOC |
                 QTD_TOKEN_TOGGLE;
  up_clean_dcache((uintptr_t)g_setup,
                  (uintptr_t)g_setup + sizeof(g_setup));
  up_clean_dcache((uintptr_t)g_qtd,
                  (uintptr_t)g_qtd + sizeof(g_qtd));
  up_clean_dcache((uintptr_t)&g_qh,
                  (uintptr_t)&g_qh + sizeof(g_qh));
  putreg32(0, opbase + EHCI_CTRLDSSEGMENT_OFFSET);
  putreg32(qhaddr, opbase + EHCI_ASYNCLISTADDR_OFFSET);
  putreg32(EHCI_INT_ALLINTS, usbsts);
  a733_uvc_modifyreg32(usbcmd, 0,
                       EHCI_USBCMD_ASEN | EHCI_USBCMD_RUN);
  ret = -ETIMEDOUT;
  for (timeout = 0; timeout < UVC_TIMEOUT_MS; timeout++)
    {
      up_invalidate_dcache((uintptr_t)zqtd,
                           (uintptr_t)zqtd + sizeof(*zqtd));
      token = zqtd->token;
      if ((token & QTD_TOKEN_ACTIVE) == 0)
        {
          ret = (token & QTD_TOKEN_ERRORS) != 0 ? -EIO : OK;
          break;
        }

      delay_ms(1);
    }

  async_stop(usbcmd, usbsts);
  if (length > 0)
    {
      up_invalidate_dcache((uintptr_t)dqtd,
                           (uintptr_t)dqtd + sizeof(*dqtd));
      token = dqtd->token;
      if ((token & QTD_TOKEN_ERRORS) != 0)
        {
          ret = -EIO;
        }

      residual = (token & QTD_TOKEN_NBYTES_MASK) >> QTD_TOKEN_NBYTES_SHIFT;
      if (residual <= length)
        {
          *actual = length - residual;
        }

      if (datain)
        {
          up_invalidate_dcache((uintptr_t)buffer,
                               (uintptr_t)buffer + length);
        }
    }

  return ret;
}

static int request(uint8_t addr, uint16_t maxpacket, uint8_t type,
                   uint8_t req, uint16_t value, uint16_t index,
                   void *buffer, uint16_t length, size_t *actual)
{
  uint8_t setup[8];

  setup[0] = type;
  setup[1] = req;
  setup[2] = value & 0xff;
  setup[3] = value >> 8;
  setup[4] = index & 0xff;
  setup[5] = index >> 8;
  setup[6] = length & 0xff;
  setup[7] = length >> 8;
  return control(addr, maxpacket, setup, buffer, length, actual);
}

static void parse_config(size_t length)
{
  size_t offset = 0;
  uint8_t interface = 0xff;
  uint8_t alternate = 0;
  uint8_t class_code = 0;
  uint8_t subclass = 0;
  uint8_t format_index = 0;
  char fourcc[5] = "none";

  g_uvc.streaming_interfaces = 0;
  g_uvc.mode_count = 0;
  g_uvc.endpoint_count = 0;
  while (offset + 2 <= length)
    {
      const uint8_t *d = &g_config[offset];
      uint8_t dlen = d[0];
      uint8_t dtype = d[1];

      if (dlen < 2 || offset + dlen > length)
        {
          g_uvc.parse = -EPROTO;
          return;
        }

      if (dtype == USB_DT_INTERFACE && dlen >= 9)
        {
          interface = d[2];
          alternate = d[3];
          class_code = d[5];
          subclass = d[6];
          format_index = 0;
          memcpy(fourcc, "none", 5);
          if (class_code == USB_CLASS_VIDEO &&
              subclass == USB_SC_VIDEOSTREAMING && alternate == 0)
            {
              g_uvc.streaming_interfaces++;
            }
        }
      else if (dtype == USB_DT_CS_INTERFACE && dlen >= 4 &&
               class_code == USB_CLASS_VIDEO &&
               subclass == USB_SC_VIDEOSTREAMING)
        {
          if (d[2] == VS_FORMAT_UNCOMPRESSED && dlen >= 21)
            {
              format_index = d[3];
              memcpy(fourcc, &d[5], 4);
              fourcc[4] = '\0';
            }
          else if (d[2] == VS_FORMAT_MJPEG && dlen >= 5)
            {
              format_index = d[3];
              memcpy(fourcc, "MJPG", 5);
            }
          else if ((d[2] == VS_FRAME_UNCOMPRESSED ||
                    d[2] == VS_FRAME_MJPEG) && dlen >= 26 &&
                   g_uvc.mode_count < UVC_MODE_MAX)
            {
              struct uvc_mode_s *m = &g_uvc.modes[g_uvc.mode_count++];
              m->format_index = format_index;
              m->frame_index = d[3];
              m->width = getle16(&d[5]);
              m->height = getle16(&d[7]);
              m->interval = getle32(&d[21]);
              memcpy(m->fourcc, fourcc, sizeof(m->fourcc));
            }
        }
      else if (dtype == USB_DT_ENDPOINT && dlen >= 7 &&
               class_code == USB_CLASS_VIDEO &&
               subclass == USB_SC_VIDEOSTREAMING &&
               (d[2] & 0x80) != 0 &&
               g_uvc.endpoint_count < UVC_ENDPOINT_MAX)
        {
          struct uvc_endpoint_s *e =
            &g_uvc.endpoints[g_uvc.endpoint_count++];
          uint16_t raw = getle16(&d[4]);
          e->interface = interface;
          e->alternate = alternate;
          e->address = d[2];
          e->type = d[3] & 3;
          e->raw_maxpacket = raw;
          e->payload = (raw & 0x7ffu) * (1u + ((raw >> 11) & 3u));
          e->interval = d[6];
        }

      offset += dlen;
    }

  g_uvc.parse = g_uvc.streaming_interfaces > 0 &&
                g_uvc.endpoint_count > 0 ? OK : -ENOTSUP;
}

static int enumerate(void)
{
  uint8_t device[18] __attribute__((aligned(64)));
  uint8_t header[9] __attribute__((aligned(64)));
  uint8_t maxpacket;
  uint16_t total;
  size_t actual;
  int ret;

  memset(device, 0, sizeof(device));
  ret = request(0, 8, 0x80, USB_GET_DESCRIPTOR, USB_DT_DEVICE << 8,
                0, device, 8, &actual);
  if (ret < 0 || actual != 8 || device[0] < 8 ||
      device[1] != USB_DT_DEVICE)
    {
      return ret < 0 ? ret : -EPROTO;
    }

  maxpacket = device[7];
  if (maxpacket != 8 && maxpacket != 16 && maxpacket != 32 &&
      maxpacket != 64)
    {
      return -EPROTO;
    }

  ret = request(0, maxpacket, 0x00, USB_SET_ADDRESS, UVC_ADDRESS,
                0, NULL, 0, &actual);
  if (ret < 0)
    {
      return ret;
    }

  delay_ms(10);
  ret = request(UVC_ADDRESS, maxpacket, 0x80, USB_GET_DESCRIPTOR,
                USB_DT_DEVICE << 8, 0, device, sizeof(device), &actual);
  if (ret < 0 || actual != sizeof(device))
    {
      return ret < 0 ? ret : -EPROTO;
    }

  g_uvc.ep0_maxpacket = maxpacket;
  g_uvc.bcdusb = getle16(&device[2]);
  g_uvc.device_class = device[4];
  g_uvc.vid = getle16(&device[8]);
  g_uvc.pid = getle16(&device[10]);
  ret = request(UVC_ADDRESS, maxpacket, 0x80, USB_GET_DESCRIPTOR,
                USB_DT_CONFIG << 8, 0, header, sizeof(header), &actual);
  if (ret < 0 || actual != sizeof(header) || header[1] != USB_DT_CONFIG)
    {
      return ret < 0 ? ret : -EPROTO;
    }

  total = getle16(&header[2]);
  g_uvc.config_length = total;
  if (total < sizeof(header) || total > sizeof(g_config))
    {
      return -E2BIG;
    }

  ret = request(UVC_ADDRESS, maxpacket, 0x80, USB_GET_DESCRIPTOR,
                USB_DT_CONFIG << 8, 0, g_config, total, &actual);
  if (ret < 0 || actual != total)
    {
      return ret < 0 ? ret : -EPROTO;
    }

  g_uvc.configuration = g_config[5];
  parse_config(total);
  return request(UVC_ADDRESS, maxpacket, 0x00, USB_SET_CONFIGURATION,
                 g_uvc.configuration, 0, NULL, 0, &actual);
}

static void snapshot(void)
{
  uintptr_t opbase = XHCI2_BASE + g_uvc.caplength;
  uintptr_t runtime = XHCI2_BASE + g_uvc.xhci_rtsoff;
  g_uvc.ccu_phy = getreg32(CCU_USB2_U2_REF);
  g_uvc.ccu_hci = getreg32(CCU_USB2_MF);
  g_uvc.ccu_u3_utmi = getreg32(CCU_USB2_U3_UTMI);
  g_uvc.ccu_u2_pipe = getreg32(CCU_USB2_U2_PIPE);
  g_uvc.pmu = getreg32(CCU_USB2_BGR);
  g_uvc.phy_ctrl = getreg32(USB2_PHYCTL);
  g_uvc.phy_iscr = getreg32(USB2_ISCR);
  g_uvc.pck_power = getreg32(PCK_USB2 + PCK_POWER);
  g_uvc.pck_status = getreg32(PCK_USB2 + PCK_STATUS);
  g_uvc.ahb_master = getreg32(CCU_AHB_MASTER);
  g_uvc.suspend_clock = getreg32(CCU_USB2_SUSPEND);
  g_uvc.serdes_clock = getreg32(CCU_SERDES_PHY_CFG);
  g_uvc.serdes_bgr = getreg32(CCU_SERDES_BGR);
  g_uvc.serdes_usb_bgr = getreg32(SERDES_USB_BGR);
  g_uvc.rtc_serdes = getreg32(RTC_DCXO_SERDES);
  g_uvc.dwc3_gctl = getreg32(DWC3_GCTL);
  g_uvc.dwc3_id = getreg32(DWC3_GSNPSID);
  g_uvc.dwc3_usb2phycfg = getreg32(DWC3_GUSB2PHYCFG0);
  g_uvc.dwc3_usb3pipectl = getreg32(DWC3_GUSB3PIPECTL0);
  g_uvc.usb2_iscr = getreg32(USB2_ISCR);
  g_uvc.usb2_physts = getreg32(USB2_PHYSTS);
  if (g_uvc.caplength >= 0x10 && g_uvc.caplength <= 0x80)
    {
      g_uvc.usbcmd = getreg32(opbase + XHCI_USBCMD);
      g_uvc.usbsts = getreg32(opbase + XHCI_USBSTS);
      g_uvc.portsc = getreg32(opbase + XHCI_PORT_BASE);
      g_uvc.portsc2 = g_uvc.max_ports > 1 ?
                      getreg32(opbase + XHCI_PORT_BASE +
                               XHCI_PORT_STRIDE) : 0;
      g_uvc.xhci_config = getreg32(opbase + XHCI_CONFIG);
      g_uvc.xhci_crcr_lo = getreg32(opbase + XHCI_CRCR);
      g_uvc.xhci_crcr_hi = getreg32(opbase + XHCI_CRCR + 4);
      g_uvc.xhci_dcbaap_lo = getreg32(opbase + XHCI_DCBAAP);
      g_uvc.xhci_dcbaap_hi = getreg32(opbase + XHCI_DCBAAP + 4);
      if (g_uvc.xhci_rtsoff != 0)
        {
          g_uvc.xhci_mfindex = getreg32(runtime);
          g_uvc.xhci_erstsz =
            getreg32(runtime + XHCI_RUNTIME_IRQ0 + XHCI_ERSTSZ);
          g_uvc.xhci_erdp_lo =
            getreg32(runtime + XHCI_RUNTIME_IRQ0 + XHCI_ERDP);
        }
    }
}

static int probe(void)
{
  int ret;

  nxmutex_lock(&g_uvc.lock);
  g_uvc.xhci_reset = -EAGAIN;
  g_uvc.xhci_start = -EAGAIN;
  g_uvc.xhci_run_polls = 0;
  g_uvc.xhci_scratchpads = 0;
  g_uvc.xhci_pagesize = 0;
  g_uvc.xhci_rtsoff = 0;
  g_uvc.xhci_config = 0;
  g_uvc.xhci_crcr_before_run = 0;
  g_uvc.xhci_crcr_lo = 0;
  g_uvc.xhci_crcr_hi = 0;
  g_uvc.xhci_dcbaap_lo = 0;
  g_uvc.xhci_dcbaap_hi = 0;
  g_uvc.xhci_erstsz = 0;
  g_uvc.xhci_erdp_lo = 0;
  g_uvc.xhci_mfindex = 0;
  g_uvc.combo_checkpoint = -EAGAIN;
  g_uvc.phy_reset_checkpoint = -EAGAIN;
  g_uvc.xhci_cnr_polls = 0;
  g_uvc.portsc = 0;
  g_uvc.portsc2 = 0;
  g_uvc.enumeration = -EAGAIN;
  g_uvc.parse = -EAGAIN;
  g_uvc.vid = 0;
  g_uvc.pid = 0;
  g_uvc.mode_count = 0;
  g_uvc.endpoint_count = 0;
  ret = xhci2_checkpoint();
  g_uvc.checkpoint = ret;

  /* Never redirect failed xHCI accesses to the power/device-only USB0 port.
   * Keep legacy helpers referenced without executing them until their
   * descriptor parser is migrated to a real xHCI control-transfer path. */

  (void)usb0_clock_enable;
  (void)ehci0_checkpoint;
  (void)enumerate;

  snapshot();
  nxmutex_unlock(&g_uvc.lock);
  syslog(ret < 0 ? LOG_WARNING : LOG_INFO,
         "A733 UVC: USB2/xHCI2 probe=%d ports=%08lx/%08lx "
         "DWC3=%08lx maxports=%u\n", ret,
         (unsigned long)g_uvc.portsc, (unsigned long)g_uvc.portsc2,
         (unsigned long)g_uvc.dwc3_id, g_uvc.max_ports);
  return ret;
}

static ssize_t uvc_read(struct file *filep, char *buffer, size_t buflen)
{
  size_t used = 0;
  size_t i;
  size_t offset;
  size_t available;

  nxmutex_lock(&g_uvc.lock);
#define APPEND(...) do { \
  if (used < sizeof(g_report)) \
    { \
      int n = snprintf(g_report + used, sizeof(g_report) - used, \
                       __VA_ARGS__); \
      if (n > 0) used += (size_t)n < sizeof(g_report) - used ? \
                         (size_t)n : sizeof(g_report) - used; \
    } \
} while (0)
  APPEND("A733 Cubie A7Z external USB2/xHCI2 UVC checkpoint\n");
  APPEND("controller: xhci=%08lx version=%04x caplen=%u maxports=%u "
         "hcs=%08lx/%08lx cmd=%08lx sts=%08lx ports=%08lx/%08lx "
         "reset=%d start=%d cnr-polls=%u run-polls=%u checkpoint=%d\n",
         (unsigned long)XHCI2_BASE,
         g_uvc.hciversion, g_uvc.caplength,
         g_uvc.max_ports,
         (unsigned long)g_uvc.hcsparams,
         (unsigned long)g_uvc.hcsparams2, (unsigned long)g_uvc.usbcmd,
         (unsigned long)g_uvc.usbsts, (unsigned long)g_uvc.portsc,
         (unsigned long)g_uvc.portsc2,
         g_uvc.xhci_reset, g_uvc.xhci_start, g_uvc.xhci_cnr_polls,
         g_uvc.xhci_run_polls,
         g_uvc.checkpoint);
  APPEND("xhci-runtime-v114: pagesize=%08lx rtsoff=%08lx scratch=%u "
         "(minimal polled rings, RUN without HCRST)\n",
         (unsigned long)g_uvc.xhci_pagesize,
         (unsigned long)g_uvc.xhci_rtsoff, g_uvc.xhci_scratchpads);
  APPEND("xhci-regs-v114: config=%08lx crcr-pre=%08lx crcr=%08lx:%08lx "
         "dcbaap=%08lx:%08lx erstsz=%08lx erdp-lo=%08lx mfindex=%08lx\n",
         (unsigned long)g_uvc.xhci_config,
         (unsigned long)g_uvc.xhci_crcr_before_run,
         (unsigned long)g_uvc.xhci_crcr_hi,
         (unsigned long)g_uvc.xhci_crcr_lo,
         (unsigned long)g_uvc.xhci_dcbaap_hi,
         (unsigned long)g_uvc.xhci_dcbaap_lo,
         (unsigned long)g_uvc.xhci_erstsz,
         (unsigned long)g_uvc.xhci_erdp_lo,
         (unsigned long)g_uvc.xhci_mfindex);
  APPEND("dwc3: id=%08lx gctl=%08lx u2cfg=%08lx u3pipe=%08lx "
         "iscr=%08lx physts=%08lx pck=%08lx/%08lx PL=%08lx\n",
         (unsigned long)g_uvc.dwc3_id, (unsigned long)g_uvc.dwc3_gctl,
         (unsigned long)g_uvc.dwc3_usb2phycfg,
         (unsigned long)g_uvc.dwc3_usb3pipectl,
         (unsigned long)g_uvc.usb2_iscr,
         (unsigned long)g_uvc.usb2_physts,
         (unsigned long)g_uvc.pck_power,
         (unsigned long)g_uvc.pck_status,
         (unsigned long)getreg32(PL_DATA));
  APPEND("typec: et7304=%04x:%04x bcd=%04x role=%02x cc=%02x "
         "power=%02x fault=%02x alert=%04x/%04x work=%02x int=%u "
         "orientation=CC%u polls=%u twi=%02lx bgr=%08lx checkpoint=%d\n",
         g_uvc.typec_vid, g_uvc.typec_pid,
         g_uvc.typec_bcd, g_uvc.typec_role, g_uvc.typec_cc,
         g_uvc.typec_power, g_uvc.typec_fault, g_uvc.typec_alert,
         g_uvc.typec_alert_mask, g_uvc.typec_working_mode,
         g_uvc.typec_int_level, g_uvc.typec_orientation,
         g_uvc.typec_cc_polls,
         (unsigned long)g_uvc.typec_twi_status,
         (unsigned long)g_uvc.typec_bgr,
         g_uvc.typec_checkpoint);
  APPEND("typec-twi-v101: ctrl=%08lx int=%08lx fifo=%08lx bus=%08lx "
         "lcr=%08lx lines=%s/%s\n",
         (unsigned long)g_uvc.typec_drv_ctrl,
         (unsigned long)g_uvc.typec_drv_int,
         (unsigned long)g_uvc.typec_drv_fifo,
         (unsigned long)g_uvc.typec_drv_bus,
         (unsigned long)g_uvc.typec_twi_lcr,
         (g_uvc.typec_drv_bus & (1u << 7)) != 0 ? "SCL-high" : "SCL-low",
         (g_uvc.typec_drv_bus & (1u << 6)) != 0 ? "SDA-high" : "SDA-low");
  APPEND("clock: u2ref=%08lx suspend=%08lx mf=%08lx u3utmi=%08lx "
         "u2pipe=%08lx bgr=%08lx "
         "phyctrl=%08lx iscr=%08lx line=%lu vbus=%lu ahb=%08lx\n",
         (unsigned long)g_uvc.ccu_phy,
         (unsigned long)g_uvc.suspend_clock,
         (unsigned long)g_uvc.ccu_hci,
         (unsigned long)g_uvc.ccu_u3_utmi,
         (unsigned long)g_uvc.ccu_u2_pipe,
         (unsigned long)g_uvc.pmu, (unsigned long)g_uvc.phy_ctrl,
         (unsigned long)g_uvc.phy_iscr,
         (unsigned long)((g_uvc.phy_iscr >> 25) & 3u),
         (unsigned long)((g_uvc.phy_iscr >> 24) & 1u),
         (unsigned long)g_uvc.ahb_master);
  APPEND("combo-v114: checkpoint=%d (late USB2P0 PHY reset release, PMA ready)\n",
         g_uvc.combo_checkpoint);
  APPEND("phy-handoff-v114: checkpoint=%d (reset-free DWC3 host handoff, forced SIE ID/VBUS)\n",
         g_uvc.phy_reset_checkpoint);
  APPEND("serdes: cfg=%08lx bgr=%08lx usb-bgr=%08lx rtc=%08lx\n",
         (unsigned long)g_uvc.serdes_clock,
         (unsigned long)g_uvc.serdes_bgr,
         (unsigned long)g_uvc.serdes_usb_bgr,
         (unsigned long)g_uvc.rtc_serdes);
  APPEND("device: enumeration=%d VID:PID=%04x:%04x bcdUSB=%04x "
         "class=%02x EP0=%u config=%u total=%u\n", g_uvc.enumeration,
         g_uvc.vid, g_uvc.pid, g_uvc.bcdusb, g_uvc.device_class,
         g_uvc.ep0_maxpacket, g_uvc.configuration, g_uvc.config_length);
  APPEND("uvc: parse=%d streaming-interfaces=%u modes=%u endpoints=%u\n",
         g_uvc.parse, g_uvc.streaming_interfaces, g_uvc.mode_count,
         g_uvc.endpoint_count);
  for (i = 0; i < g_uvc.mode_count; i++)
    {
      struct uvc_mode_s *m = &g_uvc.modes[i];
      APPEND("mode[%u]: format=%u frame=%u fourcc='%s' %ux%u "
             "interval=%lu (%.1f fps)\n", (unsigned int)i,
             m->format_index, m->frame_index, m->fourcc, m->width,
             m->height, (unsigned long)m->interval,
             m->interval == 0 ? 0.0 : 10000000.0 / m->interval);
    }

  for (i = 0; i < g_uvc.endpoint_count; i++)
    {
      struct uvc_endpoint_s *e = &g_uvc.endpoints[i];
      APPEND("endpoint[%u]: interface=%u alt=%u address=%02x type=%s "
             "raw=%04x payload=%u interval=%u\n", (unsigned int)i,
             e->interface, e->alternate, e->address,
             e->type == 1 ? "iso" : e->type == 2 ? "bulk" : "other",
             e->raw_maxpacket, e->payload, e->interval);
    }

  APPEND("control: echo probe > /dev/a733-uvc\n");
  APPEND("scope: USB-C 3.1 ET7304 source/host role, USB2/xHCI2 power, PHY "
         "and connection checkpoint; xHCI rings, UVC streaming, preprocess "
         "and live NPU inference pending physical-port validation\n");
#undef APPEND
  if (used > sizeof(g_report))
    {
      used = sizeof(g_report);
    }

  offset = filep->f_pos;
  available = offset < used ? used - offset : 0;
  if (buflen > available)
    {
      buflen = available;
    }

  memcpy(buffer, g_report + offset, buflen);
  filep->f_pos += buflen;
  nxmutex_unlock(&g_uvc.lock);
  return buflen;
}

static ssize_t uvc_write(struct file *filep, const char *buffer,
                         size_t buflen)
{
  char command[16];
  size_t length = buflen;
  int ret;

  if (length >= sizeof(command))
    {
      return -E2BIG;
    }

  memcpy(command, buffer, length);
  while (length > 0 && (command[length - 1] == '\n' ||
                         command[length - 1] == '\r' ||
                         command[length - 1] == ' '))
    {
      length--;
    }

  command[length] = '\0';
  if (strcmp(command, "probe") != 0)
    {
      return -EINVAL;
    }

  ret = probe();
  return ret < 0 ? ret : (ssize_t)buflen;
}

static const struct file_operations g_uvc_fops =
{
  .read = uvc_read,
  .write = uvc_write,
};

int a733_usb_camera_initialize(void)
{
  int ret;

  if (!g_registered)
    {
      ret = register_driver("/dev/a733-uvc", &g_uvc_fops, 0660, NULL);
      if (ret < 0)
        {
          return ret;
        }

      g_registered = true;
    }

  ret = probe(); /* Device absence must not block system initialization. */
  syslog(LOG_INFO, "A733 USB diagnostic node registered; host probe=%d (not audio-ready)\n",
         ret);
  return OK;
}

#endif /* CONFIG_A733_USB_CAMERA */
