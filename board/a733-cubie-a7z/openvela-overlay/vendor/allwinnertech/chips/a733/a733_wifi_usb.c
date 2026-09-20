/****************************************************************************
 * vendor/allwinnertech/chips/a733/a733_wifi_usb.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/* Cubie A7Z onboard FCU760K wireless-module bring-up.
 *
 * The board schematic connects the module to the dedicated USB1 host and
 * assigns PM0 to the 3.3 V load switch and PM1 to WL-REG-ON.  This code only
 * touches that internal port.  It enumerates the AIC8800D80 BootROM, uses
 * the official bulk protocol to load the exact U02 firmware set, starts the
 * application, verifies the 8d81 runtime USB identity, maps the Wi-Fi and
 * Bluetooth endpoints independently, and validates the full-MAC command
 * path with MM_VERSION_REQ.  The WLAN data transport is serialized and bound
 * to a NuttX wlan0 interface; WPA key management and Bluetooth HCI build on
 * this layer.
 */

#include <nuttx/config.h>

#ifdef CONFIG_A733_WIFI_USB_CHECKPOINT

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <syslog.h>
#include <time.h>

#include <crypto/rijndael.h>
#include <crypto/sha1.h>
#include <nuttx/arch.h>
#include <nuttx/cache.h>
#include <nuttx/clock.h>
#include <nuttx/fs/fs.h>
#include <nuttx/kthread.h>
#include <nuttx/mutex.h>
#include <nuttx/signal.h>
#include <nuttx/usb/ehci.h>

#ifdef CONFIG_NET
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <net/if_arp.h>
#  include <nuttx/net/ip.h>
#  include <nuttx/net/netdev.h>
#  include <nuttx/wireless/wireless.h>
#  ifdef CONFIG_NETUTILS_DHCPC
#    include <netutils/dhcpc.h>
#    include <netutils/netlib.h>
#  endif
#  ifdef CONFIG_NET_PKT
#    include <nuttx/net/pkt.h>
#  endif
#endif

#ifdef CONFIG_NET
#  define A733_WIFI_HOST_NETDEV "wlan0-rx-worker"
#  define A733_WIFI_READY_SCOPE \
     "LMAC station ready; 20 ms WPA auth / 3 ms low-gap data USB RX/TX; " \
     "host WPA2-PSK/CCMP and DHCP/IPv4 networking enabled; " \
     "Bluetooth HCI pending"
#else
#  define A733_WIFI_HOST_NETDEV "disabled"
#  define A733_WIFI_READY_SCOPE \
     "LMAC station ready; WLAN data checkpoints enabled; " \
     "host network stack and Bluetooth HCI pending"
#endif

#include "arm64_internal.h"

#define A733_CCU_BASE              UINT64_C(0x02002000)
#define A733_CCU_MSI_LITE2         (A733_CCU_BASE + 0x05a4)
#define A733_CCU_AHB_MASTER        (A733_CCU_BASE + 0x05c0)
#define A733_CCU_USB1_PHY          (A733_CCU_BASE + 0x1308)
#define A733_CCU_USB1_HCI          (A733_CCU_BASE + 0x130c)
#define A733_CCU_USB_REF           (A733_CCU_BASE + 0x1340)
#define A733_CCU_RES_DCAP          (A733_CCU_BASE + 0x1a00)

#define A733_R_PIO_BASE            UINT64_C(0x07025000)
#define A733_PM_BASE               (A733_R_PIO_BASE + 0x30)
#define A733_PM_CFG0               (A733_PM_BASE + 0x00)
#define A733_PM_DATA               (A733_PM_BASE + 0x10)

#define A733_EHCI1_BASE            UINT64_C(0x04200000)
#define A733_OHCI1_BASE            UINT64_C(0x04200400)
#define A733_USB1_PMU              (A733_EHCI1_BASE + 0x800)
#define A733_USB1_PHY_CTRL         (A733_EHCI1_BASE + 0x810)

#define EHCI_PORTSC_CONNECT        (1u << 0)
#define EHCI_PORTSC_ENABLE         (1u << 2)
#define EHCI_PORTSC_POWER          (1u << 12)

#define A733_USB_ADDRESS           1u
#define A733_USB_CONFIG_MAX        512u
#define A733_USB_ENDPOINT_MAX      32u
#define A733_USB_TIMEOUT_MS        500u
#define A733_AIC_TIMEOUT_MS        2000u
#define A733_AIC_RX_MAX            2048u
#define A733_WIFI_SCAN_MAX         64u
#define A733_WIFI_DATA_RX_MAX      2048u
#define A733_WIFI_DATA_TRIES       4u
#define A733_WIFI_RX_AUTH_TIMEOUT_MS 20u
#define A733_WPA_WAIT_M1           1u
#define A733_WPA_WAIT_M3           2u
#define A733_WPA_COMPLETE          3u
#define A733_WIFI_RX_DATA_TIMEOUT_MS 3u
#define A733_WIFI_RX_IDLE_US         25u
#define A733_WIFI_RX_ERROR_US      1000u
#define A733_WIFI_RX_STACKSIZE      8192u
#define A733_WIFI_RX_PRIORITY       100u

#define A733_AIC_VID               0xa69cu
#define A733_AIC_BOOT_PID          0x8d80u
#define A733_AIC_DBG_MEM_READ_REQ  0x0400u
#define A733_AIC_DBG_MEM_READ_CFM  0x0401u
#define A733_AIC_DBG_MEM_WRITE_REQ 0x0402u
#define A733_AIC_DBG_MEM_WRITE_CFM 0x0403u
#define A733_AIC_DBG_BLOCK_REQ     0x040bu
#define A733_AIC_DBG_BLOCK_CFM     0x040cu
#define A733_AIC_DBG_START_REQ     0x040du
#define A733_AIC_TASK_DBG          1u
#define A733_AIC_DRIVER_TASK       100u
#define A733_AIC_USB_TYPE_CMD      0x11u
#define A733_AIC_USB_TYPE_DATA_CFM 0x12u
#define A733_AIC_CHIP_ID_ADDR      UINT32_C(0x40500000)
#define A733_AIC_RUNTIME_PID       0x8d81u
#define A733_AIC_MM_RESET_REQ      0u
#define A733_AIC_MM_RESET_CFM      1u
#define A733_AIC_MM_START_REQ      2u
#define A733_AIC_MM_START_CFM      3u
#define A733_AIC_MM_VERSION_REQ    4u
#define A733_AIC_MM_VERSION_CFM    5u
#define A733_AIC_MM_ADD_IF_REQ     6u
#define A733_AIC_MM_ADD_IF_CFM     7u
#define A733_AIC_MM_KEY_ADD_REQ    36u
#define A733_AIC_MM_KEY_ADD_CFM    37u
#define A733_AIC_MM_RF_CALIB_REQ   105u
#define A733_AIC_MM_RF_CALIB_CFM   106u
#define A733_AIC_MM_GET_MAC_REQ    115u
#define A733_AIC_MM_GET_MAC_CFM    116u
#define A733_AIC_MM_TXPWR_REQ      119u
#define A733_AIC_MM_TXPWR_CFM      120u
#define A733_AIC_MM_STACK_REQ      123u
#define A733_AIC_MM_STACK_CFM      124u
#define A733_AIC_ME_CONFIG_REQ     (5u << 10)
#define A733_AIC_ME_CONFIG_CFM     ((5u << 10) + 1u)
#define A733_AIC_ME_CHAN_REQ       ((5u << 10) + 2u)
#define A733_AIC_ME_CHAN_CFM       ((5u << 10) + 3u)
#define A733_AIC_ME_PORT_REQ       ((5u << 10) + 4u)
#define A733_AIC_ME_PORT_CFM       ((5u << 10) + 5u)
#define A733_AIC_SCANU_START_REQ   (4u << 10)
#define A733_AIC_SCANU_START_CFM   ((4u << 10) + 1u)
#define A733_AIC_SCANU_RESULT_IND  ((4u << 10) + 4u)
#define A733_AIC_SCANU_DONE_CFM    ((4u << 10) + 9u)
#define A733_AIC_SM_CONNECT_REQ    (6u << 10)
#define A733_AIC_SM_CONNECT_CFM    ((6u << 10) + 1u)
#define A733_AIC_SM_CONNECT_IND    ((6u << 10) + 2u)
#define A733_AIC_SM_DISCONNECT_REQ ((6u << 10) + 3u)
#define A733_AIC_SM_DISCONNECT_CFM ((6u << 10) + 4u)
#define A733_AIC_SM_DISCONNECT_IND ((6u << 10) + 5u)
#define A733_AIC_CONNECT_REQ_SIZE  320u
#define A733_WIFI_ASSOC_IE_MAX     128u
#define A733_WIFI_WPA_EAPOL_MAX    512u
#define A733_WIFI_WPA_PMK_LEN      32u
#define A733_WIFI_WPA_PTK_LEN      48u
#define A733_AIC_FMAC_ADDRESS      UINT32_C(0x00120000)
#define A733_AIC_BLOCK_SIZE        1024u
#define A733_AIC_TX_MAX            1088u
#define A733_AIC_PATCH_MAX         2048u

#define A733_AIC_FW_DIR            "/data/aic"
#define A733_AIC_ADID_FILE         A733_AIC_FW_DIR "/fw_adid_8800d80_u02.bin"
#define A733_AIC_PATCH_FILE        A733_AIC_FW_DIR "/fw_patch_8800d80_u02.bin"
#define A733_AIC_PATCH_EXT0_FILE   A733_AIC_FW_DIR "/fw_patch_8800d80_u02_ext0.bin"
#define A733_AIC_PATCH_TABLE_FILE  A733_AIC_FW_DIR "/fw_patch_table_8800d80_u02.bin"
#define A733_AIC_FMAC_FILE         A733_AIC_FW_DIR "/fmacfw_8800d80_u02.bin"

#define USB_REQ_GET_DESCRIPTOR     6u
#define USB_REQ_SET_ADDRESS        5u
#define USB_REQ_SET_CONFIGURATION  9u
#define USB_DESC_DEVICE            1u
#define USB_DESC_CONFIG            2u
#define USB_DESC_INTERFACE         4u
#define USB_DESC_ENDPOINT          5u

struct a733_wifi_endpoint_s
{
  uint8_t interface;
  uint8_t alternate;
  uint8_t address;
  uint8_t attributes;
  uint16_t maxpacket;
  uint8_t interval;
};

struct a733_wifi_scan_s
{
  uint8_t bssid[6];
  char ssid[33];
  uint16_t frequency;
  int8_t rssi;
  bool privacy;
  uint16_t assoc_ie_length;
  uint8_t assoc_ie[A733_WIFI_ASSOC_IE_MAX];
};

struct a733_wifi_state_s
{
  uint32_t ccu_ahb;
  uint32_t ccu_phy;
  uint32_t ccu_hci;
  uint32_t pm_cfg;
  uint32_t pm_data;
  uint32_t pmu;
  uint32_t phy_ctrl;
  uint32_t hcsparams;
  uint32_t usbcmd;
  uint32_t usbsts;
  uint32_t portsc;
  uint16_t hciversion;
  uint8_t caplength;
  int checkpoint;
  int enumeration;
  uint8_t address;
  uint8_t device_class;
  uint8_t device_subclass;
  uint8_t device_protocol;
  uint8_t ep0_maxpacket;
  uint8_t configuration;
  uint8_t interface_count;
  uint8_t interface_descriptors;
  uint8_t endpoint_count;
  uint16_t vid;
  uint16_t pid;
  uint16_t bcdusb;
  uint16_t config_length;
  int protocol;
  uint8_t bulk_out;
  uint8_t bulk_in;
  uint16_t bulk_maxpacket;
  size_t command_actual;
  size_t response_actual;
  uint32_t probe_address;
  uint32_t probe_value;
  uint16_t response_id;
  bool bulk_out_toggle;
  bool bulk_in_toggle;
  int firmware;
  size_t firmware_bytes;
  uint8_t chip_revision;
  uint16_t runtime_vid;
  uint16_t runtime_pid;
  int runtime_transport;
  uint8_t wifi_data_out;
  uint8_t wifi_data_in;
  uint8_t wifi_msg_out;
  uint8_t wifi_msg_in;
  uint8_t bt_event_in;
  uint8_t bt_acl_out;
  uint8_t bt_acl_in;
  uint16_t runtime_maxpacket;
  bool wifi_data_out_toggle;
  bool wifi_data_in_toggle;
  bool wifi_msg_out_toggle;
  bool wifi_msg_in_toggle;
  uint32_t wifi_msg_frames;
  uint32_t wifi_data_confirmations;
  uint32_t wifi_async_messages;
  uint32_t lmac_version;
  uint32_t machw_version1;
  uint32_t machw_version2;
  uint32_t phy_version1;
  uint32_t phy_version2;
  uint32_t lmac_features;
  uint16_t lmac_max_sta;
  uint8_t lmac_max_vif;
  int stack_start;
  bool band_5g;
  uint8_t vendor_info;
  uint8_t mac[6];
  int rf_config;
  uint32_t rf_rxgain_24g;
  uint32_t rf_rxgain_5g;
  uint32_t rf_txgain_24g;
  uint32_t rf_txgain_5g;
  int me_config;
  int channel_config;
  uint8_t channel_2g_count;
  uint8_t channel_5g_count;
  int station_vif;
  uint8_t station_vif_index;
  int mac_start;
  int scan_checkpoint;
  uint8_t scan_count;
  uint8_t scan_firmware_count;
  uint16_t scan_last_message;
  uint8_t scan_status;
  bool scan_acknowledged;
  uint16_t scan_result_messages;
  int association_checkpoint;
  uint8_t association_cfm_status;
  uint16_t association_status_code;
  uint8_t association_vif;
  uint8_t association_ap;
  uint8_t association_channel;
  bool associated;
  uint8_t associated_bssid[6];
  char associated_ssid[33];
  int disconnect_checkpoint;
  bool disconnect_confirmed;
  bool disconnect_indicated;
  uint16_t disconnect_reason;
  int data_checkpoint;
  size_t data_actual;
  uint16_t data_packet_length;
  uint16_t data_frame_control;
  uint16_t data_ethertype;
  uint16_t data_llc_offset;
  uint16_t data_payload_length;
  uint32_t data_frames;
  uint32_t data_eapol_frames;
  uint32_t data_arp_frames;
  uint32_t data_arp_requests;
  uint32_t data_arp_replies;
  uint32_t data_tx_arp_frames;
  uint16_t data_arp_operation;
  uint32_t data_arp_sender_ip;
  uint32_t data_arp_target_ip;
  uint32_t data_ipv4_frames;
  uint32_t data_tcp_frames;
  uint32_t data_tcp_syn_frames;
  uint16_t data_tcp_source_port;
  uint16_t data_tcp_destination_port;
  uint32_t data_ipv4_source;
  uint32_t data_ipv4_destination;
  uint8_t data_destination[6];
  uint8_t data_source[6];
  int data_tx_checkpoint;
  size_t data_tx_actual;
  uint32_t data_tx_frames;
  int wpa_checkpoint;
  uint8_t wpa_state;
  uint32_t wpa_m1;
  uint32_t wpa_m3;
  uint32_t wpa_mic_failures;
  uint32_t wpa_replays;
  uint8_t wpa_pmk[A733_WIFI_WPA_PMK_LEN];
  uint8_t wpa_ptk[A733_WIFI_WPA_PTK_LEN];
  uint8_t wpa_snonce[32];
  uint8_t wpa_replay[8];
  uint16_t wpa_assoc_ie_length;
  uint8_t wpa_assoc_ie[A733_WIFI_ASSOC_IE_MAX];
  uint16_t wpa_diag_available;
  uint16_t wpa_diag_body_length;
  uint16_t wpa_diag_key_info;
  uint16_t wpa_diag_key_data_length;
  uint8_t wpa_diag_version;
  uint8_t wpa_diag_type;
  uint8_t wpa_diag_descriptor;
  uint8_t wpa_diag_reports;
  uint8_t wpa_group_cipher;
  uint8_t wpa_group_key_length;
  bool wpa_configured;
  bool wpa_pairwise_installed;
  bool wpa_group_installed;
  bool wpa_port_open;
  uint32_t wext_wpa_version;
  uint32_t wext_pairwise_cipher;
  uint16_t wext_frequency;
  uint8_t wext_bssid[6];
  char wext_passphrase[64];
  uint8_t wext_passphrase_length;
#ifdef CONFIG_NETUTILS_DHCPC
  int dhcp_checkpoint;
  pid_t dhcp_pid;
  uint32_t dhcp_address;
  uint32_t dhcp_netmask;
  uint32_t dhcp_router;
  uint32_t dhcp_dns;
  uint32_t dhcp_lease;
  int gratuitous_arp_checkpoint;
  uint32_t gratuitous_arp_sent;
#endif
  struct a733_wifi_scan_s scan[A733_WIFI_SCAN_MAX];
  struct a733_wifi_endpoint_s endpoints[A733_USB_ENDPOINT_MAX];
};

static struct a733_wifi_state_s g_wifi;
static mutex_t g_wifi_usb_lock = NXMUTEX_INITIALIZER;
/* The runtime message endpoint carries both synchronous command replies and
 * asynchronous TX confirmations/connection events.  The official Linux
 * driver owns it with a dedicated RX queue.  Serialize it here so the
 * lightweight background pump cannot steal a reply from scan/connect/key
 * control transactions. */

static mutex_t g_wifi_msg_lock = NXMUTEX_INITIALIZER;
/* Protect descriptor construction as well as DMA.  The USB lock alone is
 * too late: DHCP announcements can overwrite a packet before bulk takes it.
 * Lock order: network (when held) -> data TX -> USB, never the reverse. */
static mutex_t g_wifi_data_tx_lock = NXMUTEX_INITIALIZER;
static volatile bool g_wifi_tx_pending;

static struct ehci_qh_s g_wifi_qh __attribute__((aligned(64)));
static struct ehci_qtd_s g_wifi_qtd[3] __attribute__((aligned(64)));
static uint8_t g_wifi_setup[8] __attribute__((aligned(64)));
static uint8_t g_wifi_config[A733_USB_CONFIG_MAX]
  __attribute__((aligned(64)));
static uint8_t g_wifi_aic_tx[A733_AIC_TX_MAX] __attribute__((aligned(64)));
static uint8_t g_wifi_aic_rx[A733_AIC_RX_MAX] __attribute__((aligned(64)));
static uint8_t g_wifi_data_rx[A733_WIFI_DATA_RX_MAX]
  __attribute__((aligned(64)));
static uint8_t g_wifi_data_tx[A733_WIFI_DATA_RX_MAX]
  __attribute__((aligned(64)));
static uint8_t g_wifi_aic_block[A733_AIC_BLOCK_SIZE]
  __attribute__((aligned(64)));
static uint8_t g_wifi_aic_parameter[8 + A733_AIC_BLOCK_SIZE]
  __attribute__((aligned(64)));
static uint8_t g_wifi_patch_table[A733_AIC_PATCH_MAX];
static char g_wifi_report[8192];

#ifdef CONFIG_NET
struct a733_wifi_netdev_s
{
  struct net_driver_s dev;
  bool registered;
  bool ifup;
  pid_t rxpid;
};

static struct a733_wifi_netdev_s g_wifi_netdev;
static uint8_t g_wifi_netbuf[MAX_NETDEV_PKTSIZE + CONFIG_NET_GUARDSIZE]
  __attribute__((aligned(64)));

static int a733_wifi_data_send_ethernet(const uint8_t *frame,
                                        size_t length);

#ifdef CONFIG_NETUTILS_DHCPC
/* Announce the leased address without requiring a user-initiated ping.
 * Submission success is not proof that an AP or peer received the packet.
 */

static int a733_wifi_send_gratuitous_arp(void)
{
  uint8_t frame[42];
  int first_error = OK;
  unsigned int attempt;

  memset(frame, 0, sizeof(frame));
  memset(frame, 0xff, 6);
  memcpy(frame + 6, g_wifi.mac, 6);
  frame[12] = 0x08;
  frame[13] = 0x06;
  frame[14] = 0x00;
  frame[15] = 0x01;
  frame[16] = 0x08;
  frame[17] = 0x00;
  frame[18] = 6;
  frame[19] = 4;
  frame[20] = 0x00;
  frame[21] = 0x02;
  memcpy(frame + 22, g_wifi.mac, 6);
  memcpy(frame + 28, &g_wifi.dhcp_address, 4);
  memset(frame + 32, 0xff, 6);
  memcpy(frame + 38, &g_wifi.dhcp_address, 4);

  g_wifi.gratuitous_arp_sent = 0;
  for (attempt = 0; attempt < 3; attempt++)
    {
      /* Use standard announcement requests, spread over the post-DHCP
       * settling interval rather than a single 60 ms burst. */

      if (!g_wifi.associated || !g_wifi.wpa_port_open)
        {
          return -ENETDOWN;
        }

      frame[21] = 0x01;
      memset(frame + 32, 0x00, 6);
      int ret = a733_wifi_data_send_ethernet(frame, sizeof(frame));

      if (ret == OK)
        {
          g_wifi.gratuitous_arp_sent++;
        }
      else if (first_error == OK)
        {
          first_error = ret;
        }

      if (attempt < 2)
        {
          nxsig_usleep(1000000);
        }
    }

  g_wifi.gratuitous_arp_checkpoint =
    g_wifi.gratuitous_arp_sent > 0 ? OK : first_error;
  return g_wifi.gratuitous_arp_checkpoint;
}
#endif

#ifdef CONFIG_NETUTILS_DHCPC
static int a733_wifi_dhcp_thread(int argc, char *argv[])
{
  struct dhcpc_state state;
  struct in_addr zero;
  FAR void *handle;
  int ret;

  memset(&state, 0, sizeof(state));
  zero.s_addr = 0;

  /* NSH brings wlan0 up before association.  Remove its bootstrap address
   * so the client starts DHCP from INIT after the controlled port opens.
   */

  netlib_set_ipv4addr("wlan0", &zero);
  netlib_set_ipv4netmask("wlan0", &zero);
  netlib_set_dripv4addr("wlan0", &zero);

  handle = dhcpc_open("wlan0", g_wifi.mac, sizeof(g_wifi.mac));
  if (handle == NULL)
    {
      ret = -ENOMEM;
      goto finished;
    }

  errno = 0;
  ret = dhcpc_request(handle, &state);
  dhcpc_close(handle);
  if (ret < 0)
    {
      /* dhcpc_request() follows the NuttX convention and may collapse the
       * reason to ERROR.  Preserve errno when available so the board log is
       * useful; a silent failure is the client's receive timeout.
       */

      ret = errno > 0 ? -errno : -ETIMEDOUT;
      goto finished;
    }

  ret = netlib_set_ipv4addr("wlan0", &state.ipaddr);
  if (ret == OK && state.netmask.s_addr != 0)
    {
      ret = netlib_set_ipv4netmask("wlan0", &state.netmask);
    }

  if (ret == OK && state.default_router.s_addr != 0)
    {
      ret = netlib_set_dripv4addr("wlan0", &state.default_router);
    }

#ifdef CONFIG_NETDB_DNSCLIENT
  if (ret == OK && state.dnsaddr.s_addr != 0)
    {
      ret = netlib_set_ipv4dnsaddr(&state.dnsaddr);
    }
#endif

  if (ret == OK)
    {
      g_wifi.dhcp_address = state.ipaddr.s_addr;
      g_wifi.dhcp_netmask = state.netmask.s_addr;
      g_wifi.dhcp_router = state.default_router.s_addr;
      g_wifi.dhcp_dns = state.dnsaddr.s_addr;
      g_wifi.dhcp_lease = state.lease_time;
      a733_wifi_send_gratuitous_arp();
    }

finished:
  g_wifi.dhcp_checkpoint = ret;
  g_wifi.dhcp_pid = -1;
  if (ret == OK)
    {
      uint32_t ip = ntohl(g_wifi.dhcp_address);
      uint32_t router = ntohl(g_wifi.dhcp_router);
      uint32_t dns = ntohl(g_wifi.dhcp_dns);

      syslog(LOG_INFO,
             "A733 WIFI: DHCP passed address=%u.%u.%u.%u "
             "router=%u.%u.%u.%u dns=%u.%u.%u.%u lease=%lu s "
             "gratuitous-arp=%lu/%d\n",
             (ip >> 24) & 0xff, (ip >> 16) & 0xff,
             (ip >> 8) & 0xff, ip & 0xff,
             (router >> 24) & 0xff, (router >> 16) & 0xff,
             (router >> 8) & 0xff, router & 0xff,
             (dns >> 24) & 0xff, (dns >> 16) & 0xff,
             (dns >> 8) & 0xff, dns & 0xff,
             (unsigned long)g_wifi.dhcp_lease,
             (unsigned long)g_wifi.gratuitous_arp_sent,
             g_wifi.gratuitous_arp_checkpoint);
    }
  else
    {
      syslog(LOG_WARNING, "A733 WIFI: DHCP failed: %d\n", ret);
    }

  return ret;
}

static int a733_wifi_dhcp_start(void)
{
  if (!g_wifi.wpa_port_open)
    {
      return -ENETDOWN;
    }

  if (g_wifi.dhcp_pid > 0)
    {
      return -EBUSY;
    }

  g_wifi.dhcp_checkpoint = -EINPROGRESS;
  g_wifi.dhcp_pid = kthread_create("a733-dhcp", 100, 8192,
                                   a733_wifi_dhcp_thread, NULL);
  if (g_wifi.dhcp_pid < 0)
    {
      g_wifi.dhcp_checkpoint = g_wifi.dhcp_pid;
      return g_wifi.dhcp_pid;
    }

  syslog(LOG_INFO, "A733 WIFI: DHCP started on wlan0 pid=%d\n",
         g_wifi.dhcp_pid);
  return OK;
}

static void a733_wifi_ipv4_clear(void)
{
  struct in_addr zero;

  zero.s_addr = 0;
  netlib_set_ipv4addr("wlan0", &zero);
  netlib_set_ipv4netmask("wlan0", &zero);
  netlib_set_dripv4addr("wlan0", &zero);
#ifdef CONFIG_NETDB_DNSCLIENT
  netlib_set_ipv4dnsaddr(&zero);
#endif
  g_wifi.dhcp_checkpoint = -ENETDOWN;
  g_wifi.dhcp_address = 0;
  g_wifi.dhcp_netmask = 0;
  g_wifi.dhcp_router = 0;
  g_wifi.dhcp_dns = 0;
  g_wifi.dhcp_lease = 0;
  g_wifi.gratuitous_arp_checkpoint = -ENETDOWN;
  g_wifi.gratuitous_arp_sent = 0;
}
#endif

static void a733_wifi_net_carrier(bool available)
{
  if (!g_wifi_netdev.registered)
    {
      return;
    }

  if (available && g_wifi_netdev.ifup)
    {
      netdev_carrier_on(&g_wifi_netdev.dev);
    }
  else
    {
      netdev_carrier_off(&g_wifi_netdev.dev);
    }
}
#endif

static uint16_t a733_getle16(const uint8_t *buffer)
{
  return buffer[0] | ((uint16_t)buffer[1] << 8);
}

static uint32_t a733_getle32(const uint8_t *buffer)
{
  return buffer[0] | ((uint32_t)buffer[1] << 8) |
         ((uint32_t)buffer[2] << 16) | ((uint32_t)buffer[3] << 24);
}

static void a733_putle16(uint8_t *buffer, uint16_t value)
{
  buffer[0] = value & 0xffu;
  buffer[1] = value >> 8;
}

static void a733_putle32(uint8_t *buffer, uint32_t value)
{
  buffer[0] = value & 0xffu;
  buffer[1] = (value >> 8) & 0xffu;
  buffer[2] = (value >> 16) & 0xffu;
  buffer[3] = value >> 24;
}

static inline void a733_modifyreg32(uintptr_t address, uint32_t clearbits,
                                    uint32_t setbits)
{
  putreg32((getreg32(address) & ~clearbits) | setbits, address);
}

static int a733_dma32(const void *pointer, uint32_t *address)
{
  uintptr_t value = (uintptr_t)pointer;

  if ((value >> 32) != 0)
    {
      return -EOVERFLOW;
    }

  *address = (uint32_t)value;
  return OK;
}

static int a733_qtd_buffer(struct ehci_qtd_s *qtd, const void *buffer,
                           size_t length)
{
  uintptr_t current = (uintptr_t)buffer;
  size_t remaining = length;
  unsigned int index;
  uint32_t address;
  int ret;

  memset(qtd->bpl, 0, sizeof(qtd->bpl));
  for (index = 0; index < 5 && remaining > 0; index++)
    {
      ret = a733_dma32((const void *)current, &address);
      if (ret < 0)
        {
          return ret;
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

static void a733_delay_ms(unsigned int milliseconds)
{
  while (milliseconds-- > 0)
    {
      up_udelay(1000);
    }
}

static int a733_wait_clear(uintptr_t address, uint32_t mask,
                           unsigned int timeout_ms)
{
  while ((getreg32(address) & mask) != 0)
    {
      if (timeout_ms-- == 0)
        {
          return -ETIMEDOUT;
        }

      a733_delay_ms(1);
    }

  return OK;
}

static void a733_wifi_power_enable(void)
{
  uint32_t reg;

  /* PM0 = WIFI_3V3 load-switch enable, PM1 = WL-REG-ON. */

  reg = getreg32(A733_PM_CFG0);
  reg &= ~0xffu;
  reg |= 0x11u;
  putreg32(reg, A733_PM_CFG0);

  reg = getreg32(A733_PM_DATA);
  reg |= 3u;
  putreg32(reg, A733_PM_DATA);
  a733_delay_ms(100);
}

static void a733_usb1_clock_enable(void)
{
  uint32_t reg;

  /* This is the sun60iw2 Linux clock/reset order, reduced to the USB1
   * resources used by the onboard module.  The shared AHB register has a
   * write key whose readback bits are always zero.
   */

  a733_modifyreg32(A733_CCU_RES_DCAP, 0, 1u << 3);
  a733_modifyreg32(A733_CCU_MSI_LITE2, 0, 1u << 0);

  reg = getreg32(A733_CCU_AHB_MASTER);
  putreg32(reg | UINT32_C(0x010000ff) | (1u << 9),
           A733_CCU_AHB_MASTER);

  reg = getreg32(A733_CCU_USB_REF);
  reg &= ~(7u << 24);
  putreg32(reg | (1u << 31), A733_CCU_USB_REF);

  /* Deassert PHY reset before enabling its leaf clock, then enable both
   * high-speed and companion host clocks/resets.
   */

  a733_modifyreg32(A733_CCU_USB1_PHY, 0, (1u << 30) | (1u << 31));
  a733_modifyreg32(A733_CCU_USB1_HCI, 0,
                   (1u << 20) | (1u << 16) | (1u << 4) | (1u << 0));
  up_udelay(20);

  /* A733 uses SIDDQ bit 3.  Enable the vendor AHB burst/pass-by settings
   * exactly as the official host driver does for sun60iw2.
   */

  a733_modifyreg32(A733_USB1_PHY_CTRL, 1u << 3, 0);
  a733_modifyreg32(A733_USB1_PMU, 0,
                   (1u << 15) | (1u << 11) | (1u << 9) |
                   (1u << 8) | (1u << 0));
  up_udelay(20);
}

static int a733_ehci1_checkpoint(void)
{
  uintptr_t opbase;
  uintptr_t usbcmd;
  uintptr_t usbsts;
  uintptr_t configflag;
  uintptr_t portsc;
  uint32_t reg;
  unsigned int timeout;
  int ret;

  g_wifi.caplength = getreg8(A733_EHCI1_BASE);
  g_wifi.hciversion = getreg16(A733_EHCI1_BASE + 2);
  g_wifi.hcsparams = getreg32(A733_EHCI1_BASE + 4);

  if (g_wifi.caplength < 0x10 || g_wifi.caplength > 0x80 ||
      g_wifi.hciversion == 0 || g_wifi.hciversion == 0xffff)
    {
      return -ENODEV;
    }

  opbase = A733_EHCI1_BASE + g_wifi.caplength;
  usbcmd = opbase + 0x00;
  usbsts = opbase + 0x04;
  configflag = opbase + 0x40;
  portsc = opbase + 0x44;

  a733_modifyreg32(usbcmd, EHCI_USBCMD_RUN, 0);
  timeout = 100;
  while ((getreg32(usbsts) & EHCI_USBSTS_HALTED) == 0 && timeout-- > 0)
    {
      a733_delay_ms(1);
    }

  a733_modifyreg32(usbcmd, 0, EHCI_USBCMD_HCRESET);
  ret = a733_wait_clear(usbcmd, EHCI_USBCMD_HCRESET, 100);
  if (ret < 0)
    {
      return ret;
    }

  putreg32(0, opbase + 0x08);       /* Interrupts remain polling-only. */
  putreg32(1, configflag);          /* Route enabled ports to EHCI. */

  reg = getreg32(portsc);
  reg &= ~((1u << 1) | (1u << 3) | (1u << 5)); /* Do not echo W1C bits. */
  putreg32(reg | EHCI_PORTSC_POWER, portsc);
  a733_modifyreg32(usbcmd, 0, EHCI_USBCMD_RUN);

  timeout = 600;
  while ((getreg32(portsc) & EHCI_PORTSC_CONNECT) == 0 && timeout-- > 0)
    {
      a733_delay_ms(1);
    }

  if ((getreg32(portsc) & EHCI_PORTSC_CONNECT) == 0)
    {
      return -ENODEV;
    }

  reg = getreg32(portsc);
  reg &= ~((1u << 1) | (1u << 3) | (1u << 5));
  putreg32(reg | EHCI_PORTSC_POWER | EHCI_PORTSC_RESET, portsc);
  a733_delay_ms(50);
  a733_modifyreg32(portsc, EHCI_PORTSC_RESET, 0);
  a733_delay_ms(20);

  g_wifi.usbcmd = getreg32(usbcmd);
  g_wifi.usbsts = getreg32(usbsts);
  g_wifi.portsc = getreg32(portsc);

  if ((g_wifi.portsc & EHCI_PORTSC_CONNECT) == 0)
    {
      return -ENODEV;
    }

  if ((g_wifi.portsc & EHCI_PORTSC_OWNER) != 0)
    {
      return -EOPNOTSUPP; /* Full/low-speed companion path, unexpected. */
    }

  return (g_wifi.portsc & EHCI_PORTSC_ENABLE) != 0 ? OK : -EIO;
}

static int a733_ehci_async_stop(uintptr_t usbcmd, uintptr_t usbsts)
{
  unsigned int timeout;

  a733_modifyreg32(usbcmd, EHCI_USBCMD_ASEN, 0);
  for (timeout = 0; timeout < 100; timeout++)
    {
      if ((getreg32(usbsts) & EHCI_USBSTS_ASS) == 0)
        {
          return OK;
        }

      a733_delay_ms(1);
    }

  return -ETIMEDOUT;
}

static int a733_ehci_control(uint8_t devaddr, uint16_t maxpacket,
                             const uint8_t setup[8], void *buffer,
                             size_t length, size_t *actual)
{
  struct ehci_qtd_s *setup_qtd = &g_wifi_qtd[0];
  struct ehci_qtd_s *data_qtd = &g_wifi_qtd[1];
  struct ehci_qtd_s *status_qtd = &g_wifi_qtd[2];
  uintptr_t opbase = A733_EHCI1_BASE + g_wifi.caplength;
  uintptr_t usbcmd = opbase + EHCI_USBCMD_OFFSET;
  uintptr_t usbsts = opbase + EHCI_USBSTS_OFFSET;
  uint32_t qh_address;
  uint32_t setup_address;
  uint32_t data_address;
  uint32_t status_address;
  uint32_t token;
  uint32_t residual = 0;
  unsigned int timeout;
  bool datain = (setup[0] & 0x80u) != 0;
  int ret;

  *actual = 0;
  if (length > A733_USB_CONFIG_MAX || maxpacket == 0 || maxpacket > 1024)
    {
      return -EINVAL;
    }

  ret = a733_dma32(&g_wifi_qh, &qh_address);
  if (ret < 0)
    {
      return ret;
    }

  ret = a733_dma32(setup_qtd, &setup_address);
  if (ret < 0)
    {
      return ret;
    }

  ret = a733_dma32(data_qtd, &data_address);
  if (ret < 0)
    {
      return ret;
    }

  ret = a733_dma32(status_qtd, &status_address);
  if (ret < 0)
    {
      return ret;
    }

  ret = a733_ehci_async_stop(usbcmd, usbsts);
  if (ret < 0)
    {
      return ret;
    }

  memset(&g_wifi_qh, 0, sizeof(g_wifi_qh));
  memset(g_wifi_qtd, 0, sizeof(g_wifi_qtd));
  memcpy(g_wifi_setup, setup, sizeof(g_wifi_setup));

  g_wifi_qh.hlp = qh_address | QH_HLP_TYP_QH;
  g_wifi_qh.epchar = ((uint32_t)devaddr << QH_EPCHAR_DEVADDR_SHIFT) |
                     QH_EPCHAR_EPS_HIGH | QH_EPCHAR_DTC |
                     QH_EPCHAR_H |
                     ((uint32_t)maxpacket << QH_EPCHAR_MAXPKT_SHIFT) |
                     (8u << QH_EPCHAR_RL_SHIFT);
  g_wifi_qh.epcaps = QH_EPCAPS_MULT(1);
  g_wifi_qh.overlay.nqp = setup_address;
  g_wifi_qh.overlay.alt = QH_AQP_T;

  setup_qtd->nqp = length > 0 ? data_address : status_address;
  setup_qtd->alt = QTD_AQP_T;
  setup_qtd->token = QTD_TOKEN_ACTIVE | QTD_TOKEN_PID_SETUP |
                     (3u << QTD_TOKEN_CERR_SHIFT) |
                     (8u << QTD_TOKEN_NBYTES_SHIFT);
  ret = a733_qtd_buffer(setup_qtd, g_wifi_setup, sizeof(g_wifi_setup));
  if (ret < 0)
    {
      return ret;
    }

  if (length > 0)
    {
      data_qtd->nqp = status_address;
      data_qtd->alt = datain ? status_address : QTD_AQP_T;
      data_qtd->token = QTD_TOKEN_ACTIVE |
                        (datain ? QTD_TOKEN_PID_IN : QTD_TOKEN_PID_OUT) |
                        (3u << QTD_TOKEN_CERR_SHIFT) |
                        ((uint32_t)length << QTD_TOKEN_NBYTES_SHIFT) |
                        QTD_TOKEN_TOGGLE;
      ret = a733_qtd_buffer(data_qtd, buffer, length);
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

  status_qtd->nqp = QTD_NQP_T;
  status_qtd->alt = QTD_AQP_T;
  status_qtd->token = QTD_TOKEN_ACTIVE |
                      (datain ? QTD_TOKEN_PID_OUT : QTD_TOKEN_PID_IN) |
                      (3u << QTD_TOKEN_CERR_SHIFT) | QTD_TOKEN_IOC |
                      QTD_TOKEN_TOGGLE;

  up_clean_dcache((uintptr_t)g_wifi_setup,
                  (uintptr_t)g_wifi_setup + sizeof(g_wifi_setup));
  up_clean_dcache((uintptr_t)g_wifi_qtd,
                  (uintptr_t)g_wifi_qtd + sizeof(g_wifi_qtd));
  up_clean_dcache((uintptr_t)&g_wifi_qh,
                  (uintptr_t)&g_wifi_qh + sizeof(g_wifi_qh));

  putreg32(0, opbase + EHCI_CTRLDSSEGMENT_OFFSET);
  putreg32(qh_address, opbase + EHCI_ASYNCLISTADDR_OFFSET);
  putreg32(EHCI_INT_ALLINTS, usbsts);
  a733_modifyreg32(usbcmd, 0, EHCI_USBCMD_ASEN | EHCI_USBCMD_RUN);

  ret = -ETIMEDOUT;
  for (timeout = 0; timeout < A733_USB_TIMEOUT_MS; timeout++)
    {
      up_invalidate_dcache((uintptr_t)status_qtd,
                           (uintptr_t)status_qtd + sizeof(*status_qtd));
      token = status_qtd->token;
      if ((token & QTD_TOKEN_ACTIVE) == 0)
        {
          ret = (token & QTD_TOKEN_ERRORS) != 0 ? -EIO : OK;
          break;
        }

      a733_delay_ms(1);
    }

  a733_ehci_async_stop(usbcmd, usbsts);
  up_invalidate_dcache((uintptr_t)setup_qtd,
                       (uintptr_t)setup_qtd + sizeof(*setup_qtd));
  if ((setup_qtd->token & QTD_TOKEN_ERRORS) != 0)
    {
      ret = -EIO;
    }

  if (length > 0)
    {
      up_invalidate_dcache((uintptr_t)data_qtd,
                           (uintptr_t)data_qtd + sizeof(*data_qtd));
      if ((data_qtd->token & QTD_TOKEN_ERRORS) != 0)
        {
          ret = -EIO;
        }

      residual = (data_qtd->token & QTD_TOKEN_NBYTES_MASK) >>
                 QTD_TOKEN_NBYTES_SHIFT;
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

/* Execute one polling-only high-speed bulk transaction.  The boot-ROM
 * endpoints both start at DATA0 after SET_CONFIGURATION.  This checkpoint
 * performs exactly one transfer in each direction.
 */

static int a733_ehci_bulk_timeout(uint8_t devaddr, uint8_t endpoint,
                                  uint16_t maxpacket, bool datain,
                                  void *buffer, size_t length,
                                  size_t *actual, bool *toggle,
                                  unsigned int timeout_ms)
{
  struct ehci_qtd_s *qtd = &g_wifi_qtd[0];
  uintptr_t opbase = A733_EHCI1_BASE + g_wifi.caplength;
  uintptr_t usbcmd = opbase + EHCI_USBCMD_OFFSET;
  uintptr_t usbsts = opbase + EHCI_USBSTS_OFFSET;
  uint32_t qh_address;
  uint32_t qtd_address;
  uint32_t token;
  uint32_t residual;
  unsigned int timeout;
  int ret;

  *actual = 0;
  if (endpoint == 0 || endpoint > 15 || maxpacket == 0 ||
      maxpacket > 1024 || length == 0 || length > 0x7fffu ||
      timeout_ms == 0)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&g_wifi_usb_lock);
  if (ret < 0)
    {
      return ret;
    }

  ret = a733_dma32(&g_wifi_qh, &qh_address);
  if (ret < 0)
    {
      goto out_unlock;
    }

  ret = a733_dma32(qtd, &qtd_address);
  if (ret < 0)
    {
      goto out_unlock;
    }

  ret = a733_ehci_async_stop(usbcmd, usbsts);
  if (ret < 0)
    {
      goto out_unlock;
    }

  memset(&g_wifi_qh, 0, sizeof(g_wifi_qh));
  memset(g_wifi_qtd, 0, sizeof(g_wifi_qtd));
  g_wifi_qh.hlp = qh_address | QH_HLP_TYP_QH;
  g_wifi_qh.epchar = ((uint32_t)devaddr << QH_EPCHAR_DEVADDR_SHIFT) |
                     ((uint32_t)endpoint << QH_EPCHAR_ENDPT_SHIFT) |
                     QH_EPCHAR_EPS_HIGH | QH_EPCHAR_DTC | QH_EPCHAR_H |
                     ((uint32_t)maxpacket << QH_EPCHAR_MAXPKT_SHIFT) |
                     (8u << QH_EPCHAR_RL_SHIFT);
  g_wifi_qh.epcaps = QH_EPCAPS_MULT(1);
  g_wifi_qh.overlay.nqp = qtd_address;
  g_wifi_qh.overlay.alt = QH_AQP_T;

  qtd->nqp = QTD_NQP_T;
  qtd->alt = QTD_AQP_T;
  qtd->token = QTD_TOKEN_ACTIVE |
               (datain ? QTD_TOKEN_PID_IN : QTD_TOKEN_PID_OUT) |
               (3u << QTD_TOKEN_CERR_SHIFT) | QTD_TOKEN_IOC |
               (*toggle ? QTD_TOKEN_TOGGLE : 0) |
               ((uint32_t)length << QTD_TOKEN_NBYTES_SHIFT);
  ret = a733_qtd_buffer(qtd, buffer, length);
  if (ret < 0)
    {
      goto out_unlock;
    }

  if (datain)
    {
      memset(buffer, 0, length);
      up_flush_dcache((uintptr_t)buffer, (uintptr_t)buffer + length);
    }
  else
    {
      up_clean_dcache((uintptr_t)buffer, (uintptr_t)buffer + length);
    }

  up_clean_dcache((uintptr_t)g_wifi_qtd,
                  (uintptr_t)g_wifi_qtd + sizeof(g_wifi_qtd));
  up_clean_dcache((uintptr_t)&g_wifi_qh,
                  (uintptr_t)&g_wifi_qh + sizeof(g_wifi_qh));

  putreg32(0, opbase + EHCI_CTRLDSSEGMENT_OFFSET);
  putreg32(qh_address, opbase + EHCI_ASYNCLISTADDR_OFFSET);
  putreg32(EHCI_INT_ALLINTS, usbsts);
  a733_modifyreg32(usbcmd, 0, EHCI_USBCMD_ASEN | EHCI_USBCMD_RUN);

  ret = -ETIMEDOUT;
  for (timeout = 0; timeout < timeout_ms; timeout++)
    {
      up_invalidate_dcache((uintptr_t)qtd,
                           (uintptr_t)qtd + sizeof(*qtd));
      token = qtd->token;
      if ((token & QTD_TOKEN_ACTIVE) == 0)
        {
          ret = (token & QTD_TOKEN_ERRORS) != 0 ? -EIO : OK;
          break;
        }

      /* Runtime bulk I/O executes from schedulable task context.  A busy
       * microsecond delay here made the always-on WLAN RX worker consume most
       * of one CPU while an IN endpoint was NAKing, which progressively made
       * NSH and service tasks appear frozen. */

      nxsig_usleep(1000);
    }

  a733_ehci_async_stop(usbcmd, usbsts);
  up_invalidate_dcache((uintptr_t)qtd,
                       (uintptr_t)qtd + sizeof(*qtd));
  token = qtd->token;

  /* The loop checks ACTIVE before each one-millisecond delay.  With a
   * one-millisecond timeout a transfer that completed during that delay was
   * previously reported as timed out without one final observation.  This
   * made short data-endpoint polls effectively zero-length and could discard
   * the AP's EAPOL Message 1.  Always account for completion in the final
   * interval before interpreting errors and residual length.
   */

  if (ret == -ETIMEDOUT && (token & QTD_TOKEN_ACTIVE) == 0)
    {
      ret = (token & QTD_TOKEN_ERRORS) != 0 ? -EIO : OK;
    }

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

  if (ret == OK)
    {
      *toggle = (token & QTD_TOKEN_TOGGLE) != 0;
    }

out_unlock:
  nxmutex_unlock(&g_wifi_usb_lock);
  return ret;
}

static int a733_ehci_bulk(uint8_t devaddr, uint8_t endpoint,
                          uint16_t maxpacket, bool datain, void *buffer,
                          size_t length, size_t *actual, bool *toggle)
{
  return a733_ehci_bulk_timeout(devaddr, endpoint, maxpacket, datain,
                                buffer, length, actual, toggle,
                                A733_AIC_TIMEOUT_MS);
}

static int a733_aic_command(uint16_t request, const void *parameters,
                            uint16_t parameter_length,
                            uint16_t confirmation, uint8_t *reply,
                            size_t reply_size, size_t *reply_length,
                            bool wait_confirmation)
{
  size_t actual;
  uint16_t packet_length;
  uint16_t response_length;
  size_t command_length = 16u + parameter_length;
  int ret;

  if (g_wifi.vid != A733_AIC_VID || g_wifi.pid != A733_AIC_BOOT_PID ||
      g_wifi.bulk_out == 0 || g_wifi.bulk_in == 0 ||
      g_wifi.bulk_maxpacket == 0)
    {
      return -ENODEV;
    }

  if (command_length > sizeof(g_wifi_aic_tx) ||
      (wait_confirmation && (reply == NULL || reply_size < 16)))
    {
      return -E2BIG;
    }

  memset(g_wifi_aic_tx, 0, sizeof(g_wifi_aic_tx));
  a733_putle16(&g_wifi_aic_tx[0], 12u + parameter_length);
  g_wifi_aic_tx[2] = A733_AIC_USB_TYPE_CMD;
  a733_putle16(&g_wifi_aic_tx[8], request);
  a733_putle16(&g_wifi_aic_tx[10], A733_AIC_TASK_DBG);
  a733_putle16(&g_wifi_aic_tx[12], A733_AIC_DRIVER_TASK);
  a733_putle16(&g_wifi_aic_tx[14], parameter_length);
  if (parameter_length > 0)
    {
      memcpy(&g_wifi_aic_tx[16], parameters, parameter_length);
    }

  ret = a733_ehci_bulk(g_wifi.address, g_wifi.bulk_out,
                       g_wifi.bulk_maxpacket, false, g_wifi_aic_tx,
                       command_length, &actual, &g_wifi.bulk_out_toggle);
  g_wifi.command_actual = actual;
  if (ret < 0 || actual != command_length)
    {
      return ret < 0 ? ret : -EPROTO;
    }

  if (!wait_confirmation)
    {
      if (reply_length != NULL)
        {
          *reply_length = 0;
        }

      return OK;
    }

  ret = a733_ehci_bulk(g_wifi.address, g_wifi.bulk_in,
                       g_wifi.bulk_maxpacket, true, reply, reply_size,
                       &actual, &g_wifi.bulk_in_toggle);
  g_wifi.response_actual = actual;
  if (ret < 0)
    {
      return ret;
    }

  /* RX framing is the official aic_txrxif config response: four-byte USB
   * header followed by ipc_e2a_msg.  Its fixed prefix is 12 bytes and the
   * read confirmation contributes memaddr + memdata.
   */

  if (actual < 16 || (reply[2] & 0x7fu) != A733_AIC_USB_TYPE_CMD)
    {
      return -EPROTO;
    }

  packet_length = a733_getle16(reply);
  g_wifi.response_id = a733_getle16(&reply[4]);
  response_length = a733_getle16(&reply[10]);
  if ((size_t)packet_length + 4 > actual ||
      g_wifi.response_id != confirmation ||
      (size_t)response_length + 16 > actual)
    {
      return -EPROTO;
    }

  if (reply_length != NULL)
    {
      *reply_length = response_length;
    }

  return OK;
}

static int a733_aic_bootrom_read32(uint32_t address, uint32_t *value)
{
  uint8_t parameter[4];
  size_t length;
  int ret;

  a733_putle32(parameter, address);
  ret = a733_aic_command(A733_AIC_DBG_MEM_READ_REQ, parameter,
                         sizeof(parameter), A733_AIC_DBG_MEM_READ_CFM,
                         g_wifi_aic_rx, sizeof(g_wifi_aic_rx), &length,
                         true);
  if (ret < 0)
    {
      return ret;
    }

  if (length < 8 || a733_getle32(&g_wifi_aic_rx[16]) != address)
    {
      return -EPROTO;
    }

  *value = a733_getle32(&g_wifi_aic_rx[20]);
  return OK;
}

static int a733_aic_bootrom_write32(uint32_t address, uint32_t value)
{
  uint8_t parameter[8];
  size_t length;
  int ret;

  a733_putle32(parameter, address);
  a733_putle32(parameter + 4, value);
  ret = a733_aic_command(A733_AIC_DBG_MEM_WRITE_REQ, parameter,
                         sizeof(parameter), A733_AIC_DBG_MEM_WRITE_CFM,
                         g_wifi_aic_rx, sizeof(g_wifi_aic_rx), &length,
                         true);
  if (ret < 0)
    {
      return ret;
    }

  /* The official loader passes NULL for DBG_MEM_WRITE_CFM and treats a
   * correctly framed 0x0403 confirmation as success.  Some D80 BootROM
   * revisions do not echo the requested value verbatim, so imposing a
   * readback comparison here incorrectly rejects successful writes.
   */

  if (length != 0 && length < 8)
    {
      return -EPROTO;
    }

  return OK;
}

static int a733_aic_bootrom_block_write(uint32_t address,
                                         const uint8_t *data,
                                         size_t length)
{
  size_t response_length;
  int ret;

  if (length == 0 || length > A733_AIC_BLOCK_SIZE)
    {
      return -EINVAL;
    }

  /* The file reader normally passes g_wifi_aic_block itself.  Build the
   * fixed request directly in the independent parameter buffer: clearing
   * g_wifi_aic_block here would erase the source before it is copied.
   */

  memset(g_wifi_aic_parameter, 0, sizeof(g_wifi_aic_parameter));
  a733_putle32(g_wifi_aic_parameter, address);
  a733_putle32(g_wifi_aic_parameter + 4, length);

  /* aicbt_patch_table_load sends the complete fixed-size dbg_mem_block_write
   * structure, including zero-padded bytes after the final short block.
   */

  memcpy(&g_wifi_aic_parameter[8], data, length);
  ret = a733_aic_command(A733_AIC_DBG_BLOCK_REQ, g_wifi_aic_parameter,
                         8 + A733_AIC_BLOCK_SIZE,
                         A733_AIC_DBG_BLOCK_CFM, g_wifi_aic_rx,
                         sizeof(g_wifi_aic_rx), &response_length, true);
  if (ret < 0)
    {
      return ret;
    }

  return response_length >= 4 && a733_getle32(&g_wifi_aic_rx[16]) != 0 ?
         -EIO : OK;
}

static int a733_aic_bootrom_start(uint32_t address)
{
  uint8_t parameter[8];

  a733_putle32(parameter, address);
  a733_putle32(parameter + 4, 1); /* HOST_START_APP_AUTO */
  return a733_aic_command(A733_AIC_DBG_START_REQ, parameter,
                          sizeof(parameter), 0, NULL, 0, NULL, false);
}

static int a733_aic_upload_file(const char *path, uint32_t address,
                                size_t expected_size)
{
  struct file filep;
  size_t offset = 0;
  ssize_t nread;
  uint8_t extra;
  int ret;

  ret = file_open(&filep, path, O_RDONLY);
  if (ret < 0)
    {
      return ret;
    }

  while (offset < expected_size)
    {
      size_t wanted = expected_size - offset;

      if (wanted > sizeof(g_wifi_aic_block))
        {
          wanted = sizeof(g_wifi_aic_block);
        }

      nread = file_read(&filep, g_wifi_aic_block, wanted);
      if (nread <= 0)
        {
          ret = nread < 0 ? (int)nread : -EIO;
          goto out;
        }

      ret = a733_aic_bootrom_block_write(address + offset,
                                          g_wifi_aic_block,
                                          (size_t)nread);
      if (ret < 0)
        {
          goto out;
        }

      offset += (size_t)nread;
    }

  /* Refuse a similarly named file from another AIC branch. */

  nread = file_read(&filep, &extra, 1);
  ret = nread == 0 ? OK : (nread < 0 ? (int)nread : -EFBIG);
  if (ret == OK)
    {
      g_wifi.firmware_bytes += offset;
      syslog(LOG_INFO, "A733 WIFI: uploaded %s (%lu bytes) to %08lx\n",
             path, (unsigned long)offset, (unsigned long)address);
    }

out:
  file_close(&filep);
  return ret;
}

static int a733_aic_read_file(const char *path, uint8_t *buffer,
                              size_t expected_size)
{
  struct file filep;
  size_t offset = 0;
  ssize_t nread;
  uint8_t extra;
  int ret;

  ret = file_open(&filep, path, O_RDONLY);
  if (ret < 0)
    {
      return ret;
    }

  while (offset < expected_size)
    {
      nread = file_read(&filep, buffer + offset, expected_size - offset);
      if (nread <= 0)
        {
          ret = nread < 0 ? (int)nread : -EIO;
          goto out;
        }

      offset += (size_t)nread;
    }

  nread = file_read(&filep, &extra, 1);
  ret = nread == 0 ? OK : (nread < 0 ? (int)nread : -EFBIG);

out:
  file_close(&filep);
  return ret;
}

static int a733_aic_apply_patch_table(void)
{
  size_t offset = 16;
  int ret;

  ret = a733_aic_read_file(A733_AIC_PATCH_TABLE_FILE,
                           g_wifi_patch_table, 1384);
  if (ret < 0)
    {
      return ret;
    }

  if (memcmp(g_wifi_patch_table, "AICBT_PT_TAG", 12) != 0)
    {
      return -EPROTO;
    }

  while (offset < 1384)
    {
      uint32_t type;
      uint32_t pairs;
      uint32_t index;

      if (offset + 24 > 1384)
        {
          return -EPROTO;
        }

      type = a733_getle32(&g_wifi_patch_table[offset + 16]);
      pairs = a733_getle32(&g_wifi_patch_table[offset + 20]);
      offset += 24;
      if (pairs == 0 || pairs > (1384 - offset) / 8)
        {
          return -EPROTO;
        }

      /* aicbt_patch_info_unpack deliberately shortens INF to four pairs;
       * the two following pairs describe ext_patch_nb/ext0 and are metadata.
       */

      if (type == 0 && pairs > 4)
        {
          pairs = 4;
        }

      if (type != 6)
        {
          for (index = 0; index < pairs; index++)
            {
              uint32_t address =
                a733_getle32(&g_wifi_patch_table[offset + index * 8]);
              uint32_t value =
                a733_getle32(&g_wifi_patch_table[offset + index * 8 + 4]);

              if (type == 3 && index == 0)
                {
                  value = 1; /* hwinfo < 0 */
                }
              else if (type == 3 && index == 1)
                {
                  value = UINT32_C(0xffffffff); /* hwinfo = -1 */
                }

              ret = a733_aic_bootrom_write32(address, value);
              if (ret < 0)
                {
                  return ret;
                }
            }
        }

      /* Advance by the on-disk pair count, not the shortened INF count. */

      if (type == 0)
        {
          offset += 6 * 8;
        }
      else
        {
          offset += pairs * 8;
        }

      if (type == 4)
        {
          a733_delay_ms(100);
        }
    }

  return offset == 1384 ? OK : -EPROTO;
}

static int a733_aic_patch_config_d80(void)
{
  uint32_t config_base;
  uint32_t patch_struct;
  uint32_t patch_base = UINT32_C(0x001d7000);
  uint32_t version;
  static const uint32_t offsets[3] = {0x00b4, 0x0170, 0x0188};
  static const uint32_t values[3] =
    {UINT32_C(0xf3010000), UINT32_C(0x0001000a), UINT32_C(0x00000001)};
  unsigned int index;
  int ret;

  ret = a733_aic_bootrom_read32(A733_AIC_FMAC_ADDRESS + 0x198,
                                &config_base);
  if (ret < 0)
    {
      return ret;
    }

  ret = a733_aic_bootrom_read32(A733_AIC_FMAC_ADDRESS + 0x1a0,
                                &patch_struct);
  if (ret < 0)
    {
      return ret;
    }

  ret = a733_aic_bootrom_read32(A733_AIC_FMAC_ADDRESS + 0x01c, &version);
  if (ret < 0)
    {
      return ret;
    }

  /* These values are exported by the exact official normal U02 image.  A
   * zero value means the bulk protocol completed but payload bytes did not
   * reach RAM; never continue into patch writes or START_APP in that state.
   */

  if (config_base != UINT32_C(0x00177158) ||
      patch_struct != UINT32_C(0x00177c00) ||
      version != UINT32_C(0x06090101))
    {
      syslog(LOG_ERR,
             "A733 WIFI: FMAC readback mismatch cfg=%08lx struct=%08lx "
             "version=%08lx\n", (unsigned long)config_base,
             (unsigned long)patch_struct, (unsigned long)version);
      return -EILSEQ;
    }

  if (version > UINT32_C(0x06090100))
    {
      ret = a733_aic_bootrom_read32(A733_AIC_FMAC_ADDRESS + 0x1a4,
                                    &patch_base);
      if (ret < 0)
        {
          return ret;
        }
    }

  ret = a733_aic_bootrom_write32(patch_struct + 0, UINT32_C(0x48435450));
  ret = ret < 0 ? ret : a733_aic_bootrom_write32(patch_struct + 4,
                                                  patch_base);
  ret = ret < 0 ? ret : a733_aic_bootrom_write32(patch_struct + 8,
                                                  UINT32_C(0x50544348));
  ret = ret < 0 ? ret : a733_aic_bootrom_write32(patch_struct + 12, 3);
  if (ret < 0)
    {
      return ret;
    }

  for (index = 0; index < 12; index++)
    {
      ret = a733_aic_bootrom_write32(patch_struct + 16 + index * 4, 0);
      if (ret < 0)
        {
          return ret;
        }
    }

  for (index = 0; index < 3; index++)
    {
      ret = a733_aic_bootrom_write32(patch_base + index * 8,
                                      config_base + offsets[index]);
      ret = ret < 0 ? ret :
            a733_aic_bootrom_write32(patch_base + index * 8 + 4,
                                      values[index]);
      if (ret < 0)
        {
          return ret;
        }
    }

  syslog(LOG_INFO,
         "A733 WIFI: D80 patch config version=%08lx cfg=%08lx "
         "struct=%08lx pairs=%08lx\n",
         (unsigned long)version, (unsigned long)config_base,
         (unsigned long)patch_struct, (unsigned long)patch_base);
  return OK;
}

static int a733_aic_load_firmware(void)
{
  int ret;

  g_wifi.chip_revision = (g_wifi.probe_value >> 16) & 0xffu;
  g_wifi.firmware_bytes = 0;
  if (g_wifi.chip_revision != 0x07)
    {
      syslog(LOG_WARNING,
             "A733 WIFI: refusing D80 U02 firmware for chip revision %02x\n",
             g_wifi.chip_revision);
      return -ENOTSUP;
    }

  ret = a733_aic_upload_file(A733_AIC_ADID_FILE, UINT32_C(0x00201940),
                              1708);
  ret = ret < 0 ? ret :
        a733_aic_upload_file(A733_AIC_PATCH_FILE, UINT32_C(0x001e0000),
                              32700);
  ret = ret < 0 ? ret :
        a733_aic_upload_file(A733_AIC_PATCH_EXT0_FILE,
                              UINT32_C(0x0020b43c), 16136);
  ret = ret < 0 ? ret : a733_aic_apply_patch_table();
  ret = ret < 0 ? ret :
        a733_aic_upload_file(A733_AIC_FMAC_FILE, A733_AIC_FMAC_ADDRESS,
                              358072);
  ret = ret < 0 ? ret : a733_aic_patch_config_d80();
  ret = ret < 0 ? ret : a733_aic_bootrom_start(A733_AIC_FMAC_ADDRESS);
  if (ret < 0)
    {
      return ret;
    }

  syslog(LOG_INFO,
         "A733 WIFI: D80 firmware started, waiting for runtime USB device\n");
  return OK;
}

static int a733_usb_no_data(uint8_t address, uint16_t maxpacket,
                            uint8_t request, uint16_t value)
{
  uint8_t setup[8] = {0};
  size_t actual;

  setup[1] = request;
  setup[2] = value & 0xffu;
  setup[3] = value >> 8;
  return a733_ehci_control(address, maxpacket, setup, NULL, 0, &actual);
}

static int a733_wifi_parse_config(const uint8_t *config, size_t length)
{
  unsigned int offset = 0;
  uint8_t interface = 0xff;
  uint8_t alternate = 0;

  g_wifi.interface_descriptors = 0;
  g_wifi.endpoint_count = 0;
  while (offset + 2 <= length)
    {
      unsigned int descriptor_length = config[offset];
      unsigned int descriptor_type = config[offset + 1];

      if (descriptor_length < 2 || offset + descriptor_length > length)
        {
          return -EPROTO;
        }

      if (descriptor_type == USB_DESC_INTERFACE && descriptor_length >= 9)
        {
          interface = config[offset + 2];
          alternate = config[offset + 3];
          g_wifi.interface_descriptors++;
          syslog(LOG_INFO,
                 "A733 WIFI: interface=%u alt=%u class=%02x/%02x/%02x "
                 "endpoints=%u\n",
                 interface, alternate, config[offset + 5],
                 config[offset + 6], config[offset + 7],
                 config[offset + 4]);
        }
      else if (descriptor_type == USB_DESC_ENDPOINT &&
               descriptor_length >= 7)
        {
          if (g_wifi.endpoint_count < A733_USB_ENDPOINT_MAX)
            {
              struct a733_wifi_endpoint_s *endpoint =
                &g_wifi.endpoints[g_wifi.endpoint_count++];

              endpoint->interface = interface;
              endpoint->alternate = alternate;
              endpoint->address = config[offset + 2];
              endpoint->attributes = config[offset + 3];
              endpoint->maxpacket = config[offset + 4] |
                                    ((uint16_t)config[offset + 5] << 8);
              endpoint->interval = config[offset + 6];
              syslog(LOG_INFO,
                     "A733 WIFI: endpoint=%02x attr=%02x maxpacket=%u "
                     "interval=%u interface=%u/%u\n",
                     endpoint->address, endpoint->attributes,
                     endpoint->maxpacket, endpoint->interval,
                     endpoint->interface, endpoint->alternate);
            }
        }

      offset += descriptor_length;
    }

  return offset == length ? OK : -EPROTO;
}

static int a733_wifi_enumerate(void)
{
  uint8_t setup[8] = {0};
  size_t actual;
  uint16_t total;
  unsigned int index;
  int ret;

  memset(g_wifi_config, 0, sizeof(g_wifi_config));
  setup[0] = 0x80;
  setup[1] = USB_REQ_GET_DESCRIPTOR;
  setup[3] = USB_DESC_DEVICE;
  setup[6] = 8;
  ret = a733_ehci_control(0, 64, setup, g_wifi_config, 8, &actual);
  if (ret < 0 || actual != 8 || g_wifi_config[1] != USB_DESC_DEVICE)
    {
      return ret < 0 ? ret : -EPROTO;
    }

  g_wifi.ep0_maxpacket = g_wifi_config[7];
  if (g_wifi.ep0_maxpacket != 64)
    {
      return -EPROTO;
    }

  ret = a733_usb_no_data(0, g_wifi.ep0_maxpacket,
                         USB_REQ_SET_ADDRESS, A733_USB_ADDRESS);
  if (ret < 0)
    {
      return ret;
    }

  a733_delay_ms(10);
  g_wifi.address = A733_USB_ADDRESS;
  memset(g_wifi_config, 0, sizeof(g_wifi_config));
  setup[6] = 18;
  ret = a733_ehci_control(g_wifi.address, g_wifi.ep0_maxpacket, setup,
                          g_wifi_config, 18, &actual);
  if (ret < 0 || actual != 18 || g_wifi_config[0] != 18 ||
      g_wifi_config[1] != USB_DESC_DEVICE)
    {
      return ret < 0 ? ret : -EPROTO;
    }

  g_wifi.bcdusb = g_wifi_config[2] | ((uint16_t)g_wifi_config[3] << 8);
  g_wifi.device_class = g_wifi_config[4];
  g_wifi.device_subclass = g_wifi_config[5];
  g_wifi.device_protocol = g_wifi_config[6];
  g_wifi.vid = g_wifi_config[8] | ((uint16_t)g_wifi_config[9] << 8);
  g_wifi.pid = g_wifi_config[10] | ((uint16_t)g_wifi_config[11] << 8);

  syslog(LOG_INFO,
         "A733 WIFI: USB device addr=%u VID:PID=%04x:%04x "
         "class=%02x/%02x/%02x bcdUSB=%x.%02x EP0=%u\n",
         g_wifi.address, g_wifi.vid, g_wifi.pid, g_wifi.device_class,
         g_wifi.device_subclass, g_wifi.device_protocol,
         g_wifi.bcdusb >> 8, g_wifi.bcdusb & 0xffu,
         g_wifi.ep0_maxpacket);

  memset(g_wifi_config, 0, sizeof(g_wifi_config));
  setup[3] = USB_DESC_CONFIG;
  setup[6] = 9;
  ret = a733_ehci_control(g_wifi.address, g_wifi.ep0_maxpacket, setup,
                          g_wifi_config, 9, &actual);
  if (ret < 0 || actual != 9 || g_wifi_config[1] != USB_DESC_CONFIG)
    {
      return ret < 0 ? ret : -EPROTO;
    }

  total = g_wifi_config[2] | ((uint16_t)g_wifi_config[3] << 8);
  if (total < 9 || total > sizeof(g_wifi_config))
    {
      return -E2BIG;
    }

  g_wifi.configuration = g_wifi_config[5];
  g_wifi.interface_count = g_wifi_config[4];
  g_wifi.config_length = total;
  memset(g_wifi_config, 0, sizeof(g_wifi_config));
  setup[6] = total & 0xffu;
  setup[7] = total >> 8;
  ret = a733_ehci_control(g_wifi.address, g_wifi.ep0_maxpacket, setup,
                          g_wifi_config, total, &actual);
  if (ret < 0 || actual != total)
    {
      return ret < 0 ? ret : -EPROTO;
    }

  ret = a733_wifi_parse_config(g_wifi_config, total);
  if (ret < 0)
    {
      return ret;
    }

  ret = a733_usb_no_data(g_wifi.address, g_wifi.ep0_maxpacket,
                         USB_REQ_SET_CONFIGURATION,
                         g_wifi.configuration);
  if (ret < 0)
    {
      return ret;
    }

  a733_delay_ms(10);
  g_wifi.bulk_out_toggle = false;
  g_wifi.bulk_in_toggle = false;

  /* The FCU760K boot ROM exposes one vendor interface: one bulk OUT and one
   * bulk IN endpoint.  Discover rather than hard-code them, but reject an
   * ambiguous topology before sending any vendor command.
   */

  if (g_wifi.vid != A733_AIC_VID ||
      (g_wifi.pid != A733_AIC_BOOT_PID &&
       g_wifi.pid != A733_AIC_RUNTIME_PID))
    {
      return -ENODEV;
    }

  g_wifi.bulk_out = 0;
  g_wifi.bulk_in = 0;
  g_wifi.bulk_maxpacket = 0;
  for (index = 0; index < g_wifi.endpoint_count; index++)
    {
      struct a733_wifi_endpoint_s *endpoint = &g_wifi.endpoints[index];

      if ((endpoint->attributes & 3u) != 2u)
        {
          continue;
        }

      if ((endpoint->address & 0x80u) != 0)
        {
          if (g_wifi.pid == A733_AIC_BOOT_PID && g_wifi.bulk_in != 0)
            {
              return -EPROTO;
            }

          if (g_wifi.bulk_in == 0)
            {
              g_wifi.bulk_in = endpoint->address & 0x0fu;
            }
        }
      else
        {
          if (g_wifi.pid == A733_AIC_BOOT_PID && g_wifi.bulk_out != 0)
            {
              return -EPROTO;
            }

          if (g_wifi.bulk_out == 0)
            {
              g_wifi.bulk_out = endpoint->address & 0x0fu;
            }
        }

      if (g_wifi.pid == A733_AIC_BOOT_PID && g_wifi.bulk_maxpacket == 0)
        {
          g_wifi.bulk_maxpacket = endpoint->maxpacket;
        }
      else if (g_wifi.pid == A733_AIC_BOOT_PID &&
               g_wifi.bulk_maxpacket != endpoint->maxpacket)
        {
          return -EPROTO;
        }
    }

  if (g_wifi.pid == A733_AIC_BOOT_PID &&
      (g_wifi.bulk_out == 0 || g_wifi.bulk_in == 0 ||
       g_wifi.bulk_maxpacket == 0))
    {
      return -ENODEV;
    }

  syslog(LOG_INFO,
         "A733 WIFI: USB enumeration passed config=%u interfaces=%u "
         "descriptors=%u endpoints=%u total=%u\n",
         g_wifi.configuration, g_wifi.interface_count,
         g_wifi.interface_descriptors, g_wifi.endpoint_count,
         g_wifi.config_length);
  return OK;
}

/* Decode the runtime composite device by interface ownership.  Interface 0
 * is Bluetooth HCI, interface 1 is Bluetooth isochronous audio, and the
 * vendor-specific interface 2 is the Wi-Fi full-MAC transport.  Keeping
 * these endpoints separate is essential: endpoint numbers overlap between
 * the boot loader and runtime configurations.
 */

static int a733_wifi_runtime_map(void)
{
  unsigned int index;

  g_wifi.wifi_data_out = 0;
  g_wifi.wifi_data_in = 0;
  g_wifi.wifi_msg_out = 0;
  g_wifi.wifi_msg_in = 0;
  g_wifi.bt_event_in = 0;
  g_wifi.bt_acl_out = 0;
  g_wifi.bt_acl_in = 0;
  g_wifi.runtime_maxpacket = 0;

  for (index = 0; index < g_wifi.endpoint_count; index++)
    {
      struct a733_wifi_endpoint_s *endpoint = &g_wifi.endpoints[index];
      uint8_t number = endpoint->address & 0x0fu;
      bool input = (endpoint->address & 0x80u) != 0;
      uint8_t type = endpoint->attributes & 3u;

      if (endpoint->alternate != 0)
        {
          continue;
        }

      if (endpoint->interface == 0)
        {
          if (type == 3u && input)
            {
              g_wifi.bt_event_in = number;
            }
          else if (type == 2u && input)
            {
              g_wifi.bt_acl_in = number;
            }
          else if (type == 2u && !input)
            {
              g_wifi.bt_acl_out = number;
            }
        }
      else if (endpoint->interface == 2 && type == 2u)
        {
          if (endpoint->maxpacket != 512u)
            {
              return -EPROTO;
            }

          g_wifi.runtime_maxpacket = endpoint->maxpacket;
          if (!input && g_wifi.wifi_data_out == 0)
            {
              g_wifi.wifi_data_out = number;
            }
          else if (input && g_wifi.wifi_data_in == 0)
            {
              g_wifi.wifi_data_in = number;
            }
          else if (!input && g_wifi.wifi_msg_out == 0)
            {
              g_wifi.wifi_msg_out = number;
            }
          else if (input && g_wifi.wifi_msg_in == 0)
            {
              g_wifi.wifi_msg_in = number;
            }
        }
    }

  if (g_wifi.wifi_data_out == 0 || g_wifi.wifi_data_in == 0 ||
      g_wifi.wifi_msg_out == 0 || g_wifi.wifi_msg_in == 0 ||
      g_wifi.bt_event_in == 0 || g_wifi.bt_acl_out == 0 ||
      g_wifi.bt_acl_in == 0 || g_wifi.runtime_maxpacket == 0)
    {
      return -ENODEV;
    }

  g_wifi.wifi_data_out_toggle = false;
  g_wifi.wifi_data_in_toggle = false;
  g_wifi.wifi_msg_out_toggle = false;
  g_wifi.wifi_msg_in_toggle = false;

  syslog(LOG_INFO,
         "A733 WIFI: runtime map WLAN data=%02x/%02x msg=%02x/%02x "
         "BT event=%02x ACL=%02x/%02x\n",
         g_wifi.wifi_data_out, g_wifi.wifi_data_in,
         g_wifi.wifi_msg_out, g_wifi.wifi_msg_in,
         g_wifi.bt_event_in, g_wifi.bt_acl_out, g_wifi.bt_acl_in);
  return OK;
}

/* Exercise the official full-MAC command channel with MM_VERSION_REQ.  This
 * is the smallest non-destructive request that proves both runtime message
 * endpoints, DATA toggles, framing and firmware command dispatch.  The USB
 * wrapper is four bytes, followed by the firmware's dummy word and the
 * eight-byte LMAC request header.
 */

static int a733_wifi_runtime_version(void)
{
  size_t actual;
  uint16_t param_len;
  unsigned int attempt;
  int ret;

  memset(g_wifi_aic_tx, 0, 16);
  a733_putle16(g_wifi_aic_tx, 12);
  g_wifi_aic_tx[2] = A733_AIC_USB_TYPE_CMD;
  a733_putle16(g_wifi_aic_tx + 8, A733_AIC_MM_VERSION_REQ);
  a733_putle16(g_wifi_aic_tx + 10, 0);
  a733_putle16(g_wifi_aic_tx + 12, A733_AIC_DRIVER_TASK);
  a733_putle16(g_wifi_aic_tx + 14, 0);

  ret = a733_ehci_bulk(g_wifi.address, g_wifi.wifi_msg_out,
                       g_wifi.runtime_maxpacket, false, g_wifi_aic_tx,
                       16, &actual, &g_wifi.wifi_msg_out_toggle);
  if (ret < 0 || actual != 16)
    {
      return ret < 0 ? ret : -EIO;
    }

  for (attempt = 0; attempt < 4; attempt++)
    {
      ret = a733_ehci_bulk(g_wifi.address, g_wifi.wifi_msg_in,
                           g_wifi.runtime_maxpacket, true, g_wifi_aic_rx,
                           sizeof(g_wifi_aic_rx), &actual,
                           &g_wifi.wifi_msg_in_toggle);
      if (ret < 0)
        {
          return ret;
        }

      if (actual < 16 || (g_wifi_aic_rx[2] & 0x7fu) !=
                         A733_AIC_USB_TYPE_CMD ||
          a733_getle16(g_wifi_aic_rx + 4) != A733_AIC_MM_VERSION_CFM)
        {
          continue;
        }

      param_len = a733_getle16(g_wifi_aic_rx + 10);
      if (param_len < 27 || actual < 16u + param_len)
        {
          return -EPROTO;
        }

      g_wifi.lmac_version = a733_getle32(g_wifi_aic_rx + 16);
      g_wifi.machw_version1 = a733_getle32(g_wifi_aic_rx + 20);
      g_wifi.machw_version2 = a733_getle32(g_wifi_aic_rx + 24);
      g_wifi.phy_version1 = a733_getle32(g_wifi_aic_rx + 28);
      g_wifi.phy_version2 = a733_getle32(g_wifi_aic_rx + 32);
      g_wifi.lmac_features = a733_getle32(g_wifi_aic_rx + 36);
      g_wifi.lmac_max_sta = a733_getle16(g_wifi_aic_rx + 40);
      g_wifi.lmac_max_vif = g_wifi_aic_rx[42];

      syslog(LOG_INFO,
             "A733 WIFI: runtime MM_VERSION passed lmac=%08lx "
             "machw=%08lx/%08lx phy=%08lx/%08lx features=%08lx "
             "sta=%u vif=%u\n",
             (unsigned long)g_wifi.lmac_version,
             (unsigned long)g_wifi.machw_version1,
             (unsigned long)g_wifi.machw_version2,
             (unsigned long)g_wifi.phy_version1,
             (unsigned long)g_wifi.phy_version2,
             (unsigned long)g_wifi.lmac_features,
             g_wifi.lmac_max_sta, g_wifi.lmac_max_vif);
      return OK;
    }

  return -EPROTO;
}

static int a733_wifi_lmac_exchange(uint16_t request_id,
                                   uint16_t confirmation_id,
                                   const void *parameter,
                                   uint16_t parameter_length,
                                   uint8_t *confirmation,
                                   size_t confirmation_size,
                                   uint16_t *confirmation_length)
{
  size_t tx_length = 16u + parameter_length;
  size_t actual;
  uint16_t param_len;
  unsigned int attempt;
  int ret;

  if (tx_length > sizeof(g_wifi_aic_tx))
    {
      return -E2BIG;
    }

  ret = nxmutex_lock(&g_wifi_msg_lock);
  if (ret < 0)
    {
      return ret;
    }

  memset(g_wifi_aic_tx, 0, tx_length);
  a733_putle16(g_wifi_aic_tx, 12u + parameter_length);
  g_wifi_aic_tx[2] = A733_AIC_USB_TYPE_CMD;
  a733_putle16(g_wifi_aic_tx + 8, request_id);
  /* LMAC IDs encode their destination task in bits 15..10.  MM messages
   * used task zero, which hid this distinction until the first ME request.
   */

  a733_putle16(g_wifi_aic_tx + 10, request_id >> 10);
  a733_putle16(g_wifi_aic_tx + 12, A733_AIC_DRIVER_TASK);
  a733_putle16(g_wifi_aic_tx + 14, parameter_length);
  if (parameter_length > 0)
    {
      memcpy(g_wifi_aic_tx + 16, parameter, parameter_length);
    }

  ret = a733_ehci_bulk(g_wifi.address, g_wifi.wifi_msg_out,
                       g_wifi.runtime_maxpacket, false, g_wifi_aic_tx,
                       tx_length, &actual, &g_wifi.wifi_msg_out_toggle);
  if (ret < 0 || actual != tx_length)
    {
      ret = ret < 0 ? ret : -EIO;
      goto out;
    }

  for (attempt = 0; attempt < 8; attempt++)
    {
      ret = a733_ehci_bulk(g_wifi.address, g_wifi.wifi_msg_in,
                           g_wifi.runtime_maxpacket, true, g_wifi_aic_rx,
                           sizeof(g_wifi_aic_rx), &actual,
                           &g_wifi.wifi_msg_in_toggle);
      if (ret < 0)
        {
          goto out;
        }

      g_wifi.wifi_msg_frames++;
      if (actual >= 3 &&
          (g_wifi_aic_rx[2] & 0x7fu) == A733_AIC_USB_TYPE_DATA_CFM)
        {
          g_wifi.wifi_data_confirmations++;
          continue;
        }

      if (actual < 16 || (g_wifi_aic_rx[2] & 0x7fu) !=
                         A733_AIC_USB_TYPE_CMD ||
          a733_getle16(g_wifi_aic_rx + 4) != confirmation_id)
        {
          continue;
        }

      param_len = a733_getle16(g_wifi_aic_rx + 10);
      if (actual < 16u + param_len || param_len > confirmation_size)
        {
          ret = -EPROTO;
          goto out;
        }

      if (param_len > 0 && confirmation != NULL)
        {
          memcpy(confirmation, g_wifi_aic_rx + 16, param_len);
        }

      *confirmation_length = param_len;
      ret = OK;
      goto out;
    }

  ret = -EPROTO;

out:
  nxmutex_unlock(&g_wifi_msg_lock);
  return ret;
}

static int a733_wifi_stack_start(void)
{
  const uint8_t request[4] =
  {
    1,    /* is_stack_start */
    0,    /* efuse_valid */
    1u << 5, /* official D80 non-5G build vendor-info selection */
    0     /* fwtrace_redir */
  };
  uint8_t confirmation[8] = {0};
  uint16_t length;
  int ret;

  ret = a733_wifi_lmac_exchange(A733_AIC_MM_STACK_REQ,
                                 A733_AIC_MM_STACK_CFM,
                                 request, sizeof(request), confirmation,
                                 sizeof(confirmation), &length);
  if (ret < 0)
    {
      return ret;
    }

  if (length < 2)
    {
      return -EPROTO;
    }

  g_wifi.band_5g = confirmation[0] != 0;
  g_wifi.vendor_info = confirmation[1];
  syslog(LOG_INFO,
         "A733 WIFI: LMAC stack start passed 5g=%u vendor=%02x\n",
         g_wifi.band_5g, g_wifi.vendor_info);

  memset(confirmation, 0, sizeof(confirmation));
  ret = a733_wifi_lmac_exchange(A733_AIC_MM_GET_MAC_REQ,
                                 A733_AIC_MM_GET_MAC_CFM,
                                 confirmation, 4, confirmation,
                                 sizeof(confirmation), &length);
  if (ret < 0)
    {
      return ret;
    }

  if (length < sizeof(g_wifi.mac))
    {
      return -EPROTO;
    }

  memcpy(g_wifi.mac, confirmation, sizeof(g_wifi.mac));
  if ((g_wifi.mac[0] & 1u) != 0 ||
      (g_wifi.mac[0] | g_wifi.mac[1] | g_wifi.mac[2] |
       g_wifi.mac[3] | g_wifi.mac[4] | g_wifi.mac[5]) == 0)
    {
      return -EADDRNOTAVAIL;
    }

  syslog(LOG_INFO,
         "A733 WIFI: efuse MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
         g_wifi.mac[0], g_wifi.mac[1], g_wifi.mac[2],
         g_wifi.mac[3], g_wifi.mac[4], g_wifi.mac[5]);
  return OK;
}

/* Apply the unmodified D80-U02 default RF profile used by the vendor
 * full-MAC driver.  The two optional adjustment tables are disabled by
 * default, so the official path sends only the V3 power table in the
 * 95-byte union envelope and the aligned 24-byte calibration request.
 * Keeping this as an explicit
 * checkpoint is important: a working USB/LMAC command path alone does not
 * prove that the radio has usable gain tables or completed calibration.
 */

static int a733_wifi_rf_config(void)
{
  static const uint8_t txpower[95] =
  {
    1,
    20, 20, 20, 20, 20, 20, 20, 20, 18, 18, 16, 16,
    20, 20, 20, 20, 18, 18, 16, 16, 16, 16,
    20, 20, 20, 20, 18, 18, 16, 16, 16, 16, 15, 15,
    0x80, 0x80, 0x80, 0x80, 20, 20, 20, 20, 18, 18, 16, 16,
    20, 20, 20, 20, 18, 18, 16, 16, 16, 15,
    20, 20, 20, 20, 18, 18, 16, 16, 16, 15, 14, 14
    /* The remainder is the zeroed tail of mm_set_txpwr_lvl_req's V4-sized
     * union, exactly as produced by the official zero-allocation path.
     */
  };
  uint8_t calibration[24] = {0};
  uint8_t confirmation[24] = {0};
  uint16_t length;
  int ret;

  ret = a733_wifi_lmac_exchange(A733_AIC_MM_TXPWR_REQ,
                                 A733_AIC_MM_TXPWR_CFM,
                                 txpower, sizeof(txpower), confirmation,
                                 sizeof(confirmation), &length);
  if (ret < 0)
    {
      return ret;
    }

  /* AIC8800D80 enumerates as the D81 (8d81) runtime personality. */

  a733_putle32(calibration + 0, 0x00000f8f);
  a733_putle32(calibration + 4, 0x00000f0f);
  a733_putle32(calibration + 8, 0x0c34c008);
  a733_putle32(calibration + 12, 0);
  a733_putle32(calibration + 16, 0x00264203);

  memset(confirmation, 0, sizeof(confirmation));
  ret = a733_wifi_lmac_exchange(A733_AIC_MM_RF_CALIB_REQ,
                                 A733_AIC_MM_RF_CALIB_CFM,
                                 calibration, sizeof(calibration),
                                 confirmation, sizeof(confirmation),
                                 &length);
  if (ret < 0)
    {
      return ret;
    }

  if (length < 16)
    {
      return -EPROTO;
    }

  g_wifi.rf_rxgain_24g = a733_getle32(confirmation + 0);
  g_wifi.rf_rxgain_5g = a733_getle32(confirmation + 4);
  g_wifi.rf_txgain_24g = a733_getle32(confirmation + 8);
  g_wifi.rf_txgain_5g = a733_getle32(confirmation + 12);

  if ((g_wifi.rf_rxgain_24g | g_wifi.rf_rxgain_5g |
       g_wifi.rf_txgain_24g | g_wifi.rf_txgain_5g) == 0)
    {
      return -ENODATA;
    }

  syslog(LOG_INFO,
         "A733 WIFI: RF calibration passed rx=%08lx/%08lx "
         "tx=%08lx/%08lx\n",
         (unsigned long)g_wifi.rf_rxgain_24g,
         (unsigned long)g_wifi.rf_rxgain_5g,
         (unsigned long)g_wifi.rf_txgain_24g,
         (unsigned long)g_wifi.rf_txgain_5g);
  return OK;
}

/* Complete the official post-calibration management-entity handoff.  The
 * first openvela profile deliberately advertises the fully specified HT
 * subset at 40 MHz while leaving VHT/HE clear.  That gives the firmware a
 * valid, conservative station capability set before channel/regulatory and
 * scan support are attached, without claiming host features that do not yet
 * have a corresponding openvela data path.
 */

static int a733_wifi_me_config(void)
{
  uint8_t request[112] = {0};
  uint8_t confirmation[8] = {0};
  uint16_t length;
  int ret;

  ret = a733_wifi_lmac_exchange(A733_AIC_MM_RESET_REQ,
                                 A733_AIC_MM_RESET_CFM,
                                 NULL, 0, confirmation,
                                 sizeof(confirmation), &length);
  if (ret < 0)
    {
      return ret;
    }

  ret = a733_wifi_runtime_version();
  if (ret < 0)
    {
      return ret;
    }

  /* struct me_config_req, D80 non-TL4 ABI:
   *   mac_htcapability  [0..31]
   *   mac_vhtcapability [32..43]
   *   mac_hecapability  [44..99]
   *   tx_lft/phy/flags  [100..109], followed by alignment padding.
   */

  a733_putle16(request + 0, 0x0963); /* LDPC, HT40, SGI20/40, RX-STBC */
  request[2] = 0x1f;                /* 64K A-MPDU, density 16 */
  request[3] = 0xff;                /* one spatial stream MCS0..7 */
  request[7] = 0x01;                /* MCS32 for HT40 */
  a733_putle16(request + 13, 150);   /* mcs.rx_highest */
  request[15] = 0x01;               /* TX MCS set defined */
  a733_putle16(request + 100, 1000); /* official TX lifetime, ms/TU ABI */
  request[102] = 1;                 /* PHY_CHNL_BW_40 */
  request[103] = 1;                 /* ht_supp */
  request[104] = 0;                 /* vht_supp */
  request[105] = 0;                 /* he_supp */
  request[106] = 0;                 /* he_ul_on */
  /* Keep station power-save disabled until beacon/DTIM wake scheduling and
   * all U-APSD queues are represented in the host data path.  Enabling this
   * bit with a polling USB RX worker produces intermittent latency and packet
   * loss even at strong RSSI, which is especially visible to DNS and SSH.
   */

  request[107] = 0;                 /* ps_on */
  request[108] = 0;                 /* ant_div_on */
  request[109] = 0;                 /* dpsm */

  memset(confirmation, 0, sizeof(confirmation));
  ret = a733_wifi_lmac_exchange(A733_AIC_ME_CONFIG_REQ,
                                 A733_AIC_ME_CONFIG_CFM,
                                 request, sizeof(request), confirmation,
                                 sizeof(confirmation), &length);
  if (ret < 0)
    {
      return ret;
    }

  syslog(LOG_INFO,
         "A733 WIFI: ME configuration passed HT=1 VHT=0 HE=0 BW=40 "
         "PS=0 cfm=%u\n", length);
  return OK;
}

/* Install the same channel ABI used by rwnx_send_me_chan_config_req().
 * The selected regulatory profile is CN: channels 1..13 at 20 dBm, the
 * permitted 5 GHz indoor/DFS and 149..165 ranges, and explicit disabled
 * entries for channels outside that profile.  Keeping disabled channels in
 * the fixed firmware table is important; the official driver does the same
 * after cfg80211 has applied its regulatory rules.
 */

static void a733_wifi_channel(uint8_t *entry, uint16_t frequency,
                              uint8_t band, uint8_t flags, int8_t power)
{
  a733_putle16(entry, frequency);
  entry[2] = band;
  entry[3] = flags;
  entry[4] = (uint8_t)power;
  entry[5] = 0; /* struct mac_chan_def alignment padding */
}

static int a733_wifi_channel_config(void)
{
  static const uint16_t channel_5g[] =
  {
    5180, 5200, 5220, 5240, 5260, 5280, 5300, 5320,
    5500, 5520, 5540, 5560, 5580, 5600, 5620, 5640,
    5660, 5680, 5700, 5720, 5745, 5765, 5785, 5805, 5825
  };
  uint8_t request[254] = {0};
  uint8_t confirmation[8] = {0};
  uint16_t length;
  unsigned int index;
  int ret;

  for (index = 0; index < 13; index++)
    {
      a733_wifi_channel(request + index * 6u,
                        (uint16_t)(2412u + index * 5u), 0, 0, 20);
    }

  /* Channel 14 is outside the CN 2400..2483 MHz rule. */

  a733_wifi_channel(request + 13u * 6u, 2484, 0, 2, 20);
  g_wifi.channel_2g_count = 14;

  if (g_wifi.band_5g)
    {
      for (index = 0; index < sizeof(channel_5g) / sizeof(channel_5g[0]);
           index++)
        {
          uint8_t flags = 0;
          int8_t power = 23;

          if (index >= 4 && index <= 7)
            {
              flags = 1 | 4; /* CHAN_NO_IR | CHAN_RADAR */
              power = 20;
            }
          else if (index >= 8 && index <= 19)
            {
              flags = 2; /* Not present in the CN regulatory domain. */
              power = 20;
            }
          else if (index >= 20)
            {
              power = 33;
            }

          a733_wifi_channel(request + 14u * 6u + index * 6u,
                            channel_5g[index], 1, flags, power);
        }

      g_wifi.channel_5g_count =
        (uint8_t)(sizeof(channel_5g) / sizeof(channel_5g[0]));
    }
  else
    {
      g_wifi.channel_5g_count = 0;
    }

  request[252] = g_wifi.channel_2g_count;
  request[253] = g_wifi.channel_5g_count;
  ret = a733_wifi_lmac_exchange(A733_AIC_ME_CHAN_REQ,
                                 A733_AIC_ME_CHAN_CFM,
                                 request, sizeof(request), confirmation,
                                 sizeof(confirmation), &length);
  if (ret < 0)
    {
      return ret;
    }

  syslog(LOG_INFO,
         "A733 WIFI: CN channel table passed 2g=%u 5g=%u cfm=%u\n",
         g_wifi.channel_2g_count, g_wifi.channel_5g_count, length);
  return OK;
}

/* Create the initial full-MAC station interface in firmware.  The request
 * layout is mm_add_if_req: type, one byte of alignment, MAC, p2p and final
 * alignment.  A host wlan0 is deliberately not advertised until its packet
 * data path and RX worker exist; this checkpoint proves the firmware VIF is
 * real instead of presenting a non-functional network device.
 */

static int a733_wifi_station_vif(void)
{
  uint8_t request[10] = {0};
  uint8_t confirmation[8] = {0};
  uint16_t length;
  int ret;

  request[0] = 0; /* MM_STA */
  memcpy(request + 2, g_wifi.mac, sizeof(g_wifi.mac));
  request[8] = 0; /* p2p */

  ret = a733_wifi_lmac_exchange(A733_AIC_MM_ADD_IF_REQ,
                                 A733_AIC_MM_ADD_IF_CFM,
                                 request, sizeof(request), confirmation,
                                 sizeof(confirmation), &length);
  if (ret < 0)
    {
      return ret;
    }

  if (length < 2)
    {
      return -EPROTO;
    }

  if (confirmation[0] != 0 || confirmation[1] >= g_wifi.lmac_max_vif)
    {
      return -ENOSPC;
    }

  g_wifi.station_vif_index = confirmation[1];
  syslog(LOG_INFO,
         "A733 WIFI: station VIF passed index=%u MAC="
         "%02x:%02x:%02x:%02x:%02x:%02x\n",
         g_wifi.station_vif_index,
         g_wifi.mac[0], g_wifi.mac[1], g_wifi.mac[2],
         g_wifi.mac[3], g_wifi.mac[4], g_wifi.mac[5]);
  return OK;
}

/* Start the firmware MAC/PHY exactly where the vendor netdev open path calls
 * rwnx_send_start().  MM_ADD_IF only allocates a VIF; it does not start the
 * radio.  Without this handshake SCANU_START is acknowledged normally but
 * finishes with no result indications because the receive path is idle.
 *
 * struct mm_start_req is 72 bytes in the D80 ABI: sixteen 32-bit PHY words,
 * a 32-bit U-APSD timeout, a 16-bit LP-clock accuracy and two padding bytes.
 * D80 reports the KARST PHY.  The vendor configuration loader leaves the
 * KARST compensation block zero when no optional rwnx_karst.ini is supplied,
 * so a zeroed PHY block plus the official 300 ms / 20 ppm defaults is the
 * correct standalone request.
 */

static int a733_wifi_mac_start(void)
{
  uint8_t request[72] = {0};
  uint8_t confirmation[8] = {0};
  uint16_t length;
  int ret;

  a733_putle32(request + 64, 300); /* uapsd_timeout */
  a733_putle16(request + 68, 20);  /* lp_clk_accuracy */

  ret = a733_wifi_lmac_exchange(A733_AIC_MM_START_REQ,
                                 A733_AIC_MM_START_CFM,
                                 request, sizeof(request), confirmation,
                                 sizeof(confirmation), &length);
  if (ret < 0)
    {
      return ret;
    }

  syslog(LOG_INFO,
         "A733 WIFI: MAC/PHY start passed UAPSD=300 ms LPCLK=20 ppm "
         "cfm=%u\n", length);
  return OK;
}

static int a733_wifi_lmac_send(uint16_t request_id,
                               const void *parameter,
                               uint16_t parameter_length)
{
  size_t tx_length = 16u + parameter_length;
  size_t actual;
  int ret;

  if (tx_length > sizeof(g_wifi_aic_tx))
    {
      return -E2BIG;
    }

  memset(g_wifi_aic_tx, 0, tx_length);
  a733_putle16(g_wifi_aic_tx, 12u + parameter_length);
  g_wifi_aic_tx[2] = A733_AIC_USB_TYPE_CMD;
  a733_putle16(g_wifi_aic_tx + 8, request_id);
  a733_putle16(g_wifi_aic_tx + 10, request_id >> 10);
  a733_putle16(g_wifi_aic_tx + 12, A733_AIC_DRIVER_TASK);
  a733_putle16(g_wifi_aic_tx + 14, parameter_length);
  if (parameter_length > 0)
    {
      memcpy(g_wifi_aic_tx + 16, parameter, parameter_length);
    }

  ret = a733_ehci_bulk(g_wifi.address, g_wifi.wifi_msg_out,
                       g_wifi.runtime_maxpacket, false, g_wifi_aic_tx,
                       tx_length, &actual, &g_wifi.wifi_msg_out_toggle);
  return ret < 0 ? ret : (actual == tx_length ? OK : -EIO);
}

static void a733_wifi_scan_result(const uint8_t *parameter,
                                  uint16_t parameter_length)
{
  const uint8_t *frame;
  const uint8_t *ies;
  struct a733_wifi_scan_s *result;
  uint16_t frame_length;
  uint16_t offset;
  uint8_t index;

  if (parameter_length < 12)
    {
      return;
    }

  frame_length = a733_getle16(parameter);
  if (frame_length < 36 || parameter_length < 12u + frame_length)
    {
      return;
    }

  frame = parameter + 12;
  for (index = 0; index < g_wifi.scan_count; index++)
    {
      if (memcmp(g_wifi.scan[index].bssid, frame + 16, 6) == 0)
        {
          result = &g_wifi.scan[index];
          goto update;
        }
    }

  if (g_wifi.scan_count >= A733_WIFI_SCAN_MAX)
    {
      return;
    }

  result = &g_wifi.scan[g_wifi.scan_count++];
  memset(result, 0, sizeof(*result));
  memcpy(result->bssid, frame + 16, 6);

update:
  result->frequency = a733_getle16(parameter + 4);
  result->rssi = (int8_t)parameter[9];
  result->privacy = (a733_getle16(frame + 34) & 0x0010u) != 0;
  result->assoc_ie_length = 0;
  ies = frame + 36;
  offset = 36;
  while (offset + 2u <= frame_length)
    {
      uint8_t id = ies[0];
      uint8_t length = ies[1];

      if (offset + 2u + length > frame_length)
        {
          break;
        }

      if (id == 0)
        {
          if (length > 32)
            {
              length = 32;
            }

          memcpy(result->ssid, ies + 2, length);
          result->ssid[length] = '\0';
        }

      /* Preserve the RSN or legacy WPA information element exactly as it
       * appeared in the beacon.  The official cfg80211 path forwards these
       * bytes in SM_CONNECT_REQ; the firmware performs authentication and
       * association while the host retains control of the EAPOL port.
       */

      if ((id == 48 ||
           (id == 221 && length >= 4 && ies[2] == 0x00 &&
            ies[3] == 0x50 && ies[4] == 0xf2 && ies[5] == 0x01)) &&
          result->assoc_ie_length + 2u + length <=
            sizeof(result->assoc_ie))
        {
          memcpy(result->assoc_ie + result->assoc_ie_length,
                 ies, 2u + length);
          result->assoc_ie_length += 2u + length;
        }

      offset += 2u + length;
      ies += 2u + length;
    }
}

/* Run one synchronous wildcard scan from the NSH control node.  Firmware
 * delivers SCANU_RESULT_IND messages before the additional completion CFM,
 * so this cannot use the ordinary request/confirmation helper.
 */

enum a733_wifi_scan_mode_e
{
  A733_WIFI_SCAN_2G = 0,
  A733_WIFI_SCAN_5G_LOW,
  A733_WIFI_SCAN_5G_HIGH,
  A733_WIFI_SCAN_DFS
};

static int a733_wifi_scan_once(enum a733_wifi_scan_mode_e mode)
{
  static const uint16_t channel_5g_low[] =
  {
    5180, 5200, 5220, 5240
  };
  static const uint16_t channel_5g_high[] =
  {
    5745, 5765, 5785, 5805, 5825
  };
  static const uint16_t channel_dfs[] =
  {
    5260, 5280, 5300, 5320, 5500, 5520, 5540, 5560, 5580, 5600,
    5620, 5640, 5660, 5680, 5700, 5720
  };
  const uint16_t *channels = NULL;
  size_t channels_count = 0;
  uint8_t request[376] = {0};
  uint8_t channel_count = 0;
  unsigned int index;
  unsigned int idle = 0;
  size_t actual;
  int ret;

  if (g_wifi.station_vif != OK || g_wifi.mac_start != OK)
    {
      return -ENODEV;
    }

  /* Keep a bounded union across scans.  Firmware reports only BSSs heard in
   * the current dwell cycle, so clearing this list made a strong 5 GHz AP
   * disappear whenever one beacon was missed.  Existing BSSIDs are refreshed
   * by a733_wifi_scan_result().
   */

  g_wifi.scan_firmware_count = 0;
  g_wifi.scan_acknowledged = false;
  g_wifi.scan_result_messages = 0;
  g_wifi.scan_last_message = 0;
  g_wifi.scan_status = 0xff;
  if (mode == A733_WIFI_SCAN_2G)
    {
      for (index = 0; index < 13; index++)
        {
          a733_wifi_channel(request + channel_count++ * 6u,
                            (uint16_t)(2412u + index * 5u), 0, 0, 20);
        }

      /* China permits channel 14 at 2484 MHz. */

      a733_wifi_channel(request + channel_count++ * 6u, 2484, 0, 0, 20);
    }
  else if (g_wifi.band_5g)
    {
      if (mode == A733_WIFI_SCAN_5G_LOW)
        {
          channels = channel_5g_low;
          channels_count = sizeof(channel_5g_low) /
                           sizeof(channel_5g_low[0]);
        }
      else if (mode == A733_WIFI_SCAN_5G_HIGH)
        {
          channels = channel_5g_high;
          channels_count = sizeof(channel_5g_high) /
                           sizeof(channel_5g_high[0]);
        }
      else
        {
          channels = channel_dfs;
          channels_count = sizeof(channel_dfs) / sizeof(channel_dfs[0]);
        }

      for (index = 0; index < channels_count; index++)
        {
          uint16_t frequency = channels[index];
          uint8_t flags = mode == A733_WIFI_SCAN_DFS ? 5 : 0;
          int8_t power = mode == A733_WIFI_SCAN_5G_HIGH ? 30 : 20;

          a733_wifi_channel(request + channel_count++ * 6u,
                            frequency, 1, flags, power);
        }
    }

  if (channel_count == 0)
    {
      return -ENOTSUP;
    }

  memset(request + 352, 0xff, 6); /* wildcard BSSID */
  request[366] = g_wifi.station_vif_index;
  request[367] = channel_count;
  /* The vendor WEXT wildcard path passes n_ssids == 0 to
   * rwnx_send_scanu_req().  D80 firmware accepts a one-entry, zero-length
   * SSID request but can finish it without emitting SCANU_RESULT_IND.  Use
   * the exact vendor ABI here: no SSID entries means scan all networks.
   */

  request[368] = 0;
  request[369] = 0;
  a733_putle32(request + 372, 0);

  ret = nxmutex_lock(&g_wifi_msg_lock);
  if (ret < 0)
    {
      return ret;
    }

  ret = a733_wifi_lmac_send(A733_AIC_SCANU_START_REQ,
                             request, sizeof(request));
  if (ret < 0)
    {
      goto out;
    }

  for (index = 0; index < 96 && idle < 12; index++)
    {
      uint16_t message_id;
      uint16_t parameter_length;

      ret = a733_ehci_bulk(g_wifi.address, g_wifi.wifi_msg_in,
                           g_wifi.runtime_maxpacket, true, g_wifi_aic_rx,
                           sizeof(g_wifi_aic_rx), &actual,
                           &g_wifi.wifi_msg_in_toggle);
      if (ret == -ETIMEDOUT)
        {
          idle++;
          continue;
        }

      if (ret < 0)
        {
          goto out;
        }

      idle = 0;
      g_wifi.wifi_msg_frames++;
      if (actual >= 3 &&
          (g_wifi_aic_rx[2] & 0x7fu) == A733_AIC_USB_TYPE_DATA_CFM)
        {
          g_wifi.wifi_data_confirmations++;
          continue;
        }

      if (actual < 16 || (g_wifi_aic_rx[2] & 0x7fu) !=
                         A733_AIC_USB_TYPE_CMD)
        {
          continue;
        }

      message_id = a733_getle16(g_wifi_aic_rx + 4);
      parameter_length = a733_getle16(g_wifi_aic_rx + 10);
      g_wifi.scan_last_message = message_id;
      if (actual < 16u + parameter_length)
        {
          continue;
        }

      if (message_id == A733_AIC_SCANU_RESULT_IND)
        {
          g_wifi.scan_result_messages++;
          a733_wifi_scan_result(g_wifi_aic_rx + 16, parameter_length);
        }
      else if (message_id == A733_AIC_SCANU_DONE_CFM)
        {
          /* The vendor driver waits synchronously for this additional CFM
           * before returning from rwnx_send_scanu_req().  It is only an ACK;
           * scan results and the ordinary START_CFM arrive afterwards.
           */

          g_wifi.scan_acknowledged = true;
        }
      else if (message_id == A733_AIC_SCANU_START_CFM)
        {
          uint8_t status = parameter_length >= 2 ? g_wifi_aic_rx[17] : 0;

          g_wifi.scan_status = status;
          g_wifi.scan_firmware_count =
            parameter_length >= 3 ? g_wifi_aic_rx[18] : 0;

          if (status != 0)
            {
              ret = -EIO;
              goto out;
            }

          syslog(LOG_INFO,
                 "A733 WIFI: active scan passed channels=%u indications=%u "
                 "results=%u firmware=%u ack=%u\n",
                 channel_count, g_wifi.scan_result_messages,
                 g_wifi.scan_count, g_wifi.scan_firmware_count,
                 g_wifi.scan_acknowledged);
          ret = OK;
          goto out;
        }
    }

  ret = -ETIMEDOUT;

out:
  nxmutex_unlock(&g_wifi_msg_lock);
  return ret;
}

/* A single large dual-band request is unreliable on D80-U02: firmware can
 * acknowledge it while returning no result indications.  Scan small channel
 * groups and retry empty dwell cycles.  A user-visible scan is still one
 * operation, but the single radio visits 2.4 GHz and 5 GHz sequentially.
 */

static int a733_wifi_scan_retry(enum a733_wifi_scan_mode_e mode)
{
  unsigned int attempt;
  int ret = -ENODATA;

  for (attempt = 0; attempt < 3; attempt++)
    {
      ret = a733_wifi_scan_once(mode);
      if (ret == OK && g_wifi.scan_result_messages > 0)
        {
          return OK;
        }

      if (ret < 0 && ret != -ETIMEDOUT)
        {
          return ret;
        }

      nxsig_usleep(100000);
    }

  return ret < 0 ? ret : -ENODATA;
}

static int a733_wifi_scan_group(bool scan_2g, bool scan_5g, bool scan_dfs)
{
  int success = 0;
  int last_error = -ENODATA;
  int ret;

  memset(g_wifi.scan, 0, sizeof(g_wifi.scan));
  g_wifi.scan_count = 0;

  if (scan_2g)
    {
      ret = a733_wifi_scan_retry(A733_WIFI_SCAN_2G);
      success += ret == OK;
      if (ret < 0)
        {
          last_error = ret;
        }
    }

  if (scan_5g)
    {
      ret = a733_wifi_scan_retry(A733_WIFI_SCAN_5G_LOW);
      success += ret == OK;
      if (ret < 0)
        {
          last_error = ret;
        }

      ret = a733_wifi_scan_retry(A733_WIFI_SCAN_5G_HIGH);
      success += ret == OK;
      if (ret < 0)
        {
          last_error = ret;
        }
    }

  if (scan_dfs)
    {
      ret = a733_wifi_scan_retry(A733_WIFI_SCAN_DFS);
      success += ret == OK;
      if (ret < 0)
        {
          last_error = ret;
        }
    }

  return success > 0 ? OK : last_error;
}

/* Perform the firmware authentication/association half of a station
 * connection.  This deliberately stops at the controlled-port boundary:
 * protected networks still require the host netdev, EAPOL delivery and key
 * installation before IP traffic may flow.  Keeping this as a separate
 * checkpoint makes failures attributable to SM or to the later WPA path.
 */

static int a733_wifi_disconnect(void);

/* Build the station RSN IE instead of reflecting the AP beacon verbatim.
 * A WPA2/WPA3 transition BSS advertises both PSK and SAE.  Echoing that IE
 * tells firmware/AP that the station supports SAE even though this host
 * supplicant implements WPA2-PSK only; the AP can then send a descriptor-
 * version-0 handshake which cannot be processed by the SHA1/CCMP path.
 * Select one CCMP pairwise suite and one PSK AKM explicitly.  The group
 * cipher remains the AP's advertised value, and PMF bits are cleared because
 * transition-mode WPA2-PSK cannot require management-frame protection.
 */

static int a733_wifi_wpa_select_rsn(const uint8_t *ies, size_t ies_length,
                                    uint8_t *selected,
                                    uint16_t *selected_length)
{
  static const uint8_t ccmp[4] = {0x00, 0x0f, 0xac, 0x04};
  static const uint8_t tkip[4] = {0x00, 0x0f, 0xac, 0x02};
  static const uint8_t psk[4] = {0x00, 0x0f, 0xac, 0x02};
  size_t offset = 0;

  while (offset + 2u <= ies_length)
    {
      const uint8_t *ie = ies + offset;
      size_t ie_length = ie[1];
      const uint8_t *cursor;
      const uint8_t *end;
      const uint8_t *group;
      uint16_t count;
      bool has_ccmp = false;
      bool has_psk = false;
      unsigned int index;

      if (offset + 2u + ie_length > ies_length)
        {
          return -EPROTO;
        }

      offset += 2u + ie_length;
      if (ie[0] != 48)
        {
          continue;
        }

      cursor = ie + 2;
      end = cursor + ie_length;
      if (ie_length < 18 || a733_getle16(cursor) != 1)
        {
          return -EPROTO;
        }

      cursor += 2;
      group = cursor;
      cursor += 4;
      if (cursor + 2 > end)
        {
          return -EPROTO;
        }

      count = a733_getle16(cursor);
      cursor += 2;
      if (count == 0 || cursor + (size_t)count * 4u > end)
        {
          return -EPROTO;
        }

      for (index = 0; index < count; index++, cursor += 4)
        {
          if (memcmp(cursor, ccmp, sizeof(ccmp)) == 0)
            {
              has_ccmp = true;
            }
        }

      if (cursor + 2 > end)
        {
          return -EPROTO;
        }

      count = a733_getle16(cursor);
      cursor += 2;
      if (count == 0 || cursor + (size_t)count * 4u > end)
        {
          return -EPROTO;
        }

      for (index = 0; index < count; index++, cursor += 4)
        {
          if (memcmp(cursor, psk, sizeof(psk)) == 0)
            {
              has_psk = true;
            }
        }

      if (!has_ccmp || !has_psk)
        {
          return -ENOTSUP;
        }

      if (memcmp(group, ccmp, sizeof(ccmp)) == 0)
        {
          g_wifi.wpa_group_cipher = 2; /* MAC_CIPHER_CCMP */
          g_wifi.wpa_group_key_length = 16;
        }
      else if (memcmp(group, tkip, sizeof(tkip)) == 0)
        {
          /* A mixed WPA/WPA2 BSS can use TKIP only for broadcast traffic
           * while choosing CCMP for the pairwise key.  The handshake still
           * uses descriptor version 2; install its full 32-byte GTK using
           * the firmware's MAC_CIPHER_TKIP mode.
           */

          g_wifi.wpa_group_cipher = 1; /* MAC_CIPHER_TKIP */
          g_wifi.wpa_group_key_length = 32;
        }
      else
        {
          return -ENOTSUP;
        }

      selected[0] = 48;
      selected[1] = 20;
      a733_putle16(selected + 2, 1);
      memcpy(selected + 4, group, 4);
      a733_putle16(selected + 8, 1);
      memcpy(selected + 10, ccmp, 4);
      a733_putle16(selected + 14, 1);
      memcpy(selected + 16, psk, 4);
      a733_putle16(selected + 20, 0); /* no PMF capability/requirement */
      *selected_length = 22;
      return OK;
    }

  return -ENOTSUP;
}

static int a733_wifi_associate(unsigned int result_index)
{
  struct a733_wifi_scan_s *result;
  uint8_t request[A733_AIC_CONNECT_REQ_SIZE] = {0};
  bool got_cfm = false;
  size_t actual;
  unsigned int attempt;
  unsigned int idle = 0;
  int ret;

  if (result_index >= g_wifi.scan_count)
    {
      return -ENOENT;
    }

  if (g_wifi.station_vif != OK || g_wifi.mac_start != OK)
    {
      return -ENODEV;
    }

  result = &g_wifi.scan[result_index];
  if (result->ssid[0] == '\0')
    {
      return -EINVAL;
    }

  /* Firmware cannot accept a second SM_CONNECT_REQ while the station VIF is
   * already associated.  Make the control idempotent for the current BSS and
   * perform the complete vendor disconnect handshake before changing BSS.
   */

  if (g_wifi.associated)
    {
      if (memcmp(g_wifi.associated_bssid, result->bssid,
                 sizeof(result->bssid)) == 0)
        {
          if (!g_wifi.wpa_configured)
            {
              syslog(LOG_INFO,
                     "A733 WIFI: association already active ssid='%s'; "
                     "duplicate request ignored\n", g_wifi.associated_ssid);
              return OK;
            }

          /* A WPA control request must negotiate with the selected station
           * RSN IE even if an earlier diagnostic association reached the
           * same BSSID using the beacon's unfiltered transition IE.
           */
        }

      ret = a733_wifi_disconnect();
      g_wifi.disconnect_checkpoint = ret;
      if (ret < 0)
        {
          return ret;
        }
    }

  request[0] = (uint8_t)strnlen(result->ssid, 32);
  memcpy(request + 1, result->ssid, request[0]);
  memcpy(request + 34, result->bssid, sizeof(result->bssid));
  a733_putle16(request + 40, result->frequency);
  request[42] = result->frequency >= 5000 ? 1 : 0;
  request[43] = 0;

  /* Keep chan.tx_power zero.  The vendor rwnx connect path allocates the
   * request with zalloc and only fills band/frequency/flags.  Supplying the
   * regulatory maximum here is not equivalent: D80 firmware interprets this
   * field as a per-connection override, which can destabilize a 5 GHz join.
   */

  request[44] = 0;

  if (result->assoc_ie_length > 0)
    {
      const uint8_t *assoc_ie = result->assoc_ie;
      uint16_t assoc_ie_length = result->assoc_ie_length;

      if (g_wifi.wpa_configured && g_wifi.wpa_assoc_ie_length > 0)
        {
          assoc_ie = g_wifi.wpa_assoc_ie;
          assoc_ie_length = g_wifi.wpa_assoc_ie_length;
        }

      /* CONTROL_PORT_HOST | WPA_WPA2_IN_USE. */

      a733_putle32(request + 48, (1u << 0) | (1u << 3));
      /* ctrl_port_ethertype is declared in network byte order. */

      request[52] = 0x88;
      request[53] = 0x8e;
      a733_putle16(request + 54, assoc_ie_length);
      memcpy(request + 64, assoc_ie, assoc_ie_length);
    }

  a733_putle16(request + 56, 0); /* vendor default listen interval */
  request[58] = 0;               /* wait for BC/MC after DTIM */
  request[59] = 0;               /* WLAN_AUTH_OPEN */
  request[60] = 0;               /* no UAPSD queue while PS is disabled */
  request[61] = g_wifi.station_vif_index;

  g_wifi.association_cfm_status = 0xff;
  g_wifi.association_status_code = 0xffff;
  g_wifi.association_vif = 0xff;
  g_wifi.association_ap = 0xff;
  g_wifi.association_channel = 0xff;
  g_wifi.associated = false;
  memset(g_wifi.associated_bssid, 0, sizeof(g_wifi.associated_bssid));
  memset(g_wifi.associated_ssid, 0, sizeof(g_wifi.associated_ssid));

  ret = nxmutex_lock(&g_wifi_msg_lock);
  if (ret < 0)
    {
      return ret;
    }

  ret = a733_wifi_lmac_send(A733_AIC_SM_CONNECT_REQ,
                            request, sizeof(request));
  if (ret < 0)
    {
      goto out;
    }

  for (attempt = 0; attempt < 128 && idle < 24; attempt++)
    {
      uint16_t message_id;
      uint16_t parameter_length;

      ret = a733_ehci_bulk(g_wifi.address, g_wifi.wifi_msg_in,
                           g_wifi.runtime_maxpacket, true, g_wifi_aic_rx,
                           sizeof(g_wifi_aic_rx), &actual,
                           &g_wifi.wifi_msg_in_toggle);
      if (ret == -ETIMEDOUT)
        {
          idle++;
          continue;
        }

      if (ret < 0)
        {
          goto out;
        }

      idle = 0;
      g_wifi.wifi_msg_frames++;
      if (actual >= 3 &&
          (g_wifi_aic_rx[2] & 0x7fu) == A733_AIC_USB_TYPE_DATA_CFM)
        {
          g_wifi.wifi_data_confirmations++;
          continue;
        }

      if (actual < 16 || (g_wifi_aic_rx[2] & 0x7fu) !=
                         A733_AIC_USB_TYPE_CMD)
        {
          continue;
        }

      message_id = a733_getle16(g_wifi_aic_rx + 4);
      parameter_length = a733_getle16(g_wifi_aic_rx + 10);
      if (actual < 16u + parameter_length)
        {
          continue;
        }

      if (message_id == A733_AIC_SM_CONNECT_CFM)
        {
          if (parameter_length < 1)
            {
              ret = -EPROTO;
              goto out;
            }

          g_wifi.association_cfm_status = g_wifi_aic_rx[16];
          got_cfm = true;
          if (g_wifi.association_cfm_status != 0)
            {
              ret = -EBUSY;
              goto out;
            }
        }
      else if (message_id == A733_AIC_SM_CONNECT_IND)
        {
          if (parameter_length < 18 || !got_cfm)
            {
              ret = -EPROTO;
              goto out;
            }

          g_wifi.association_status_code =
            a733_getle16(g_wifi_aic_rx + 16);
          memcpy(g_wifi.associated_bssid, g_wifi_aic_rx + 18, 6);
          g_wifi.association_vif = g_wifi_aic_rx[25];
          g_wifi.association_ap = g_wifi_aic_rx[26];
          g_wifi.association_channel = g_wifi_aic_rx[27];
          if (g_wifi.association_status_code != 0)
            {
              ret = -ECONNREFUSED;
              goto out;
            }

          g_wifi.associated = true;
#ifdef CONFIG_NET
          a733_wifi_net_carrier(true);
#endif
          memcpy(g_wifi.associated_ssid, result->ssid,
                 sizeof(g_wifi.associated_ssid));
          syslog(LOG_INFO,
                 "A733 WIFI: association control plane passed ssid='%s' "
                 "vif=%u ap=%u channel=%u protected=%u\n",
                 g_wifi.associated_ssid, g_wifi.association_vif,
                 g_wifi.association_ap, g_wifi.association_channel,
                 result->assoc_ie_length > 0);
          ret = OK;
          goto out;
        }
      else if (message_id == A733_AIC_SM_DISCONNECT_IND)
        {
          g_wifi.associated = false;
          g_wifi.wpa_port_open = false;
          g_wifi.wpa_pairwise_installed = false;
          g_wifi.wpa_group_installed = false;
          if (g_wifi.wpa_configured)
            {
              g_wifi.wpa_checkpoint = -ENETDOWN;
              g_wifi.wpa_state = A733_WPA_WAIT_M1;
            }
#ifdef CONFIG_NET
          a733_wifi_net_carrier(false);
#endif
        }
    }

  ret = -ETIMEDOUT;

out:
  nxmutex_unlock(&g_wifi_msg_lock);
  return ret;
}

static int a733_wifi_disconnect(void)
{
  uint8_t request[4] = {0};
  bool was_associated = g_wifi.associated;
  size_t actual;
  unsigned int attempt;
  unsigned int idle = 0;
  int ret;

  a733_putle16(request, 3); /* station is leaving */
  request[2] = g_wifi.station_vif_index;

  g_wifi.disconnect_confirmed = false;
  g_wifi.disconnect_indicated = false;
  g_wifi.disconnect_reason = 0xffff;

  ret = nxmutex_lock(&g_wifi_msg_lock);
  if (ret < 0)
    {
      return ret;
    }

  ret = a733_wifi_lmac_send(A733_AIC_SM_DISCONNECT_REQ,
                            request, sizeof(request));
  if (ret < 0)
    {
      goto out;
    }

  /* A successful CFM only means that firmware accepted the request.  The
   * official driver keeps the VIF in DISCONNECTING until SM_DISCONNECT_IND
   * arrives.  Consume both messages (in either order) so an immediate join on
   * another band cannot race the old link teardown and poison USB1.
   */

  for (attempt = 0; attempt < 128 && idle < 24; attempt++)
    {
      uint16_t message_id;
      uint16_t parameter_length;

      ret = a733_ehci_bulk(g_wifi.address, g_wifi.wifi_msg_in,
                           g_wifi.runtime_maxpacket, true, g_wifi_aic_rx,
                           sizeof(g_wifi_aic_rx), &actual,
                           &g_wifi.wifi_msg_in_toggle);
      if (ret == -ETIMEDOUT)
        {
          idle++;
          continue;
        }

      if (ret < 0)
        {
          goto out;
        }

      idle = 0;
      g_wifi.wifi_msg_frames++;
      if (actual >= 3 &&
          (g_wifi_aic_rx[2] & 0x7fu) == A733_AIC_USB_TYPE_DATA_CFM)
        {
          g_wifi.wifi_data_confirmations++;
          continue;
        }

      if (actual < 16 || (g_wifi_aic_rx[2] & 0x7fu) !=
                         A733_AIC_USB_TYPE_CMD)
        {
          continue;
        }

      message_id = a733_getle16(g_wifi_aic_rx + 4);
      parameter_length = a733_getle16(g_wifi_aic_rx + 10);
      if (actual < 16u + parameter_length)
        {
          continue;
        }

      if (message_id == A733_AIC_SM_DISCONNECT_CFM)
        {
          g_wifi.disconnect_confirmed = true;
        }
      else if (message_id == A733_AIC_SM_DISCONNECT_IND)
        {
          if (parameter_length < 3)
            {
              ret = -EPROTO;
              goto out;
            }

          g_wifi.disconnect_reason = a733_getle16(g_wifi_aic_rx + 16);
          g_wifi.disconnect_indicated = true;
          g_wifi.associated = false;
#ifdef CONFIG_NET
          a733_wifi_net_carrier(false);
#endif
        }

      if (g_wifi.disconnect_confirmed &&
          (g_wifi.disconnect_indicated || !was_associated))
        {
          memset(g_wifi.associated_bssid, 0,
                 sizeof(g_wifi.associated_bssid));
          memset(g_wifi.associated_ssid, 0,
                 sizeof(g_wifi.associated_ssid));

          /* Match the vendor driver's state-settle window before allowing a
           * new association request to reuse the command endpoint.
           */

          up_mdelay(100);
          ret = OK;
          goto out;
        }
    }

  ret = -ETIMEDOUT;

out:
  nxmutex_unlock(&g_wifi_msg_lock);
  return ret;
}

/* Receive and decode one firmware WLAN data indication.  The D80 USB data
 * endpoint does not carry Ethernet frames directly: its first 60 bytes are
 * the official hw_rxhdr (the first word also contains the USB packet length
 * and type), followed by an 802.11 data frame.  For this checkpoint we keep
 * the frame intact and locate the RFC1042 LLC/SNAP header.  That is enough to
 * prove the independent 01/81 data pipe and, in particular, observe EAPOL
 * message 1 before adding the host WPA state machine.
 */

static int a733_wifi_data_receive_timeout(unsigned int timeout_ms)
{
  const size_t hwrx_length = 60;
  size_t actual;
  size_t offset;
  uint8_t ds;
  uint16_t frame_control;
  uint16_t packet_length;
  int ret;

  if (!g_wifi.associated || g_wifi.wifi_data_in == 0 ||
      g_wifi.runtime_maxpacket == 0)
    {
      return -ENOTCONN;
    }

  ret = a733_ehci_bulk_timeout(g_wifi.address, g_wifi.wifi_data_in,
                               g_wifi.runtime_maxpacket, true,
                               g_wifi_data_rx, sizeof(g_wifi_data_rx),
                               &actual, &g_wifi.wifi_data_in_toggle,
                               timeout_ms);
  g_wifi.data_actual = actual;
  if (ret < 0)
    {
      return ret;
    }

  if (actual < hwrx_length + 24)
    {
      return -EPROTO;
    }

  packet_length = a733_getle16(g_wifi_data_rx);
  if ((size_t)packet_length + hwrx_length > actual)
    {
      return -EPROTO;
    }

  frame_control = a733_getle16(g_wifi_data_rx + hwrx_length);
  g_wifi.data_packet_length = packet_length;
  g_wifi.data_frame_control = frame_control;
  g_wifi.data_ethertype = 0;
  g_wifi.data_llc_offset = 0;
  g_wifi.data_payload_length = 0;
  memset(g_wifi.data_destination, 0, sizeof(g_wifi.data_destination));
  memset(g_wifi.data_source, 0, sizeof(g_wifi.data_source));

  /* The official driver accepts data subtypes and reconstructs Ethernet
   * addresses according to the ToDS/FromDS bits.
   */

  if ((frame_control & 0x000cu) != 0x0008u)
    {
      return -ENOMSG;
    }

  ds = (frame_control >> 8) & 3u;
  if (ds == 1) /* To DS */
    {
      memcpy(g_wifi.data_destination, g_wifi_data_rx + hwrx_length + 16, 6);
      memcpy(g_wifi.data_source, g_wifi_data_rx + hwrx_length + 10, 6);
    }
  else if (ds == 2) /* From DS */
    {
      memcpy(g_wifi.data_destination, g_wifi_data_rx + hwrx_length + 4, 6);
      memcpy(g_wifi.data_source, g_wifi_data_rx + hwrx_length + 16, 6);
    }
  else
    {
      memcpy(g_wifi.data_destination, g_wifi_data_rx + hwrx_length + 4, 6);
      memcpy(g_wifi.data_source, g_wifi_data_rx + hwrx_length + 10, 6);
    }

  /* Encryption headers vary with the negotiated cipher.  EAPOL message 1 is
   * unencrypted, but scanning a small bounded prefix for RFC1042 also makes
   * the diagnostic robust to QoS/HT control and future cipher headers.
   */

  for (offset = hwrx_length + 24;
       offset + 8 <= hwrx_length + packet_length &&
       offset < hwrx_length + 96;
       offset++)
    {
      if (g_wifi_data_rx[offset] == 0xaa &&
          g_wifi_data_rx[offset + 1] == 0xaa &&
          g_wifi_data_rx[offset + 2] == 0x03 &&
          g_wifi_data_rx[offset + 3] == 0x00 &&
          g_wifi_data_rx[offset + 4] == 0x00 &&
          g_wifi_data_rx[offset + 5] == 0x00)
        {
          g_wifi.data_ethertype =
            ((uint16_t)g_wifi_data_rx[offset + 6] << 8) |
            g_wifi_data_rx[offset + 7];
          g_wifi.data_llc_offset = offset;
          g_wifi.data_payload_length =
            (uint16_t)(hwrx_length + packet_length - (offset + 8));
          break;
        }
    }

  g_wifi.data_frames++;
  if (g_wifi.data_ethertype == 0x888e)
    {
      g_wifi.data_eapol_frames++;
    }

  return OK;
}

static int a733_wifi_data_receive(void)
{
  return a733_wifi_data_receive_timeout(A733_AIC_TIMEOUT_MS);
}

static int a733_wifi_data_checkpoint(void)
{
  unsigned int attempt;
  int last = -ETIMEDOUT;

  for (attempt = 0; attempt < A733_WIFI_DATA_TRIES; attempt++)
    {
      last = a733_wifi_data_receive();
      if (last == OK && g_wifi.data_ethertype == 0x888e)
        {
          syslog(LOG_INFO,
                 "A733 WIFI: WLAN data endpoint passed, EAPOL frame "
                 "len=%u fc=%04x %02x:%02x:%02x:%02x:%02x:%02x -> "
                 "%02x:%02x:%02x:%02x:%02x:%02x\n",
                 g_wifi.data_packet_length, g_wifi.data_frame_control,
                 g_wifi.data_source[0], g_wifi.data_source[1],
                 g_wifi.data_source[2], g_wifi.data_source[3],
                 g_wifi.data_source[4], g_wifi.data_source[5],
                 g_wifi.data_destination[0], g_wifi.data_destination[1],
                 g_wifi.data_destination[2], g_wifi.data_destination[3],
                 g_wifi.data_destination[4], g_wifi.data_destination[5]);
          return OK;
        }

      if (last != OK && last != -ETIMEDOUT && last != -ENOMSG)
        {
          return last;
        }
    }

  return last == OK ? -ENOMSG : last;
}

/* Send an EAPOL-Start frame using the exact non-aggregated USB layout selected
 * by the official FCU760K build (CONFIG_USB_TX_AGGR=n): four-byte USB header,
 * 28-byte txdesc_api/hostdesc and the Ethernet payload.  The Ethernet header
 * itself is represented by the destination/source/EtherType fields in the
 * host descriptor and is not copied into the payload.
 */

static int a733_wifi_data_send_eapol_start(void)
{
  const size_t usb_header_length = 4;
  const size_t descriptor_length = 28;
  const size_t payload_length = 4;
  const size_t transfer_length = usb_header_length + descriptor_length +
                                 payload_length;
  uint8_t *descriptor = g_wifi_data_tx + usb_header_length;
  uint8_t *payload = descriptor + descriptor_length;
  size_t actual;
  int ret;

  if (!g_wifi.associated || g_wifi.wifi_data_out == 0 ||
      g_wifi.runtime_maxpacket == 0 || g_wifi.association_ap == 0xff)
    {
      return -ENOTCONN;
    }

  ret = nxmutex_lock(&g_wifi_data_tx_lock);
  if (ret < 0)
    {
      return ret;
    }

  memset(g_wifi_data_tx, 0, transfer_length);

  /* usb_header: total transfer length, data type. */

  a733_putle16(g_wifi_data_tx, transfer_length);
  g_wifi_data_tx[2] = 0x01;

  /* struct hostdesc, full-MAC layout (28 bytes). */

  a733_putle16(descriptor + 0, payload_length); /* packet_len */
  a733_putle16(descriptor + 2, 0);              /* flags_ext */
  a733_putle32(descriptor + 4, 0);              /* status_desc_addr */
  memcpy(descriptor + 8, g_wifi.associated_bssid, 6);
  memcpy(descriptor + 14, g_wifi.mac, 6);
  descriptor[20] = 0x88; /* h_proto memory representation for 0x888e */
  descriptor[21] = 0x8e;
  descriptor[22] = 1;    /* AC_BE */
  descriptor[23] = 0;    /* TID_0 */
  descriptor[24] = g_wifi.station_vif_index;
  descriptor[25] = g_wifi.association_ap;
  a733_putle16(descriptor + 26, 0); /* TX flags */

  /* IEEE 802.1X EAPOL-Start: protocol version 2, packet type 1, length 0. */

  payload[0] = 2;
  payload[1] = 1;
  payload[2] = 0;
  payload[3] = 0;

  g_wifi_tx_pending = true;
  ret = a733_ehci_bulk(g_wifi.address, g_wifi.wifi_data_out,
                       g_wifi.runtime_maxpacket, false, g_wifi_data_tx,
                       transfer_length, &actual,
                       &g_wifi.wifi_data_out_toggle);
  g_wifi_tx_pending = false;
  g_wifi.data_tx_actual = actual;
  if (ret < 0 || actual != transfer_length)
    {
      nxmutex_unlock(&g_wifi_data_tx_lock);
      return ret < 0 ? ret : -EPROTO;
    }

  g_wifi.data_tx_frames++;
  nxmutex_unlock(&g_wifi_data_tx_lock);
  return OK;
}

#ifdef CONFIG_NET
/* Convert an Ethernet frame generated by the openvela network stack to the
 * non-aggregated D80 USB TX format.  The 14-byte Ethernet header is carried
 * in hostdesc while only the L3 payload follows the descriptor.
 */

static int a733_wifi_data_send_ethernet(const uint8_t *frame, size_t length)
{
  const size_t usb_header_length = 4;
  const size_t descriptor_length = 28;
  size_t payload_length;
  size_t transfer_length;
  uint8_t *descriptor = g_wifi_data_tx + usb_header_length;
  uint8_t *payload = descriptor + descriptor_length;
  size_t actual;
  int ret;

  if (!g_wifi.associated || length < 14 ||
      length > CONFIG_NET_ETH_PKTSIZE || g_wifi.association_ap == 0xff)
    {
      return -ENOTCONN;
    }

  payload_length = length - 14;
  transfer_length = usb_header_length + descriptor_length + payload_length;
  if (transfer_length > sizeof(g_wifi_data_tx))
    {
      return -E2BIG;
    }

  ret = nxmutex_lock(&g_wifi_data_tx_lock);
  if (ret < 0)
    {
      return ret;
    }

  memset(g_wifi_data_tx, 0, transfer_length);
  a733_putle16(g_wifi_data_tx, transfer_length);
  g_wifi_data_tx[2] = 0x01;

  a733_putle16(descriptor + 0, payload_length);
  memcpy(descriptor + 8, frame, 6);
  memcpy(descriptor + 14, frame + 6, 6);
  descriptor[20] = frame[12];
  descriptor[21] = frame[13];
  descriptor[22] = 1; /* AC_BE */
  descriptor[23] = 0; /* TID_0 */
  descriptor[24] = g_wifi.station_vif_index;
  descriptor[25] = g_wifi.association_ap;
  memcpy(payload, frame + 14, payload_length);

  g_wifi_tx_pending = true;
  ret = a733_ehci_bulk(g_wifi.address, g_wifi.wifi_data_out,
                       g_wifi.runtime_maxpacket, false, g_wifi_data_tx,
                       transfer_length, &actual,
                       &g_wifi.wifi_data_out_toggle);
  g_wifi_tx_pending = false;
  if (ret == OK && actual != transfer_length)
    {
      ret = -EPROTO;
    }

  if (ret == OK)
    {
      g_wifi.data_tx_frames++;
      g_wifi.data_tx_actual = actual;
      if (frame[12] == 0x08 && frame[13] == 0x06)
        {
          g_wifi.data_tx_arp_frames++;
        }
    }

  nxmutex_unlock(&g_wifi_data_tx_lock);
  return ret;
}

/* WPA2-PSK/CCMP host state machine.  FCU760K is a split-MAC device: firmware
 * performs authentication/association but Linux normally leaves EAPOL and
 * key management to wpa_supplicant.  openvela does not ship a supplicant, so
 * the small implementation below intentionally supports only RSN PSK + CCMP.
 * It follows IEEE 802.11i message 1..4 and uses the exact MM/ME messages used
 * by the official rwnx driver to install keys and open the controlled port.
 */

#define A733_WPA_KEY_TYPE         0x0008u
#define A733_WPA_KEY_INSTALL      0x0040u
#define A733_WPA_KEY_ACK          0x0080u
#define A733_WPA_KEY_MIC          0x0100u
#define A733_WPA_KEY_SECURE       0x0200u
#define A733_WPA_KEY_ENCRYPTED    0x1000u

static uint16_t a733_getbe16(const uint8_t *buffer)
{
  return ((uint16_t)buffer[0] << 8) | buffer[1];
}

static void a733_putbe16(uint8_t *buffer, uint16_t value)
{
  buffer[0] = value >> 8;
  buffer[1] = value & 0xffu;
}

static void a733_wifi_hmac_sha1(const uint8_t *key, size_t key_length,
                                const uint8_t *data, size_t data_length,
                                uint8_t digest[20])
{
  SHA1_CTX context;
  uint8_t key_block[SHA1_BLOCK_LENGTH];
  uint8_t pad[SHA1_BLOCK_LENGTH];
  size_t index;

  memset(key_block, 0, sizeof(key_block));
  if (key_length > sizeof(key_block))
    {
      sha1init(&context);
      sha1update(&context, key, key_length);
      sha1final(key_block, &context);
    }
  else
    {
      memcpy(key_block, key, key_length);
    }

  for (index = 0; index < sizeof(pad); index++)
    {
      pad[index] = key_block[index] ^ 0x36;
    }

  sha1init(&context);
  sha1update(&context, pad, sizeof(pad));
  sha1update(&context, data, data_length);
  sha1final(digest, &context);

  for (index = 0; index < sizeof(pad); index++)
    {
      pad[index] = key_block[index] ^ 0x5c;
    }

  sha1init(&context);
  sha1update(&context, pad, sizeof(pad));
  sha1update(&context, digest, SHA1_DIGEST_LENGTH);
  sha1final(digest, &context);
  explicit_bzero(&context, sizeof(context));
  explicit_bzero(key_block, sizeof(key_block));
  explicit_bzero(pad, sizeof(pad));
}

static void a733_wifi_pbkdf2_block(const uint8_t *passphrase,
                                   size_t passphrase_length,
                                   const uint8_t *ssid, size_t ssid_length,
                                   uint32_t block, uint8_t output[20])
{
  uint8_t salt[36];
  uint8_t digest[20];
  unsigned int iteration;
  unsigned int index;

  memcpy(salt, ssid, ssid_length);
  salt[ssid_length] = block >> 24;
  salt[ssid_length + 1] = block >> 16;
  salt[ssid_length + 2] = block >> 8;
  salt[ssid_length + 3] = block;
  a733_wifi_hmac_sha1(passphrase, passphrase_length, salt,
                      ssid_length + 4, digest);
  memcpy(output, digest, 20);

  for (iteration = 1; iteration < 4096; iteration++)
    {
      a733_wifi_hmac_sha1(passphrase, passphrase_length,
                          digest, sizeof(digest), digest);
      for (index = 0; index < sizeof(digest); index++)
        {
          output[index] ^= digest[index];
        }
    }

  explicit_bzero(salt, sizeof(salt));
  explicit_bzero(digest, sizeof(digest));
}

static int a733_wifi_wpa_derive_pmk(const char *passphrase,
                                    const char *ssid, uint8_t pmk[32])
{
  uint8_t block[20];
  size_t passphrase_length = strlen(passphrase);
  size_t ssid_length = strlen(ssid);

  if (passphrase_length < 8 || passphrase_length > 63 ||
      ssid_length == 0 || ssid_length > 32)
    {
      return -EINVAL;
    }

  a733_wifi_pbkdf2_block((const uint8_t *)passphrase, passphrase_length,
                         (const uint8_t *)ssid, ssid_length, 1, block);
  memcpy(pmk, block, 20);
  a733_wifi_pbkdf2_block((const uint8_t *)passphrase, passphrase_length,
                         (const uint8_t *)ssid, ssid_length, 2, block);
  memcpy(pmk + 20, block, 12);
  explicit_bzero(block, sizeof(block));
  return OK;
}

static void a733_wifi_wpa_prf(const uint8_t *key, size_t key_length,
                              const char *label, const uint8_t *data,
                              size_t data_length, uint8_t *output,
                              size_t output_length)
{
  uint8_t input[128];
  uint8_t digest[20];
  size_t label_length = strlen(label);
  size_t done = 0;
  uint8_t counter = 0;

  memcpy(input, label, label_length);
  input[label_length] = 0;
  memcpy(input + label_length + 1, data, data_length);
  while (done < output_length)
    {
      size_t copy;

      input[label_length + 1 + data_length] = counter++;
      a733_wifi_hmac_sha1(key, key_length, input,
                          label_length + data_length + 2, digest);
      copy = output_length - done;
      if (copy > sizeof(digest))
        {
          copy = sizeof(digest);
        }

      memcpy(output + done, digest, copy);
      done += copy;
    }

  explicit_bzero(input, sizeof(input));
  explicit_bzero(digest, sizeof(digest));
}

static void a733_wifi_wpa_derive_ptk(const uint8_t anonce[32])
{
  uint8_t material[76];
  uint64_t ticks = (uint64_t)clock_systime_ticks();
  const uint8_t *first;
  const uint8_t *second;

  /* SNonce is a keyed PRF of a fresh timer value, both MAC addresses and the
   * AP nonce.  It is unpredictable without the PMK and unique for every M1.
   */

  memcpy(material, anonce, 32);
  memcpy(material + 32, &ticks, sizeof(ticks));
  memcpy(material + 40, g_wifi.mac, 6);
  memcpy(material + 46, g_wifi.associated_bssid, 6);
  a733_wifi_wpa_prf(g_wifi.wpa_pmk, sizeof(g_wifi.wpa_pmk),
                    "A733 WPA2 SNonce", material, 52,
                    g_wifi.wpa_snonce, sizeof(g_wifi.wpa_snonce));

  first = memcmp(g_wifi.mac, g_wifi.associated_bssid, 6) < 0 ?
          g_wifi.mac : g_wifi.associated_bssid;
  second = first == g_wifi.mac ? g_wifi.associated_bssid : g_wifi.mac;
  memcpy(material, first, 6);
  memcpy(material + 6, second, 6);
  first = memcmp(g_wifi.wpa_snonce, anonce, 32) < 0 ?
          g_wifi.wpa_snonce : anonce;
  second = first == g_wifi.wpa_snonce ? anonce : g_wifi.wpa_snonce;
  memcpy(material + 12, first, 32);
  memcpy(material + 44, second, 32);
  a733_wifi_wpa_prf(g_wifi.wpa_pmk, sizeof(g_wifi.wpa_pmk),
                    "Pairwise key expansion", material, sizeof(material),
                    g_wifi.wpa_ptk, sizeof(g_wifi.wpa_ptk));
  explicit_bzero(material, sizeof(material));
}

static int a733_wifi_wpa_send_key(uint16_t key_info,
                                  const uint8_t replay[8],
                                  const uint8_t *nonce,
                                  const uint8_t *key_data,
                                  size_t key_data_length)
{
  uint8_t frame[14 + A733_WIFI_WPA_EAPOL_MAX];
  uint8_t digest[20];
  uint8_t *eapol = frame + 14;
  size_t eapol_length = 99u + key_data_length;

  if (eapol_length > A733_WIFI_WPA_EAPOL_MAX)
    {
      return -E2BIG;
    }

  memset(frame, 0, 14 + eapol_length);
  memcpy(frame, g_wifi.associated_bssid, 6);
  memcpy(frame + 6, g_wifi.mac, 6);
  frame[12] = 0x88;
  frame[13] = 0x8e;
  eapol[0] = 2;
  eapol[1] = 3;
  a733_putbe16(eapol + 2, eapol_length - 4);
  eapol[4] = 2;
  a733_putbe16(eapol + 5, key_info);
  a733_putbe16(eapol + 7, 16);
  memcpy(eapol + 9, replay, 8);
  if (nonce != NULL)
    {
      memcpy(eapol + 17, nonce, 32);
    }

  a733_putbe16(eapol + 97, key_data_length);
  if (key_data_length > 0)
    {
      memcpy(eapol + 99, key_data, key_data_length);
    }

  if ((key_info & A733_WPA_KEY_MIC) != 0)
    {
      a733_wifi_hmac_sha1(g_wifi.wpa_ptk, 16, eapol,
                          eapol_length, digest);
      memcpy(eapol + 81, digest, 16);
      explicit_bzero(digest, sizeof(digest));
    }

  return a733_wifi_data_send_ethernet(frame, 14 + eapol_length);
}

static int a733_wifi_wpa_install_key(const uint8_t *key, uint8_t key_length,
                                     uint8_t cipher, uint8_t key_index,
                                     bool pairwise)
{
  uint8_t request[44] = {0};
  uint8_t confirmation[8] = {0};
  uint16_t length;
  int ret;

  request[0] = key_index;
  request[1] = pairwise ? g_wifi.association_ap : 0xff;
  if (key_length == 0 || key_length > 32)
    {
      return -EINVAL;
    }

  request[4] = key_length;
  memcpy(request + 8, key, key_length);
  request[40] = cipher;
  request[41] = g_wifi.station_vif_index;
  request[43] = pairwise ? 1 : 0;
  ret = a733_wifi_lmac_exchange(A733_AIC_MM_KEY_ADD_REQ,
                                 A733_AIC_MM_KEY_ADD_CFM,
                                 request, sizeof(request), confirmation,
                                 sizeof(confirmation), &length);
  if (ret < 0 || length < 2 || confirmation[0] != 0)
    {
      return ret < 0 ? ret : -EIO;
    }

  return OK;
}

static int a733_wifi_wpa_open_port(void)
{
  uint8_t request[2] = {g_wifi.association_ap, 1};
  uint8_t confirmation[4] = {0};
  uint16_t length;

  return a733_wifi_lmac_exchange(A733_AIC_ME_PORT_REQ,
                                  A733_AIC_ME_PORT_CFM,
                                  request, sizeof(request), confirmation,
                                  sizeof(confirmation), &length);
}

static int a733_wifi_wpa_mic_valid(const uint8_t *eapol, size_t length)
{
  uint8_t copy[A733_WIFI_WPA_EAPOL_MAX];
  uint8_t digest[20];
  int valid;

  if (length > sizeof(copy) || length < 99)
    {
      return false;
    }

  memcpy(copy, eapol, length);
  memset(copy + 81, 0, 16);
  a733_wifi_hmac_sha1(g_wifi.wpa_ptk, 16, copy, length, digest);
  valid = timingsafe_bcmp(digest, eapol + 81, 16) == 0;
  explicit_bzero(copy, sizeof(copy));
  explicit_bzero(digest, sizeof(digest));
  return valid;
}

static int a733_wifi_wpa_extract_gtk(const uint8_t *encrypted,
                                     size_t encrypted_length,
                                     uint8_t gtk[32], uint8_t gtk_length,
                                     uint8_t *key_index)
{
  static const uint8_t key_wrap_iv[8] =
    {
      0xa6, 0xa6, 0xa6, 0xa6, 0xa6, 0xa6, 0xa6, 0xa6
    };
  rijndael_ctx context;
  uint8_t accumulator[8];
  uint8_t block[16];
  uint8_t plain[A733_WIFI_WPA_EAPOL_MAX];
  size_t plain_length;
  size_t offset;
  size_t count;
  size_t index;
  int round;

  if (encrypted_length < 16 || (encrypted_length & 7u) != 0)
    {
      return -EPROTO;
    }

  plain_length = encrypted_length - 8;
  count = plain_length / 8;
  memcpy(accumulator, encrypted, sizeof(accumulator));
  memcpy(plain, encrypted + 8, plain_length);
  if (rijndael_set_key(&context, g_wifi.wpa_ptk + 16, 128) != 0)
    {
      explicit_bzero(&context, sizeof(context));
      explicit_bzero(plain, sizeof(plain));
      return -EKEYREJECTED;
    }

  for (round = 5; round >= 0; round--)
    {
      for (index = count; index > 0; index--)
        {
          uint64_t value = (uint64_t)round * count + index;
          size_t byte;

          memcpy(block, accumulator, sizeof(accumulator));
          for (byte = 0; byte < sizeof(value); byte++)
            {
              block[7 - byte] ^= (uint8_t)(value >> (byte * 8));
            }

          memcpy(block + 8, plain + (index - 1) * 8, 8);
          rijndael_decrypt(&context, block, block);
          memcpy(accumulator, block, 8);
          memcpy(plain + (index - 1) * 8, block + 8, 8);
        }
    }

  if (timingsafe_bcmp(accumulator, key_wrap_iv,
                      sizeof(accumulator)) != 0)
    {
      explicit_bzero(&context, sizeof(context));
      explicit_bzero(accumulator, sizeof(accumulator));
      explicit_bzero(block, sizeof(block));
      explicit_bzero(plain, sizeof(plain));
      return -EKEYREJECTED;
    }

  for (offset = 0; offset + 2 <= plain_length; )
    {
      size_t element_length = plain[offset + 1];

      if (plain[offset] == 0xdd &&
          element_length >= 6u + gtk_length &&
          offset + 2 + element_length <= plain_length &&
          memcmp(plain + offset + 2, "\x00\x0f\xac\x01", 4) == 0)
        {
          *key_index = plain[offset + 6] & 3u;
          memcpy(gtk, plain + offset + 8, gtk_length);
          explicit_bzero(&context, sizeof(context));
          explicit_bzero(accumulator, sizeof(accumulator));
          explicit_bzero(block, sizeof(block));
          explicit_bzero(plain, sizeof(plain));
          return OK;
        }

      if (plain[offset] == 0 || element_length == 0)
        {
          break;
        }

      offset += 2 + element_length;
    }

  explicit_bzero(&context, sizeof(context));
  explicit_bzero(accumulator, sizeof(accumulator));
  explicit_bzero(block, sizeof(block));
  explicit_bzero(plain, sizeof(plain));
  return -ENOENT;
}

static int a733_wifi_wpa_eapol(const uint8_t *eapol, size_t available)
{
  uint8_t gtk[32];
  uint8_t key_index = 0;
  uint16_t body_length;
  uint16_t key_info;
  uint16_t key_data_length;
  size_t length;
  int ret;

  g_wifi.wpa_diag_available = available > UINT16_MAX ?
                              UINT16_MAX : (uint16_t)available;
  g_wifi.wpa_diag_version = available > 0 ? eapol[0] : 0xff;
  g_wifi.wpa_diag_type = available > 1 ? eapol[1] : 0xff;
  g_wifi.wpa_diag_body_length = available >= 4 ?
                                a733_getbe16(eapol + 2) : 0xffff;
  g_wifi.wpa_diag_descriptor = available > 4 ? eapol[4] : 0xff;
  g_wifi.wpa_diag_key_info = available >= 7 ?
                             a733_getbe16(eapol + 5) : 0xffff;
  g_wifi.wpa_diag_key_data_length = available >= 99 ?
                                    a733_getbe16(eapol + 97) : 0xffff;

  if (!g_wifi.wpa_configured || available < 99 || eapol[1] != 3)
    {
      return -ENOMSG;
    }

  body_length = a733_getbe16(eapol + 2);
  length = 4u + body_length;
  if (length > available || length < 99 || eapol[4] != 2)
    {
      if (g_wifi.wpa_diag_reports++ < 3)
        {
          syslog(LOG_WARNING,
                 "A733 WIFI: malformed EAPOL-Key version=%u type=%u "
                 "body=%u descriptor=%u key-info=%04x key-data=%u "
                 "available=%u\n",
                 g_wifi.wpa_diag_version, g_wifi.wpa_diag_type,
                 g_wifi.wpa_diag_body_length, g_wifi.wpa_diag_descriptor,
                 g_wifi.wpa_diag_key_info, g_wifi.wpa_diag_key_data_length,
                 g_wifi.wpa_diag_available);
        }

      return -EPROTO;
    }

  key_info = a733_getbe16(eapol + 5);
  key_data_length = a733_getbe16(eapol + 97);
  if (99u + key_data_length > length || (key_info & 7u) != 2u)
    {
      if (g_wifi.wpa_diag_reports++ < 3)
        {
          syslog(LOG_WARNING,
                 "A733 WIFI: unsupported EAPOL-Key version=%u type=%u "
                 "body=%u descriptor=%u key-info=%04x key-data=%u "
                 "available=%u; WPA2-PSK requires descriptor-version 2\n",
                 g_wifi.wpa_diag_version, g_wifi.wpa_diag_type,
                 g_wifi.wpa_diag_body_length, g_wifi.wpa_diag_descriptor,
                 g_wifi.wpa_diag_key_info, g_wifi.wpa_diag_key_data_length,
                 g_wifi.wpa_diag_available);
        }

      return -EPROTO;
    }

  if ((key_info & (A733_WPA_KEY_TYPE | A733_WPA_KEY_ACK |
                   A733_WPA_KEY_MIC)) ==
      (A733_WPA_KEY_TYPE | A733_WPA_KEY_ACK))
    {
      struct a733_wifi_scan_s *scan = NULL;
      unsigned int index;

      for (index = 0; index < g_wifi.scan_count; index++)
        {
          if (memcmp(g_wifi.scan[index].bssid,
                     g_wifi.associated_bssid, 6) == 0)
            {
              scan = &g_wifi.scan[index];
              break;
            }
        }

      if (scan == NULL || g_wifi.wpa_assoc_ie_length == 0)
        {
          return -ENOENT;
        }

      memcpy(g_wifi.wpa_replay, eapol + 9, 8);
      a733_wifi_wpa_derive_ptk(eapol + 17);
      ret = a733_wifi_wpa_send_key(0x010au, g_wifi.wpa_replay,
                                   g_wifi.wpa_snonce, g_wifi.wpa_assoc_ie,
                                   g_wifi.wpa_assoc_ie_length);
      if (ret == OK)
        {
          g_wifi.wpa_m1++;
          g_wifi.wpa_state = A733_WPA_WAIT_M3;
          syslog(LOG_INFO, "A733 WIFI: WPA2 message 1/2 passed\n");
        }

      return ret;
    }

  if ((key_info & (A733_WPA_KEY_TYPE | A733_WPA_KEY_ACK |
                   A733_WPA_KEY_MIC)) ==
      (A733_WPA_KEY_TYPE | A733_WPA_KEY_ACK | A733_WPA_KEY_MIC))
    {
      if (g_wifi.wpa_state < A733_WPA_WAIT_M3)
        {
          return -EAGAIN;
        }

      if (memcmp(eapol + 9, g_wifi.wpa_replay, 8) <= 0)
        {
          g_wifi.wpa_replays++;
          return -EALREADY;
        }

      if (!a733_wifi_wpa_mic_valid(eapol, length))
        {
          g_wifi.wpa_mic_failures++;
          return -EKEYREJECTED;
        }

      memcpy(g_wifi.wpa_replay, eapol + 9, 8);
      if ((key_info & A733_WPA_KEY_ENCRYPTED) != 0 && key_data_length > 0)
        {
          ret = a733_wifi_wpa_extract_gtk(eapol + 99, key_data_length,
                                          gtk, g_wifi.wpa_group_key_length,
                                          &key_index);
          if (ret < 0)
            {
              return ret;
            }
        }
      else
        {
          return -EPROTO;
        }

      /* Message 4 must leave through the still-unkeyed data path.  The
       * upstream supplicant sends 4/4 before installing PTK/GTK; installing
       * PTK first made FCU760K encrypt 4/4 at the 802.11 layer.  Some APs
       * tolerated that ordering, but strict and multi-BSSID APs discarded
       * the frame and retransmitted Message 3 until reason 15. */

      ret = a733_wifi_wpa_send_key(0x030au, g_wifi.wpa_replay,
                                   NULL, NULL, 0);
      if (ret < 0)
        {
          explicit_bzero(gtk, sizeof(gtk));
          return ret;
        }

      ret = a733_wifi_wpa_install_key(g_wifi.wpa_ptk + 32, 16, 2,
                                      0, true);
      if (ret < 0)
        {
          explicit_bzero(gtk, sizeof(gtk));
          return ret;
        }

      g_wifi.wpa_pairwise_installed = true;
      ret = a733_wifi_wpa_install_key(gtk, g_wifi.wpa_group_key_length,
                                      g_wifi.wpa_group_cipher, key_index,
                                      false);
      explicit_bzero(gtk, sizeof(gtk));
      if (ret < 0)
        {
          return ret;
        }

      g_wifi.wpa_group_installed = true;
      ret = a733_wifi_wpa_open_port();
      if (ret < 0)
        {
          return ret;
        }

      g_wifi.wpa_port_open = true;
      g_wifi.wpa_state = A733_WPA_COMPLETE;
      g_wifi.wpa_checkpoint = OK;
      g_wifi.wpa_m3++;
      syslog(LOG_INFO,
             "A733 WIFI: WPA2 four-way handshake passed; CCMP PTK/GTK "
             "installed and controlled port open\n");
#ifdef CONFIG_NETUTILS_DHCPC
      ret = a733_wifi_dhcp_start();
      if (ret < 0)
        {
          syslog(LOG_WARNING,
                 "A733 WIFI: automatic DHCP start pending: %d\n", ret);
        }
#endif
      return OK;
    }

  return -ENOMSG;
}

/* WEXT is the standard wireless control ABI used by openvela's WAPI and
 * netinit components.  FCU760K remains a board driver, but scan/association
 * must not require the private /dev/a733-wifi protocol.
 */

static int a733_wifi_wext_wpa_connect(unsigned int index,
                                      const char *passphrase,
                                      size_t passphrase_length)
{
  int ret;

  if (index >= g_wifi.scan_count || passphrase_length < 8 ||
      passphrase_length > 63 || g_wifi.scan[index].assoc_ie_length == 0)
    {
      return -EINVAL;
    }

  explicit_bzero(g_wifi.wpa_pmk, sizeof(g_wifi.wpa_pmk));
  explicit_bzero(g_wifi.wpa_ptk, sizeof(g_wifi.wpa_ptk));
  explicit_bzero(g_wifi.wpa_snonce, sizeof(g_wifi.wpa_snonce));
  explicit_bzero(g_wifi.wpa_assoc_ie, sizeof(g_wifi.wpa_assoc_ie));
  g_wifi.wpa_assoc_ie_length = 0;
  g_wifi.wpa_group_cipher = 0xff;
  g_wifi.wpa_group_key_length = 0;

  ret = a733_wifi_wpa_select_rsn(g_wifi.scan[index].assoc_ie,
                                 g_wifi.scan[index].assoc_ie_length,
                                 g_wifi.wpa_assoc_ie,
                                 &g_wifi.wpa_assoc_ie_length);
  if (ret < 0)
    {
      return ret;
    }

  ret = a733_wifi_wpa_derive_pmk(passphrase, g_wifi.scan[index].ssid,
                                 g_wifi.wpa_pmk);
  if (ret < 0)
    {
      return ret;
    }

  g_wifi.wpa_configured = true;
  g_wifi.wpa_state = A733_WPA_WAIT_M1;
  g_wifi.wpa_checkpoint = -EINPROGRESS;
  g_wifi.wpa_pairwise_installed = false;
  g_wifi.wpa_group_installed = false;
  g_wifi.wpa_port_open = false;
#ifdef CONFIG_NETUTILS_DHCPC
  g_wifi.dhcp_checkpoint = -ENETDOWN;
  g_wifi.dhcp_address = 0;
  g_wifi.dhcp_netmask = 0;
  g_wifi.dhcp_router = 0;
  g_wifi.dhcp_dns = 0;
  g_wifi.dhcp_lease = 0;
#endif
  g_wifi.wpa_diag_reports = 0;
  g_wifi.wpa_diag_available = 0;
  g_wifi.wpa_diag_body_length = 0;
  g_wifi.wpa_diag_key_info = 0;
  g_wifi.wpa_diag_key_data_length = 0;
  g_wifi.wpa_diag_version = 0;
  g_wifi.wpa_diag_type = 0;
  g_wifi.wpa_diag_descriptor = 0;
  memset(g_wifi.wpa_replay, 0, sizeof(g_wifi.wpa_replay));

  ret = a733_wifi_associate(index);
  if (ret == OK)
    {
      ret = a733_wifi_data_send_eapol_start();
    }

  if (ret < 0)
    {
      g_wifi.wpa_checkpoint = ret;
      g_wifi.wpa_configured = false;
      explicit_bzero(g_wifi.wpa_pmk, sizeof(g_wifi.wpa_pmk));
      return ret;
    }

  syslog(LOG_INFO,
         "A733 WIFI: openvela WEXT WPA2 started ssid='%s'; waiting for "
         "four-way handshake\n", g_wifi.associated_ssid);
  return OK;
}

static int a733_wifi_wext_scan_results(struct iwreq *iwr)
{
  size_t required = 0;
  char *pointer;
  unsigned int index;

  for (index = 0; index < g_wifi.scan_count; index++)
    {
      size_t ssid_length = strnlen(g_wifi.scan[index].ssid, 32);

      required += IW_EV_LEN(ap_addr) + IW_EV_LEN(freq) + IW_EV_LEN(qual) +
                  IW_EV_LEN(data) + IW_EV_LEN(essid) +
                  ((ssid_length + 3u) & ~3u);
    }

  if (iwr->u.data.pointer == NULL || iwr->u.data.length < required)
    {
      iwr->u.data.length = required;
      return required == 0 ? OK : -E2BIG;
    }

  pointer = iwr->u.data.pointer;
  for (index = 0; index < g_wifi.scan_count; index++)
    {
      struct a733_wifi_scan_s *scan = &g_wifi.scan[index];
      struct iw_event *event;
      size_t ssid_length = strnlen(scan->ssid, 32);

      event = (struct iw_event *)pointer;
      memset(event, 0, IW_EV_LEN(ap_addr));
      event->cmd = SIOCGIWAP;
      event->u.ap_addr.sa_family = ARPHRD_ETHER;
      memcpy(event->u.ap_addr.sa_data, scan->bssid, 6);
      event->len = IW_EV_LEN(ap_addr);
      pointer += event->len;

      event = (struct iw_event *)pointer;
      memset(event, 0, IW_EV_LEN(essid) + ((ssid_length + 3u) & ~3u));
      event->cmd = SIOCGIWESSID;
      event->u.essid.flags = 1;
      event->u.essid.length = ssid_length;
      event->u.essid.pointer = (void *)sizeof(event->u.essid);
      memcpy(&event->u.essid + 1, scan->ssid, ssid_length);
      event->len = IW_EV_LEN(essid) + ((ssid_length + 3u) & ~3u);
      pointer += event->len;

      event = (struct iw_event *)pointer;
      memset(event, 0, IW_EV_LEN(qual));
      event->cmd = IWEVQUAL;
      event->u.qual.level = (uint8_t)scan->rssi;
      event->u.qual.updated = IW_QUAL_DBM | IW_QUAL_LEVEL_UPDATED |
                              IW_QUAL_QUAL_INVALID |
                              IW_QUAL_NOISE_INVALID;
      event->len = IW_EV_LEN(qual);
      pointer += event->len;

      event = (struct iw_event *)pointer;
      memset(event, 0, IW_EV_LEN(freq));
      event->cmd = SIOCGIWFREQ;
      event->u.freq.m = scan->frequency * 100000;
      event->u.freq.e = 1;
      event->len = IW_EV_LEN(freq);
      pointer += event->len;

      event = (struct iw_event *)pointer;
      memset(event, 0, IW_EV_LEN(data));
      event->cmd = SIOCGIWENCODE;
      event->u.data.flags = scan->privacy ?
                            IW_ENCODE_ENABLED | IW_ENCODE_NOKEY :
                            IW_ENCODE_DISABLED;
      event->len = IW_EV_LEN(data);
      pointer += event->len;
    }

  iwr->u.data.length = pointer - (char *)iwr->u.data.pointer;
  return OK;
}

static int a733_wifi_wext_find(const char *ssid, size_t ssid_length)
{
  int selected = -ENOENT;
  int best_rssi = -128;
  unsigned int index;

  for (index = 0; index < g_wifi.scan_count; index++)
    {
      struct a733_wifi_scan_s *scan = &g_wifi.scan[index];

      if (strnlen(scan->ssid, 32) != ssid_length ||
          memcmp(scan->ssid, ssid, ssid_length) != 0)
        {
          continue;
        }

      if (g_wifi.wext_frequency != 0 &&
          scan->frequency != g_wifi.wext_frequency)
        {
          continue;
        }

      if (memcmp(g_wifi.wext_bssid, "\0\0\0\0\0\0", 6) != 0 &&
          memcmp(scan->bssid, g_wifi.wext_bssid, 6) != 0)
        {
          continue;
        }

      if (selected < 0 || scan->rssi > best_rssi)
        {
          selected = index;
          best_rssi = scan->rssi;
        }
    }

  return selected;
}

#ifdef CONFIG_NETDEV_IOCTL
static int a733_wifi_net_ioctl(struct net_driver_s *dev, int cmd,
                               unsigned long arg)
{
  struct iwreq *iwr = (struct iwreq *)arg;
  int index;
  int ret = OK;

  (void)dev;
  switch (cmd)
    {
      case SIOCSIWSCAN:
        return a733_wifi_scan_group(true, true, false);

      case SIOCGIWSCAN:
        return a733_wifi_wext_scan_results(iwr);

      case SIOCSIWMODE:
        return iwr->u.mode == IW_MODE_INFRA ||
               iwr->u.mode == IW_MODE_AUTO ? OK : -ENOTSUP;

      case SIOCGIWMODE:
        iwr->u.mode = IW_MODE_INFRA;
        return OK;

      case SIOCSIWAUTH:
        switch (iwr->u.param.flags & IW_AUTH_INDEX)
          {
            case IW_AUTH_WPA_VERSION:
              g_wifi.wext_wpa_version = iwr->u.param.value;
              return OK;
            case IW_AUTH_CIPHER_PAIRWISE:
            case IW_AUTH_CIPHER_GROUP:
              g_wifi.wext_pairwise_cipher = iwr->u.param.value;
              return OK;
            default:
              return OK;
          }

      case SIOCGIWAUTH:
        if ((iwr->u.param.flags & IW_AUTH_INDEX) == IW_AUTH_WPA_VERSION)
          {
            iwr->u.param.value = g_wifi.wext_wpa_version;
          }
        else
          {
            iwr->u.param.value = g_wifi.wext_pairwise_cipher;
          }
        return OK;

      case SIOCSIWENCODEEXT:
        {
          struct iw_encode_ext *ext = iwr->u.encoding.pointer;

          if (ext == NULL || ext->key_len > 63 ||
              iwr->u.encoding.length < sizeof(*ext) + ext->key_len)
            {
              return -EINVAL;
            }

          explicit_bzero(g_wifi.wext_passphrase,
                         sizeof(g_wifi.wext_passphrase));
          memcpy(g_wifi.wext_passphrase, ext->key, ext->key_len);
          g_wifi.wext_passphrase_length = ext->key_len;
          return OK;
        }

      case SIOCGIWENCODEEXT:
        {
          struct iw_encode_ext *ext = iwr->u.encoding.pointer;

          if (ext == NULL || iwr->u.encoding.length < sizeof(*ext))
            {
              return -E2BIG;
            }

          /* Passphrases are write-only.  Never copy a saved secret back to
           * a caller through WEXT or a diagnostic command.
           */

          memset(ext, 0, sizeof(*ext));
          ext->alg = IW_ENCODE_ALG_CCMP;
          ext->key_len = 0;
          iwr->u.encoding.flags = IW_ENCODE_ENABLED | IW_ENCODE_NOKEY;
          iwr->u.encoding.length = sizeof(*ext);
          return OK;
        }

      case SIOCSIWFREQ:
        if (iwr->u.freq.e == 0)
          {
            int channel = iwr->u.freq.m;
            g_wifi.wext_frequency = channel == 14 ? 2484 :
              channel <= 13 ? 2407 + channel * 5 : 5000 + channel * 5;
          }
        else if (iwr->u.freq.e == 1)
          {
            g_wifi.wext_frequency = iwr->u.freq.m / 100000;
          }
        else
          {
            return -EINVAL;
          }
        return OK;

      case SIOCGIWFREQ:
        iwr->u.freq.m = g_wifi.wext_frequency * 100000;
        iwr->u.freq.e = 1;
        return OK;

      case SIOCSIWAP:
        if (memcmp(iwr->u.ap_addr.sa_data, "\0\0\0\0\0\0", 6) == 0)
          {
            memset(g_wifi.wext_bssid, 0, sizeof(g_wifi.wext_bssid));
            return g_wifi.associated ? a733_wifi_disconnect() : OK;
          }
        memcpy(g_wifi.wext_bssid, iwr->u.ap_addr.sa_data, 6);
        return OK;

      case SIOCGIWAP:
        iwr->u.ap_addr.sa_family = ARPHRD_ETHER;
        memcpy(iwr->u.ap_addr.sa_data, g_wifi.associated_bssid, 6);
        return OK;

      case SIOCSIWESSID:
        if (iwr->u.essid.pointer == NULL || iwr->u.essid.length == 0 ||
            iwr->u.essid.flags == 0)
          {
            return g_wifi.associated ? a733_wifi_disconnect() : OK;
          }
        if (iwr->u.essid.length > 32)
          {
            return -EINVAL;
          }
        index = a733_wifi_wext_find(iwr->u.essid.pointer,
                                    iwr->u.essid.length);
        if (index < 0)
          {
            return index;
          }
        if (g_wifi.wext_wpa_version == IW_AUTH_WPA_VERSION_DISABLED ||
            g_wifi.scan[index].assoc_ie_length == 0)
          {
            g_wifi.wpa_configured = false;
            return a733_wifi_associate(index);
          }
        if ((g_wifi.wext_wpa_version & IW_AUTH_WPA_VERSION_WPA2) == 0 ||
            (g_wifi.wext_pairwise_cipher & IW_AUTH_CIPHER_CCMP) == 0)
          {
            return -ENOTSUP;
          }
        return a733_wifi_wext_wpa_connect(
          index, g_wifi.wext_passphrase, g_wifi.wext_passphrase_length);

      case SIOCGIWESSID:
        if (iwr->u.essid.pointer == NULL ||
            iwr->u.essid.length < strlen(g_wifi.associated_ssid))
          {
            iwr->u.essid.length = strlen(g_wifi.associated_ssid);
            return -E2BIG;
          }
        iwr->u.essid.length = strlen(g_wifi.associated_ssid);
        memcpy(iwr->u.essid.pointer, g_wifi.associated_ssid,
               iwr->u.essid.length);
        iwr->u.essid.flags = g_wifi.associated ? 1 : 0;
        return OK;

      case SIOCGIWSENS:
        iwr->u.sens.value = g_wifi.associated ? 0 : -128;
        iwr->u.sens.flags = IW_QUAL_DBM;
        return OK;

      case SIOCGIWCOUNTRY:
        if (iwr->u.data.pointer == NULL || iwr->u.data.length < 3)
          {
            iwr->u.data.length = 3;
            return -E2BIG;
          }
        memcpy(iwr->u.data.pointer, "CN", 3);
        iwr->u.data.length = 3;
        return OK;

      case SIOCSIWCOUNTRY:
        return iwr->u.data.pointer != NULL &&
               strncmp(iwr->u.data.pointer, "CN", 2) == 0 ? OK : -ENOTSUP;

      case SIOCGIWNAME:
        strlcpy(iwr->u.name, "IEEE 802.11", sizeof(iwr->u.name));
        return OK;

      default:
        ret = -ENOTTY;
        break;
    }

  return ret;
}
#endif

static int a733_wifi_net_txpoll(struct net_driver_s *dev)
{
  int ret = OK;

  if (dev->d_len > 0)
    {
      ret = a733_wifi_data_send_ethernet(dev->d_buf, dev->d_len);
      if (ret == OK)
        {
          NETDEV_TXPACKETS(dev);
        }
      else
        {
          NETDEV_TXERRORS(dev);
        }

      dev->d_len = 0;
    }

  return ret;
}

static int a733_wifi_net_ifup(struct net_driver_s *dev)
{
  struct a733_wifi_netdev_s *priv = dev->d_private;

  priv->ifup = true;
  if (g_wifi.associated)
    {
      netdev_carrier_on(dev);
    }

  return OK;
}

static int a733_wifi_net_ifdown(struct net_driver_s *dev)
{
  struct a733_wifi_netdev_s *priv = dev->d_private;

  priv->ifup = false;
  netdev_carrier_off(dev);
  return OK;
}

static int a733_wifi_net_txavail(struct net_driver_s *dev)
{
  struct a733_wifi_netdev_s *priv = dev->d_private;

  if (priv->ifup && g_wifi.associated)
    {
      devif_poll(dev, a733_wifi_net_txpoll);
    }

  return OK;
}

static size_t a733_wifi_net_payload_length(uint16_t ethertype,
                                            const uint8_t *payload,
                                            size_t available)
{
  size_t length = available;

  if (ethertype == 0x0800 && available >= 4)
    {
      length = ((size_t)payload[2] << 8) | payload[3];
    }
  else if (ethertype == 0x0806 && available >= 28)
    {
      length = 28;
    }
  else if (ethertype == 0x86dd && available >= 6)
    {
      length = 40u + (((size_t)payload[4] << 8) | payload[5]);
    }
  else if (ethertype == 0x888e && available >= 4)
    {
      length = 4u + (((size_t)payload[2] << 8) | payload[3]);
    }

  return length <= available ? length : 0;
}

static void a733_wifi_net_receive(void)
{
  struct net_driver_s *dev = &g_wifi_netdev.dev;
  const uint8_t *payload;
  size_t payload_length;

  if (g_wifi.data_llc_offset == 0 || g_wifi.data_payload_length == 0)
    {
      return;
    }

  payload = g_wifi_data_rx + g_wifi.data_llc_offset + 8;
  payload_length = a733_wifi_net_payload_length(g_wifi.data_ethertype,
                                                 payload,
                                                 g_wifi.data_payload_length);
  if (payload_length == 0 || payload_length + 14 > sizeof(g_wifi_netbuf))
    {
      NETDEV_RXDROPPED(dev);
      return;
    }

  memcpy(g_wifi_netbuf, g_wifi.data_destination, 6);
  memcpy(g_wifi_netbuf + 6, g_wifi.data_source, 6);
  g_wifi_netbuf[12] = g_wifi.data_ethertype >> 8;
  g_wifi_netbuf[13] = g_wifi.data_ethertype & 0xff;
  memcpy(g_wifi_netbuf + 14, payload, payload_length);
  dev->d_buf = g_wifi_netbuf;
  dev->d_len = payload_length + 14;

  if (g_wifi.data_ethertype == 0x0806)
    {
      g_wifi.data_arp_frames++;
      if (payload_length >= 28)
        {
          g_wifi.data_arp_operation =
            ((uint16_t)payload[6] << 8) | payload[7];
          memcpy(&g_wifi.data_arp_sender_ip, payload + 14, 4);
          memcpy(&g_wifi.data_arp_target_ip, payload + 24, 4);
          if (g_wifi.data_arp_operation == 1)
            {
              g_wifi.data_arp_requests++;
            }
          else if (g_wifi.data_arp_operation == 2)
            {
              g_wifi.data_arp_replies++;
            }
        }
    }
  else if (g_wifi.data_ethertype == 0x0800 && payload_length >= 20)
    {
      size_t ihl = (payload[0] & 0x0f) * 4u;

      g_wifi.data_ipv4_frames++;
      memcpy(&g_wifi.data_ipv4_source, payload + 12, 4);
      memcpy(&g_wifi.data_ipv4_destination, payload + 16, 4);
      if (payload[9] == 6 && ihl >= 20 && payload_length >= ihl + 20)
        {
          const uint8_t *tcp = payload + ihl;

          g_wifi.data_tcp_frames++;
          g_wifi.data_tcp_source_port =
            ((uint16_t)tcp[0] << 8) | tcp[1];
          g_wifi.data_tcp_destination_port =
            ((uint16_t)tcp[2] << 8) | tcp[3];
          if ((tcp[13] & 0x02) != 0)
            {
              g_wifi.data_tcp_syn_frames++;
            }
        }
    }

#ifdef CONFIG_NET_PKT
  pkt_input(dev);
#endif

  if (g_wifi.data_ethertype == 0x0800)
    {
      NETDEV_RXIPV4(dev);
      ipv4_input(dev);
    }
#ifdef CONFIG_NET_IPv6
  else if (g_wifi.data_ethertype == 0x86dd)
    {
      NETDEV_RXIPV6(dev);
      ipv6_input(dev);
    }
#endif
#ifdef CONFIG_NET_ARP
  else if (g_wifi.data_ethertype == 0x0806)
    {
      NETDEV_RXARP(dev);
      arp_input(dev);
    }
#endif
  else
    {
      if (g_wifi.data_ethertype == 0x888e)
        {
          int wpa_ret = a733_wifi_wpa_eapol(payload, payload_length);

          if (wpa_ret < 0 && wpa_ret != -ENOMSG &&
              wpa_ret != -EALREADY)
            {
              g_wifi.wpa_checkpoint = wpa_ret;
              syslog(LOG_WARNING,
                     "A733 WIFI: WPA2 EAPOL processing failed: %d\n",
                     wpa_ret);
            }
        }

      dev->d_len = 0;
      NETDEV_RXDROPPED(dev);
    }

  if (dev->d_len > 0)
    {
      a733_wifi_net_txpoll(dev);
    }
}

/* Drain the dedicated runtime message endpoint while the data path is live.
 * FCU760K returns USB_TYPE_CFG_DATA_CFM for transmitted WLAN packets and can
 * also deliver unsolicited disconnect indications here.  Leaving these
 * packets queued eventually exhausts firmware message flow control and makes
 * data, scan and association all appear to hang although the USB root port is
 * still connected.  This is the polling equivalent of the official driver's
 * permanently submitted message RX URBs.
 */

static int a733_wifi_message_poll(unsigned int timeout_ms)
{
  bool carrier_off = false;
  uint16_t message_id;
  uint16_t parameter_length;
  uint8_t type;
  size_t actual;
  int ret;

  ret = nxmutex_trylock(&g_wifi_msg_lock);
  if (ret < 0)
    {
      return ret;
    }

  ret = a733_ehci_bulk_timeout(g_wifi.address, g_wifi.wifi_msg_in,
                               g_wifi.runtime_maxpacket, true,
                               g_wifi_aic_rx, sizeof(g_wifi_aic_rx),
                               &actual, &g_wifi.wifi_msg_in_toggle,
                               timeout_ms);
  if (ret < 0)
    {
      nxmutex_unlock(&g_wifi_msg_lock);
      return ret;
    }

  g_wifi.wifi_msg_frames++;
  type = actual >= 3 ? g_wifi_aic_rx[2] & 0x7fu : 0;
  if (type == A733_AIC_USB_TYPE_DATA_CFM)
    {
      g_wifi.wifi_data_confirmations++;
    }
  else if (type == A733_AIC_USB_TYPE_CMD && actual >= 16)
    {
      message_id = a733_getle16(g_wifi_aic_rx + 4);
      parameter_length = a733_getle16(g_wifi_aic_rx + 10);
      g_wifi.wifi_async_messages++;

      if (message_id == A733_AIC_SM_DISCONNECT_IND &&
          parameter_length >= 3 && actual >= 16u + parameter_length)
        {
          g_wifi.disconnect_reason = a733_getle16(g_wifi_aic_rx + 16);
          g_wifi.disconnect_indicated = true;
          g_wifi.associated = false;
          g_wifi.wpa_port_open = false;
          g_wifi.wpa_pairwise_installed = false;
          g_wifi.wpa_group_installed = false;
          g_wifi.wpa_checkpoint = -ENETDOWN;
          g_wifi.wpa_state = A733_WPA_WAIT_M1;
          carrier_off = true;
        }
    }

  nxmutex_unlock(&g_wifi_msg_lock);

  if (carrier_off)
    {
      a733_wifi_net_carrier(false);
      syslog(LOG_WARNING,
             "A733 WIFI: asynchronous disconnect reason=%u; carrier off\n",
             g_wifi.disconnect_reason);
    }

  return OK;
}

static int a733_wifi_rx_thread(int argc, char *argv[])
{
  struct a733_wifi_netdev_s *priv = &g_wifi_netdev;
  unsigned int message_tick = 0;
  unsigned int timeout_ms;
  int ret;

  for (;;)
    {
      /* DATA_CFM is expected after every data OUT.  Drain it immediately;
       * also sample the message endpoint periodically for unsolicited link
       * events.  trylock keeps synchronous WAPI commands as the sole owner
       * of their confirmations.
       */

      if (g_wifi.wifi_data_confirmations < g_wifi.data_tx_frames ||
          ++message_tick >= 64)
        {
          a733_wifi_message_poll(1);
          message_tick = 0;
        }

      if (!priv->ifup || !g_wifi.associated)
        {
          nxsig_usleep(50000);
          continue;
        }

      /* The EHCI checkpoint still uses one shared asynchronous QH.  Keep the
       * proven 20 ms receive window while the WPA handshake is waiting for
       * asynchronous EAPOL messages.  Once the controlled port is open, use
       * short correctly-accounted polls and never start one while the network
       * stack is waiting to transmit.  The old unconditional 20 ms poll held
       * g_wifi_usb_lock for its whole timeout and limited TX to about 46
       * packets/s regardless of link rate.
       */

      timeout_ms = g_wifi.wpa_configured && !g_wifi.wpa_port_open ?
                   A733_WIFI_RX_AUTH_TIMEOUT_MS :
                   A733_WIFI_RX_DATA_TIMEOUT_MS;

      if (g_wifi_tx_pending)
        {
          nxsig_usleep(A733_WIFI_RX_IDLE_US);
          continue;
        }

      ret = a733_wifi_data_receive_timeout(timeout_ms);
      if (ret == OK)
        {
          net_lock();
          a733_wifi_net_receive();
          net_unlock();
        }
      else if (ret != -ETIMEDOUT && ret != -ENOMSG)
        {
          nxsig_usleep(A733_WIFI_RX_ERROR_US);
        }
      else
        {
          /* Yield after an empty poll so a newly queued TX cannot be
           * starved by the receive worker immediately reacquiring the QH.
           */

          nxsig_usleep(A733_WIFI_RX_IDLE_US);
        }
    }

  return OK;
}

static int a733_wifi_netdev_register(void)
{
  struct a733_wifi_netdev_s *priv = &g_wifi_netdev;
  int ret;

  memset(priv, 0, sizeof(*priv));
  priv->dev.d_buf = g_wifi_netbuf;
  priv->dev.d_ifup = a733_wifi_net_ifup;
  priv->dev.d_ifdown = a733_wifi_net_ifdown;
  priv->dev.d_txavail = a733_wifi_net_txavail;
#ifdef CONFIG_NETDEV_IOCTL
  priv->dev.d_ioctl = a733_wifi_net_ioctl;
#endif
  priv->dev.d_private = priv;
  memcpy(priv->dev.d_mac.ether.ether_addr_octet,
         g_wifi.mac, sizeof(g_wifi.mac));

  ret = netdev_register(&priv->dev, NET_LL_IEEE80211);
  if (ret < 0)
    {
      return ret;
    }

  priv->registered = true;
  netdev_carrier_off(&priv->dev);
  priv->rxpid = kthread_create("a733-wlan-rx", A733_WIFI_RX_PRIORITY,
                               A733_WIFI_RX_STACKSIZE,
                               a733_wifi_rx_thread, NULL);
  if (priv->rxpid < 0)
    {
      return priv->rxpid;
    }

  syslog(LOG_INFO,
         "A733 WIFI: wlan0 registered; serialized USB RX worker pid=%d\n",
         priv->rxpid);
  return OK;
}
#endif /* CONFIG_NET */

static int a733_wifi_data_tx_checkpoint(void)
{
  uint32_t eapol_before = g_wifi.data_eapol_frames;
  int ret;

  ret = a733_wifi_data_send_eapol_start();
  if (ret < 0)
    {
      return ret;
    }

  /* A protected AP answers EAPOL-Start by (re)sending Message 1.  Observing
   * that response proves both endpoint 01 TX and endpoint 81 RX without
   * installing a deliberately incomplete key.
   */

  ret = a733_wifi_data_checkpoint();
  if (ret < 0)
    {
      return ret;
    }

  if (g_wifi.data_eapol_frames <= eapol_before)
    {
      return -ENOMSG;
    }

  syslog(LOG_INFO,
         "A733 WIFI: WLAN TX endpoint passed, EAPOL-Start %lu/%lu "
         "bytes and AP Message 1 received\n",
         (unsigned long)g_wifi.data_tx_actual,
         (unsigned long)(4 + 28 + 4));
  return OK;
}

static void a733_wifi_snapshot(void)
{
  uintptr_t opbase = A733_EHCI1_BASE + g_wifi.caplength;

  g_wifi.ccu_ahb = getreg32(A733_CCU_AHB_MASTER);
  g_wifi.ccu_phy = getreg32(A733_CCU_USB1_PHY);
  g_wifi.ccu_hci = getreg32(A733_CCU_USB1_HCI);
  g_wifi.pm_cfg = getreg32(A733_PM_CFG0);
  g_wifi.pm_data = getreg32(A733_PM_DATA);
  g_wifi.pmu = getreg32(A733_USB1_PMU);
  g_wifi.phy_ctrl = getreg32(A733_USB1_PHY_CTRL);

  if (g_wifi.caplength >= 0x10 && g_wifi.caplength <= 0x80)
    {
      g_wifi.usbcmd = getreg32(opbase + 0x00);
      g_wifi.usbsts = getreg32(opbase + 0x04);
      g_wifi.portsc = getreg32(opbase + 0x44);
    }
}

static ssize_t a733_wifi_read(struct file *filep, char *buffer,
                              size_t buflen)
{
  char *report = g_wifi_report;
  const char *state;
  size_t length;
  size_t copy;
  unsigned int index;

  a733_wifi_snapshot();

  if ((g_wifi.portsc & EHCI_PORTSC_CONNECT) == 0)
    {
      state = "not-connected";
    }
  else if ((g_wifi.portsc & EHCI_PORTSC_OWNER) != 0)
    {
      state = "full/low-speed-companion";
    }
  else if ((g_wifi.portsc & EHCI_PORTSC_ENABLE) != 0)
    {
      state = "connected-high-speed";
    }
  else
    {
      state = "connected-not-enabled";
    }

  length = (size_t)snprintf(report, sizeof(g_wifi_report),
    "Cubie A7Z onboard wireless checkpoint\n"
    "module: FCU760K Wi-Fi 6 + Bluetooth 5.4, dedicated USB1\n"
    "power: WIFI_3V3=PM0 WL-REG-ON=PM1 cfg=%08lx data=%08lx\n"
    "clock: ahb=%08lx phy=%08lx hci=%08lx\n"
    "phy: pmu=%08lx ctrl=%08lx siddq=%lu\n"
    "ehci1: caplen=%u version=%04x hcs=%08lx cmd=%08lx sts=%08lx "
    "port=%08lx state=%s checkpoint=%d\n"
    "ohci1: revision=%08lx control=%08lx (companion only)\n"
    "enumeration: address=%u VID:PID=%04x:%04x class=%02x/%02x/%02x "
    "bcdUSB=%x.%02x EP0=%u config=%u interfaces=%u/%u endpoints=%u "
    "total=%u checkpoint=%d\n",
    (unsigned long)g_wifi.pm_cfg, (unsigned long)g_wifi.pm_data,
    (unsigned long)g_wifi.ccu_ahb, (unsigned long)g_wifi.ccu_phy,
    (unsigned long)g_wifi.ccu_hci, (unsigned long)g_wifi.pmu,
    (unsigned long)g_wifi.phy_ctrl,
    (unsigned long)((g_wifi.phy_ctrl >> 3) & 1u),
    g_wifi.caplength, g_wifi.hciversion,
    (unsigned long)g_wifi.hcsparams, (unsigned long)g_wifi.usbcmd,
    (unsigned long)g_wifi.usbsts, (unsigned long)g_wifi.portsc,
    state, g_wifi.checkpoint,
    (unsigned long)getreg32(A733_OHCI1_BASE),
    (unsigned long)getreg32(A733_OHCI1_BASE + 4),
    g_wifi.address, g_wifi.vid, g_wifi.pid, g_wifi.device_class,
    g_wifi.device_subclass, g_wifi.device_protocol,
    g_wifi.bcdusb >> 8, g_wifi.bcdusb & 0xffu,
    g_wifi.ep0_maxpacket, g_wifi.configuration, g_wifi.interface_count,
    g_wifi.interface_descriptors, g_wifi.endpoint_count,
    g_wifi.config_length, g_wifi.enumeration);

  if (length >= sizeof(g_wifi_report))
    {
      length = sizeof(g_wifi_report) - 1;
    }

  for (index = 0; index < g_wifi.endpoint_count &&
                  length < sizeof(g_wifi_report) - 1; index++)
    {
      struct a733_wifi_endpoint_s *endpoint = &g_wifi.endpoints[index];
      int written = snprintf(report + length, sizeof(g_wifi_report) - length,
        "endpoint[%u]: interface=%u/%u address=%02x attr=%02x "
        "maxpacket=%u interval=%u\n",
        index, endpoint->interface, endpoint->alternate,
        endpoint->address, endpoint->attributes, endpoint->maxpacket,
        endpoint->interval);

      if (written < 0)
        {
          break;
        }

      if ((size_t)written >= sizeof(g_wifi_report) - length)
        {
          length = sizeof(g_wifi_report) - 1;
          break;
        }

      length += (size_t)written;
    }

  if (length < sizeof(g_wifi_report) - 1)
    {
      int written = snprintf(report + length,
                             sizeof(g_wifi_report) - length,
        "bulk: out=%02x in=%02x maxpacket=%u command=%lu response=%lu\n"
        "protocol: read32[%08lx]=%08lx response-id=%04x checkpoint=%d\n"
        "firmware: branch=D80-U02 chip-rev=%02x bytes=%lu "
        "runtime=%04x:%04x checkpoint=%d\n"
        "runtime-map: WLAN data=%02x/%02x msg=%02x/%02x maxpacket=%u; "
        "BT event=%02x ACL=%02x/%02x checkpoint=%d\n"
        "lmac: version=%08lx machw=%08lx/%08lx phy=%08lx/%08lx "
        "features=%08lx sta=%u vif=%u\n"
        "stack: checkpoint=%d 5g=%u vendor=%02x "
        "mac=%02x:%02x:%02x:%02x:%02x:%02x\n"
        "rf: checkpoint=%d rx=%08lx/%08lx tx=%08lx/%08lx\n"
        "me: checkpoint=%d profile=HT40/PS-off\n"
        "regulatory: checkpoint=%d domain=CN channels=%u/%u\n"
        "station: checkpoint=%d vif=%u mac-start=%d host-netdev="
        A733_WIFI_HOST_NETDEV "\n"
        "msg-rx: frames=%lu data-cfm=%lu async=%lu "
        "pump=serialized-background\n"
        "scan: checkpoint=%d results=%u indications=%u firmware=%u ack=%u "
        "last=%04x status=%u "
        "control='echo scanall|scan2|scan5|scandfs > /dev/a733-wifi'\n"
        "association: checkpoint=%d cfm=%u status=%u associated=%u "
        "vif=%u ap=%u channel=%u bssid=%02x:%02x:%02x:%02x:%02x:%02x "
        "ssid='%s'\n"
        "disconnect: checkpoint=%d cfm=%u ind=%u reason=%u\n"
        "data: checkpoint=%d endpoint=%02x actual=%lu packet=%u "
        "fc=%04x ethertype=%04x frames=%lu eapol=%lu "
        "src=%02x:%02x:%02x:%02x:%02x:%02x "
        "dst=%02x:%02x:%02x:%02x:%02x:%02x\n"
        "lan-rx: arp=%lu req=%lu reply=%lu op=%u sender=%u.%u.%u.%u "
        "target=%u.%u.%u.%u tx-arp=%lu ipv4=%lu tcp=%lu syn=%lu "
        "last-ip=%u.%u.%u.%u>"
        "%u.%u.%u.%u last-tcp=%u>%u\n"
        "data-tx: checkpoint=%d endpoint=%02x actual=%lu frames=%lu "
        "layout=usb4+hostdesc28+ethernet-payload\n"
        "wpa2: checkpoint=%d state=%u m1=%lu m3=%lu mic-fail=%lu "
        "replay=%lu ptk=%u gtk=%u port=%u rsn-ie=%u group=%s/%u\n"
        "eapol: available=%u version=%u type=%u body=%u descriptor=%u "
        "key-info=%04x key-data=%u diagnostics=%u\n"
        "control: 'echo associate=<bss-index> > /dev/a733-wifi' or "
        "'echo datacheck=<bss-index> > /dev/a733-wifi' or "
        "'echo txcheck=<bss-index> > /dev/a733-wifi' or "
        "'echo wpa2=<bss-index>,<passphrase> > /dev/a733-wifi' or "
        "'echo dhcp > /dev/a733-wifi' or "
        "'echo disconnect > /dev/a733-wifi'\n"
        "scope: %s\n",
        g_wifi.bulk_out, g_wifi.bulk_in, g_wifi.bulk_maxpacket,
        (unsigned long)g_wifi.command_actual,
        (unsigned long)g_wifi.response_actual,
        (unsigned long)g_wifi.probe_address,
        (unsigned long)g_wifi.probe_value, g_wifi.response_id,
        g_wifi.protocol,
        g_wifi.chip_revision, (unsigned long)g_wifi.firmware_bytes,
        g_wifi.runtime_vid, g_wifi.runtime_pid, g_wifi.firmware,
        g_wifi.wifi_data_out, g_wifi.wifi_data_in,
        g_wifi.wifi_msg_out, g_wifi.wifi_msg_in,
        g_wifi.runtime_maxpacket, g_wifi.bt_event_in,
        g_wifi.bt_acl_out, g_wifi.bt_acl_in, g_wifi.runtime_transport,
        (unsigned long)g_wifi.lmac_version,
        (unsigned long)g_wifi.machw_version1,
        (unsigned long)g_wifi.machw_version2,
        (unsigned long)g_wifi.phy_version1,
        (unsigned long)g_wifi.phy_version2,
        (unsigned long)g_wifi.lmac_features,
        g_wifi.lmac_max_sta, g_wifi.lmac_max_vif,
        g_wifi.stack_start, g_wifi.band_5g, g_wifi.vendor_info,
        g_wifi.mac[0], g_wifi.mac[1], g_wifi.mac[2],
        g_wifi.mac[3], g_wifi.mac[4], g_wifi.mac[5],
        g_wifi.rf_config,
        (unsigned long)g_wifi.rf_rxgain_24g,
        (unsigned long)g_wifi.rf_rxgain_5g,
        (unsigned long)g_wifi.rf_txgain_24g,
        (unsigned long)g_wifi.rf_txgain_5g,
        g_wifi.me_config,
        g_wifi.channel_config, g_wifi.channel_2g_count,
        g_wifi.channel_5g_count,
        g_wifi.station_vif, g_wifi.station_vif_index, g_wifi.mac_start,
        (unsigned long)g_wifi.wifi_msg_frames,
        (unsigned long)g_wifi.wifi_data_confirmations,
        (unsigned long)g_wifi.wifi_async_messages,
        g_wifi.scan_checkpoint, g_wifi.scan_count,
        g_wifi.scan_result_messages, g_wifi.scan_firmware_count,
        g_wifi.scan_acknowledged,
        g_wifi.scan_last_message, g_wifi.scan_status,
        g_wifi.association_checkpoint, g_wifi.association_cfm_status,
        g_wifi.association_status_code, g_wifi.associated,
        g_wifi.association_vif, g_wifi.association_ap,
        g_wifi.association_channel,
        g_wifi.associated_bssid[0], g_wifi.associated_bssid[1],
        g_wifi.associated_bssid[2], g_wifi.associated_bssid[3],
        g_wifi.associated_bssid[4], g_wifi.associated_bssid[5],
        g_wifi.associated_ssid,
        g_wifi.disconnect_checkpoint, g_wifi.disconnect_confirmed,
        g_wifi.disconnect_indicated, g_wifi.disconnect_reason,
        g_wifi.data_checkpoint, g_wifi.wifi_data_in,
        (unsigned long)g_wifi.data_actual, g_wifi.data_packet_length,
        g_wifi.data_frame_control, g_wifi.data_ethertype,
        (unsigned long)g_wifi.data_frames,
        (unsigned long)g_wifi.data_eapol_frames,
        g_wifi.data_source[0], g_wifi.data_source[1],
        g_wifi.data_source[2], g_wifi.data_source[3],
        g_wifi.data_source[4], g_wifi.data_source[5],
        g_wifi.data_destination[0], g_wifi.data_destination[1],
        g_wifi.data_destination[2], g_wifi.data_destination[3],
        g_wifi.data_destination[4], g_wifi.data_destination[5],
        (unsigned long)g_wifi.data_arp_frames,
        (unsigned long)g_wifi.data_arp_requests,
        (unsigned long)g_wifi.data_arp_replies,
        g_wifi.data_arp_operation,
        ((uint8_t *)&g_wifi.data_arp_sender_ip)[0],
        ((uint8_t *)&g_wifi.data_arp_sender_ip)[1],
        ((uint8_t *)&g_wifi.data_arp_sender_ip)[2],
        ((uint8_t *)&g_wifi.data_arp_sender_ip)[3],
        ((uint8_t *)&g_wifi.data_arp_target_ip)[0],
        ((uint8_t *)&g_wifi.data_arp_target_ip)[1],
        ((uint8_t *)&g_wifi.data_arp_target_ip)[2],
        ((uint8_t *)&g_wifi.data_arp_target_ip)[3],
        (unsigned long)g_wifi.data_tx_arp_frames,
        (unsigned long)g_wifi.data_ipv4_frames,
        (unsigned long)g_wifi.data_tcp_frames,
        (unsigned long)g_wifi.data_tcp_syn_frames,
        ((uint8_t *)&g_wifi.data_ipv4_source)[0],
        ((uint8_t *)&g_wifi.data_ipv4_source)[1],
        ((uint8_t *)&g_wifi.data_ipv4_source)[2],
        ((uint8_t *)&g_wifi.data_ipv4_source)[3],
        ((uint8_t *)&g_wifi.data_ipv4_destination)[0],
        ((uint8_t *)&g_wifi.data_ipv4_destination)[1],
        ((uint8_t *)&g_wifi.data_ipv4_destination)[2],
        ((uint8_t *)&g_wifi.data_ipv4_destination)[3],
        g_wifi.data_tcp_source_port, g_wifi.data_tcp_destination_port,
        g_wifi.data_tx_checkpoint, g_wifi.wifi_data_out,
        (unsigned long)g_wifi.data_tx_actual,
        (unsigned long)g_wifi.data_tx_frames,
        g_wifi.wpa_checkpoint, g_wifi.wpa_state,
        (unsigned long)g_wifi.wpa_m1, (unsigned long)g_wifi.wpa_m3,
        (unsigned long)g_wifi.wpa_mic_failures,
        (unsigned long)g_wifi.wpa_replays,
        g_wifi.wpa_pairwise_installed, g_wifi.wpa_group_installed,
        g_wifi.wpa_port_open, g_wifi.wpa_assoc_ie_length,
        g_wifi.wpa_group_cipher == 1 ? "TKIP" :
          (g_wifi.wpa_group_cipher == 2 ? "CCMP" : "none"),
        g_wifi.wpa_group_key_length,
        g_wifi.wpa_diag_available, g_wifi.wpa_diag_version,
        g_wifi.wpa_diag_type, g_wifi.wpa_diag_body_length,
        g_wifi.wpa_diag_descriptor, g_wifi.wpa_diag_key_info,
        g_wifi.wpa_diag_key_data_length, g_wifi.wpa_diag_reports,
        g_wifi.station_vif == OK ? A733_WIFI_READY_SCOPE :
          "firmware start/re-enumeration pending; netdev and HCI pending");

      if (written > 0)
        {
          length += (size_t)written < sizeof(g_wifi_report) - length ?
                    (size_t)written : sizeof(g_wifi_report) - length - 1;
        }
    }

#ifdef CONFIG_NETUTILS_DHCPC
  if (length < sizeof(g_wifi_report) - 1)
    {
      uint32_t ip = ntohl(g_wifi.dhcp_address);
      uint32_t mask = ntohl(g_wifi.dhcp_netmask);
      uint32_t router = ntohl(g_wifi.dhcp_router);
      uint32_t dns = ntohl(g_wifi.dhcp_dns);
      int written = snprintf(report + length,
                             sizeof(g_wifi_report) - length,
        "dhcp: checkpoint=%d pid=%d address=%u.%u.%u.%u "
        "mask=%u.%u.%u.%u router=%u.%u.%u.%u dns=%u.%u.%u.%u "
        "lease=%lu garp=%lu/%d control='echo dhcp > /dev/a733-wifi'\n",
        g_wifi.dhcp_checkpoint, g_wifi.dhcp_pid,
        (ip >> 24) & 0xff, (ip >> 16) & 0xff,
        (ip >> 8) & 0xff, ip & 0xff,
        (mask >> 24) & 0xff, (mask >> 16) & 0xff,
        (mask >> 8) & 0xff, mask & 0xff,
        (router >> 24) & 0xff, (router >> 16) & 0xff,
        (router >> 8) & 0xff, router & 0xff,
        (dns >> 24) & 0xff, (dns >> 16) & 0xff,
        (dns >> 8) & 0xff, dns & 0xff,
        (unsigned long)g_wifi.dhcp_lease,
        (unsigned long)g_wifi.gratuitous_arp_sent,
        g_wifi.gratuitous_arp_checkpoint);

      if (written > 0)
        {
          length += (size_t)written < sizeof(g_wifi_report) - length ?
                    (size_t)written : sizeof(g_wifi_report) - length - 1;
        }
    }
#endif

  for (index = 0; index < g_wifi.scan_count &&
                  length < sizeof(g_wifi_report) - 1; index++)
    {
      struct a733_wifi_scan_s *result = &g_wifi.scan[index];
      int written = snprintf(report + length,
        sizeof(g_wifi_report) - length,
        "bss[%u]: %02x:%02x:%02x:%02x:%02x:%02x freq=%u rssi=%d "
        "security=%s assoc-ie=%u ssid='%s'\n",
        index, result->bssid[0], result->bssid[1], result->bssid[2],
        result->bssid[3], result->bssid[4], result->bssid[5],
        result->frequency, result->rssi,
        result->assoc_ie_length > 0 ? "WPA/RSN" :
          (result->privacy ? "privacy-legacy" : "open"),
        result->assoc_ie_length, result->ssid);

      if (written <= 0)
        {
          break;
        }

      length += (size_t)written < sizeof(g_wifi_report) - length ?
                (size_t)written : sizeof(g_wifi_report) - length - 1;
    }

  if ((size_t)filep->f_pos >= length)
    {
      return 0;
    }

  copy = length - (size_t)filep->f_pos;
  if (copy > buflen)
    {
      copy = buflen;
    }

  memcpy(buffer, report + filep->f_pos, copy);
  filep->f_pos += copy;
  return (ssize_t)copy;
}

static ssize_t a733_wifi_write(struct file *filep, const char *buffer,
                               size_t buflen)
{
  char command[96];
  size_t length = buflen;

  if (length >= sizeof(command))
    {
      length = sizeof(command) - 1;
    }

  memcpy(command, buffer, length);
  command[length] = '\0';
  while (length > 0 && (command[length - 1] == '\n' ||
                        command[length - 1] == '\r' ||
                        command[length - 1] == ' '))
    {
      command[--length] = '\0';
    }

  if (strcmp(command, "scan") == 0 || strcmp(command, "scanall") == 0 ||
      strcmp(command, "scan2") == 0 || strcmp(command, "scan5") == 0 ||
      strcmp(command, "scandfs") == 0)
    {
      bool scan_2g = false;
      bool scan_5g = false;
      bool scan_dfs = false;
      const char *name;

      if (strcmp(command, "scan2") == 0)
        {
          scan_2g = true;
          name = "2.4 GHz";
        }
      else if (strcmp(command, "scan5") == 0)
        {
          scan_5g = true;
          name = "5 GHz non-DFS split";
        }
      else if (strcmp(command, "scandfs") == 0)
        {
          scan_dfs = true;
          name = "5 GHz DFS";
        }
      else
        {
          scan_2g = true;
          scan_5g = true;
          name = "2.4/5 GHz split";
        }

      g_wifi.scan_checkpoint = a733_wifi_scan_group(scan_2g, scan_5g,
                                                     scan_dfs);
      syslog(g_wifi.scan_checkpoint < 0 ? LOG_WARNING : LOG_INFO,
             "A733 WIFI: %s scan checkpoint (%d), cached-results=%u\n",
             name, g_wifi.scan_checkpoint, g_wifi.scan_count);
      return g_wifi.scan_checkpoint < 0 ? g_wifi.scan_checkpoint :
             (ssize_t)buflen;
    }

#ifdef CONFIG_NETUTILS_DHCPC
  if (strcmp(command, "dhcp") == 0)
    {
      int ret = a733_wifi_dhcp_start();

      return ret < 0 ? ret : (ssize_t)buflen;
    }
#endif

  if (strncmp(command, "datacheck=", 10) == 0)
    {
      char *end;
      unsigned long result_index;

      errno = 0;
      result_index = strtoul(command + 10, &end, 10);
      if (errno != 0 || end == command + 10 || *end != '\0' ||
          result_index >= A733_WIFI_SCAN_MAX)
        {
          return -EINVAL;
        }

      g_wifi.data_checkpoint = -EAGAIN;
      g_wifi.association_checkpoint =
        a733_wifi_associate((unsigned int)result_index);
      if (g_wifi.association_checkpoint < 0)
        {
          return g_wifi.association_checkpoint;
        }

      g_wifi.data_checkpoint = a733_wifi_data_checkpoint();
      syslog(g_wifi.data_checkpoint < 0 ? LOG_WARNING : LOG_INFO,
             "A733 WIFI: WLAN data checkpoint (%d), frames=%lu "
             "eapol=%lu ethertype=%04x\n",
             g_wifi.data_checkpoint,
             (unsigned long)g_wifi.data_frames,
             (unsigned long)g_wifi.data_eapol_frames,
             g_wifi.data_ethertype);
      return g_wifi.data_checkpoint < 0 ? g_wifi.data_checkpoint :
             (ssize_t)buflen;
    }

  if (strncmp(command, "txcheck=", 8) == 0)
    {
      char *end;
      unsigned long result_index;

      errno = 0;
      result_index = strtoul(command + 8, &end, 10);
      if (errno != 0 || end == command + 8 || *end != '\0' ||
          result_index >= A733_WIFI_SCAN_MAX)
        {
          return -EINVAL;
        }

      g_wifi.data_checkpoint = -EAGAIN;
      g_wifi.data_tx_checkpoint = -EAGAIN;
      g_wifi.association_checkpoint =
        a733_wifi_associate((unsigned int)result_index);
      if (g_wifi.association_checkpoint < 0)
        {
          return g_wifi.association_checkpoint;
        }

      g_wifi.data_checkpoint = a733_wifi_data_checkpoint();
      if (g_wifi.data_checkpoint < 0)
        {
          return g_wifi.data_checkpoint;
        }

      g_wifi.data_tx_checkpoint = a733_wifi_data_tx_checkpoint();
      syslog(g_wifi.data_tx_checkpoint < 0 ? LOG_WARNING : LOG_INFO,
             "A733 WIFI: WLAN TX checkpoint (%d), frames=%lu "
             "actual=%lu\n", g_wifi.data_tx_checkpoint,
             (unsigned long)g_wifi.data_tx_frames,
             (unsigned long)g_wifi.data_tx_actual);
      return g_wifi.data_tx_checkpoint < 0 ? g_wifi.data_tx_checkpoint :
             (ssize_t)buflen;
    }

  if (strncmp(command, "associate=", 10) == 0)
    {
      char *end;
      unsigned long result_index;

      errno = 0;
      result_index = strtoul(command + 10, &end, 10);
      if (errno != 0 || end == command + 10 || *end != '\0' ||
          result_index >= A733_WIFI_SCAN_MAX)
        {
          return -EINVAL;
        }

      g_wifi.association_checkpoint =
        a733_wifi_associate((unsigned int)result_index);
      syslog(g_wifi.association_checkpoint < 0 ? LOG_WARNING : LOG_INFO,
             "A733 WIFI: association checkpoint (%d), cfm=%u status=%u\n",
             g_wifi.association_checkpoint,
             g_wifi.association_cfm_status,
             g_wifi.association_status_code);
      return g_wifi.association_checkpoint < 0 ?
             g_wifi.association_checkpoint : (ssize_t)buflen;
    }

  if (strncmp(command, "wpa2=", 5) == 0)
    {
      char *separator;
      char *end;
      unsigned long result_index;
      int ret;

      separator = strchr(command + 5, ',');
      if (separator == NULL)
        {
          explicit_bzero(command, sizeof(command));
          return -EINVAL;
        }

      *separator++ = '\0';
      errno = 0;
      result_index = strtoul(command + 5, &end, 10);
      if (errno != 0 || end == command + 5 || *end != '\0' ||
          result_index >= g_wifi.scan_count || strlen(separator) < 8 ||
          strlen(separator) > 63 ||
          g_wifi.scan[result_index].assoc_ie_length == 0)
        {
          explicit_bzero(command, sizeof(command));
          return -EINVAL;
        }

      explicit_bzero(g_wifi.wpa_pmk, sizeof(g_wifi.wpa_pmk));
      explicit_bzero(g_wifi.wpa_ptk, sizeof(g_wifi.wpa_ptk));
      explicit_bzero(g_wifi.wpa_snonce, sizeof(g_wifi.wpa_snonce));
      explicit_bzero(g_wifi.wpa_assoc_ie, sizeof(g_wifi.wpa_assoc_ie));
      g_wifi.wpa_assoc_ie_length = 0;
      g_wifi.wpa_group_cipher = 0xff;
      g_wifi.wpa_group_key_length = 0;
      ret = a733_wifi_wpa_select_rsn(
              g_wifi.scan[result_index].assoc_ie,
              g_wifi.scan[result_index].assoc_ie_length,
              g_wifi.wpa_assoc_ie, &g_wifi.wpa_assoc_ie_length);
      if (ret < 0)
        {
          syslog(LOG_WARNING,
                 "A733 WIFI: BSS does not offer WPA2-PSK/CCMP (%d); "
                 "SAE-only networks are not supported yet\n", ret);
          explicit_bzero(separator, strlen(separator));
          explicit_bzero(command, sizeof(command));
          return ret;
        }

      ret = a733_wifi_wpa_derive_pmk(separator,
                                     g_wifi.scan[result_index].ssid,
                                     g_wifi.wpa_pmk);
      explicit_bzero(separator, strlen(separator));
      if (ret < 0)
        {
          explicit_bzero(command, sizeof(command));
          return ret;
        }

      g_wifi.wpa_configured = true;
      g_wifi.wpa_state = A733_WPA_WAIT_M1;
      g_wifi.wpa_checkpoint = -EINPROGRESS;
      g_wifi.wpa_pairwise_installed = false;
      g_wifi.wpa_group_installed = false;
      g_wifi.wpa_port_open = false;
#ifdef CONFIG_NETUTILS_DHCPC
      g_wifi.dhcp_checkpoint = -ENETDOWN;
      g_wifi.dhcp_address = 0;
      g_wifi.dhcp_netmask = 0;
      g_wifi.dhcp_router = 0;
      g_wifi.dhcp_dns = 0;
      g_wifi.dhcp_lease = 0;
#endif
      g_wifi.wpa_diag_reports = 0;
      g_wifi.wpa_diag_available = 0;
      g_wifi.wpa_diag_body_length = 0;
      g_wifi.wpa_diag_key_info = 0;
      g_wifi.wpa_diag_key_data_length = 0;
      g_wifi.wpa_diag_version = 0;
      g_wifi.wpa_diag_type = 0;
      g_wifi.wpa_diag_descriptor = 0;
      memset(g_wifi.wpa_replay, 0, sizeof(g_wifi.wpa_replay));
      g_wifi.association_checkpoint =
        a733_wifi_associate((unsigned int)result_index);
      if (g_wifi.association_checkpoint == OK)
        {
          ret = a733_wifi_data_send_eapol_start();
        }
      else
        {
          ret = g_wifi.association_checkpoint;
        }

      if (ret < 0)
        {
          g_wifi.wpa_checkpoint = ret;
          g_wifi.wpa_configured = false;
          explicit_bzero(g_wifi.wpa_pmk, sizeof(g_wifi.wpa_pmk));
        }
      else
        {
          syslog(LOG_INFO,
                 "A733 WIFI: WPA2-PSK started ssid='%s'; waiting for "
                 "four-way handshake\n", g_wifi.associated_ssid);
        }

      explicit_bzero(command, sizeof(command));
      return ret < 0 ? ret : (ssize_t)buflen;
    }

  if (strcmp(command, "disconnect") == 0)
    {
      g_wifi.disconnect_checkpoint = a733_wifi_disconnect();
      g_wifi.wpa_configured = false;
      g_wifi.wpa_state = 0;
      g_wifi.wpa_port_open = false;
      explicit_bzero(g_wifi.wpa_pmk, sizeof(g_wifi.wpa_pmk));
      explicit_bzero(g_wifi.wpa_ptk, sizeof(g_wifi.wpa_ptk));
#ifdef CONFIG_NETUTILS_DHCPC
      a733_wifi_ipv4_clear();
#endif

      syslog(g_wifi.disconnect_checkpoint < 0 ? LOG_WARNING : LOG_INFO,
             "A733 WIFI: disconnect checkpoint (%d), cfm=%u ind=%u "
             "reason=%u\n", g_wifi.disconnect_checkpoint,
             g_wifi.disconnect_confirmed, g_wifi.disconnect_indicated,
             g_wifi.disconnect_reason);
      return g_wifi.disconnect_checkpoint < 0 ?
             g_wifi.disconnect_checkpoint : (ssize_t)buflen;
    }

  return -EINVAL;
}

static const struct file_operations g_a733_wifi_fops =
{
  .read = a733_wifi_read,
  .write = a733_wifi_write,
};

int a733_wifi_usb_initialize(void)
{
  int driver_ret;
  int ret;

  g_wifi.protocol = -EAGAIN;
  g_wifi.firmware = -EAGAIN;
  g_wifi.runtime_transport = -EAGAIN;
  g_wifi.stack_start = -EAGAIN;
  g_wifi.rf_config = -EAGAIN;
  g_wifi.me_config = -EAGAIN;
  g_wifi.channel_config = -EAGAIN;
  g_wifi.station_vif = -EAGAIN;
  g_wifi.mac_start = -EAGAIN;
  g_wifi.scan_checkpoint = -EAGAIN;
  g_wifi.association_checkpoint = -EAGAIN;
  g_wifi.association_cfm_status = 0xff;
  g_wifi.association_status_code = 0xffff;
  g_wifi.association_vif = 0xff;
  g_wifi.association_ap = 0xff;
  g_wifi.association_channel = 0xff;
  g_wifi.disconnect_checkpoint = -EAGAIN;
  g_wifi.disconnect_reason = 0xffff;
  g_wifi.data_checkpoint = -EAGAIN;
  g_wifi.data_tx_checkpoint = -EAGAIN;
  g_wifi.wext_wpa_version = IW_AUTH_WPA_VERSION_WPA2;
  g_wifi.wext_pairwise_cipher = IW_AUTH_CIPHER_CCMP;
#ifdef CONFIG_NETUTILS_DHCPC
  g_wifi.dhcp_checkpoint = -EAGAIN;
  g_wifi.dhcp_pid = -1;
#endif
  a733_wifi_power_enable();
  a733_usb1_clock_enable();
  ret = a733_ehci1_checkpoint();
  g_wifi.checkpoint = ret;
  a733_wifi_snapshot();

  syslog(ret < 0 ? LOG_WARNING : LOG_INFO,
         "A733 WIFI: FCU760K USB1 root port %s (%d), version=%04x "
         "port=%08lx\n",
         ret < 0 ? "checkpoint pending" : "connected at high speed",
         ret, g_wifi.hciversion, (unsigned long)g_wifi.portsc);

  if (ret == OK)
    {
      g_wifi.enumeration = a733_wifi_enumerate();
      if (g_wifi.enumeration < 0)
        {
          syslog(LOG_WARNING,
                 "A733 WIFI: USB descriptor checkpoint pending (%d)\n",
                 g_wifi.enumeration);
        }
      else
        {
          g_wifi.probe_address = A733_AIC_CHIP_ID_ADDR;
          g_wifi.protocol = a733_aic_bootrom_read32(g_wifi.probe_address,
                                                    &g_wifi.probe_value);
          syslog(g_wifi.protocol < 0 ? LOG_WARNING : LOG_INFO,
                 "A733 WIFI: AIC BootROM read32 [%08lx]=%08lx "
                 "bulk=%lu/%lu response=%04x (%d)\n",
                 (unsigned long)g_wifi.probe_address,
                 (unsigned long)g_wifi.probe_value,
                 (unsigned long)g_wifi.command_actual,
                 (unsigned long)g_wifi.response_actual,
                 g_wifi.response_id, g_wifi.protocol);

          if (g_wifi.protocol == OK)
            {
              g_wifi.firmware = a733_aic_load_firmware();
              if (g_wifi.firmware == OK)
                {
                  /* START_APP disconnects PID 8d80 and exposes the official
                   * D80 runtime composite device as PID 8d81.  Restart the
                   * polling host so address zero and DATA toggles are clean.
                   */

                  a733_delay_ms(300);
                  g_wifi.firmware = a733_ehci1_checkpoint();
                  if (g_wifi.firmware == OK)
                    {
                      g_wifi.firmware = a733_wifi_enumerate();
                    }

                  g_wifi.runtime_vid = g_wifi.vid;
                  g_wifi.runtime_pid = g_wifi.pid;
                  if (g_wifi.firmware == OK &&
                      (g_wifi.runtime_vid != A733_AIC_VID ||
                       g_wifi.runtime_pid != A733_AIC_RUNTIME_PID))
                    {
                      g_wifi.firmware = -EPROTO;
                    }

                  if (g_wifi.firmware == OK)
                    {
                      g_wifi.runtime_transport = a733_wifi_runtime_map();
                      if (g_wifi.runtime_transport == OK)
                        {
                          g_wifi.runtime_transport =
                            a733_wifi_runtime_version();
                        }

                      if (g_wifi.runtime_transport == OK)
                        {
                          g_wifi.stack_start = a733_wifi_stack_start();
                          syslog(g_wifi.stack_start < 0 ? LOG_WARNING :
                                 LOG_INFO,
                                 "A733 WIFI: LMAC stack checkpoint (%d)\n",
                                 g_wifi.stack_start);

                          if (g_wifi.stack_start == OK)
                            {
                              g_wifi.rf_config = a733_wifi_rf_config();
                              syslog(g_wifi.rf_config < 0 ? LOG_WARNING :
                                     LOG_INFO,
                                     "A733 WIFI: RF configuration "
                                     "checkpoint (%d)\n",
                                     g_wifi.rf_config);

                              if (g_wifi.rf_config == OK)
                                {
                                  g_wifi.me_config = a733_wifi_me_config();
                                  syslog(g_wifi.me_config < 0 ? LOG_WARNING :
                                         LOG_INFO,
                                         "A733 WIFI: ME configuration "
                                         "checkpoint (%d)\n",
                                         g_wifi.me_config);

                                  if (g_wifi.me_config == OK)
                                    {
                                      g_wifi.channel_config =
                                        a733_wifi_channel_config();
                                      syslog(g_wifi.channel_config < 0 ?
                                             LOG_WARNING : LOG_INFO,
                                             "A733 WIFI: regulatory channel "
                                             "checkpoint (%d)\n",
                                             g_wifi.channel_config);

                                      if (g_wifi.channel_config == OK)
                                        {
                                          g_wifi.station_vif =
                                            a733_wifi_station_vif();
                                          syslog(g_wifi.station_vif < 0 ?
                                                 LOG_WARNING : LOG_INFO,
                                                 "A733 WIFI: station VIF "
                                                 "checkpoint (%d)\n",
                                                 g_wifi.station_vif);

                                          if (g_wifi.station_vif == OK)
                                            {
                                              g_wifi.mac_start =
                                                a733_wifi_mac_start();
                                              syslog(g_wifi.mac_start < 0 ?
                                                     LOG_WARNING : LOG_INFO,
                                                     "A733 WIFI: MAC/PHY "
                                                     "start checkpoint "
                                                     "(%d)\n",
                                                     g_wifi.mac_start);
                                            }
                                        }
                                    }
                                }
                            }
                        }

                      syslog(g_wifi.runtime_transport < 0 ? LOG_WARNING :
                             LOG_INFO,
                             "A733 WIFI: runtime transport checkpoint (%d)\n",
                             g_wifi.runtime_transport);
                    }
                }

              syslog(g_wifi.firmware < 0 ? LOG_WARNING : LOG_INFO,
                     "A733 WIFI: D80 firmware bytes=%lu runtime=%04x:%04x "
                     "(%d)\n", (unsigned long)g_wifi.firmware_bytes,
                     g_wifi.runtime_vid, g_wifi.runtime_pid,
                     g_wifi.firmware);
            }
        }
    }
  else
    {
      g_wifi.enumeration = ret;
    }

  /* The diagnostic node is useful even when the module is absent or still
   * settling, so hardware checkpoint failure is intentionally non-fatal.
   */

  driver_ret = register_driver("/dev/a733-wifi", &g_a733_wifi_fops,
                               0666, NULL);
#ifdef CONFIG_NET
  if (driver_ret == OK && g_wifi.mac_start == OK)
    {
      ret = a733_wifi_netdev_register();
      if (ret < 0)
        {
          syslog(LOG_WARNING,
                 "A733 WIFI: wlan0 registration pending (%d)\n", ret);
        }
    }
#endif

  return driver_ret;
}

#endif
