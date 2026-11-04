/* SPDX-License-Identifier: Apache-2.0 */
#ifndef A733_MIPI_RX_H
#define A733_MIPI_RX_H
#include <stdbool.h>
#include <stdint.h>
struct a733_imx214_cold_io;
struct a733_mipi_fault
{
  uint32_t address,mask,expected,observed;
  int error;
};
/* V1.11 MCSIB packet-only experiment. No parser capture, DMA, IOMMU or
 * memory address registers. Snapshots/configuration are NOT received frames.
 * Exclusive camera broker must enclose prepare -> sensor ON -> observe ->
 * sensor OFF -> restore. Failed restoration retains ownership until reboot.
 */
struct a733_mipi_rx
{
  const struct a733_imx214_cold_io *io;
  uint32_t original[28];
  unsigned saved, changed, reads, writes, polls, raw_headers, fs, fe;
  uint32_t status, errors, header, phy_debug, phy_frequency;
  uint32_t pll_ref, pll_peri, started, elapsed, last_address;
  int error, cleanup_error;
  bool attempted, owned, ready, cleaning;
  const char *stage;
  struct a733_mipi_fault first_fault,cleanup_fault;
};
int a733_mipi_rx_prepare(struct a733_mipi_rx *, const struct a733_imx214_cold_io *);
int a733_mipi_rx_check(void *);
int a733_mipi_rx_observe(struct a733_mipi_rx *);
int a733_mipi_rx_snapshot(struct a733_mipi_rx *);
int a733_mipi_rx_finish(struct a733_mipi_rx *);
/* Adapter allowlist: masks are fixed; status is W1C, never RMW/restored. */
uint32_t a733_mipi_rx_write_mask(uint32_t);
/* Hardware-produced calibration results/flags/counters: never restored,
 * compared as stable config, or echoed back as ones by masked RMW. */
uint32_t a733_mipi_rx_volatile_mask(uint32_t);
bool a733_mipi_rx_readable(uint32_t);
#endif
