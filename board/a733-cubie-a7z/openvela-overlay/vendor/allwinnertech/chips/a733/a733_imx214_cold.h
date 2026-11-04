/* SPDX-License-Identifier: Apache-2.0 */
#ifndef A733_IMX214_COLD_H
#define A733_IMX214_COLD_H
#include <stdbool.h>
#include <stdint.h>
#include "a733_imx214_probe.h"
#include "a733_imx214_mode.h"

/* V1.11/J5 CONTROL preparation on firmware-powered rails, NOT a complete
 * cold PMIC/CSI driver. Provider must hold exclusive TWI3/MCLK1 ownership
 * and the shared whole-PE-bank RMW lock (not only five independent pin
 * locks), and serialize PMIC reads with its real power-bus owner.
 * No CLI booleans, stale files or historical U-Boot readings may implement
 * read_pmic8. A live provider is deliberately NOT installed by this core;
 * the separate v192 adapter uses an explicitly assumed firmware-retention
 * profile from a newly loaded Image's same-boot handoff, not a live sample.
 */
struct a733_imx214_twi3;
struct a733_imx214_cold_io
{
  int (*read_pmic8)(void *, uint8_t reg, uint8_t *, unsigned timeout_ms);
  int (*read32)(void *, uint32_t, uint32_t *, unsigned timeout_ms);
  int (*write32)(void *, uint32_t, uint32_t, unsigned timeout_ms);
  int (*delay_ms)(void *, unsigned);
  uint32_t (*now_ms)(void *);
  void *context;
  /* Alternative to read_pmic8: six immutable bytes from the image-bound,
   * same-boot U-Boot producer. Never a CLI flag/file or a substitute for a
   * live reading. Provider retains rails as a declared firmware assumption.
   * Exactly ONE power source must be installed.
   */
  const uint8_t *firmware_power;
  /* Optional explicit packet-only experiment, invoked ONLY after a fresh
   * ID and all pinned mode readbacks. Not used by the old ID/mode commands.
   * Must stop its source and restore the receiver before returning.
   */
  int (*packet_trial)(void *,struct a733_imx214_twi3 *);
};

enum a733_imx214_cold_state
{
  A733_IMX214_COLD_NEW = 0,
  A733_IMX214_COLD_PREPARED,
  A733_IMX214_COLD_PARKED,
  A733_IMX214_COLD_FAULT
};

struct a733_imx214_cold
{
  enum a733_imx214_cold_state state;
  const char *stage;
  int error, cleanup_error;
  uint32_t original[6]; /* APB1, PE cfg0/cfg1/data, MCLK1, TWI3 BGR */
  uint8_t power_before[6], power_after[6];
  unsigned reads, writes, polls;
  uint32_t started_ms, elapsed_ms, last_address;
  uint32_t configured_mclk_hz; /* Nominal register plan, NOT a measurement. */
  bool snapshot_complete, controls_owned, dirty, twi_accessible, cleaning;
  bool firmware_power_assumed; /* NOT live resampling or physical measurement. */
  bool id_trial_attempted, bus_quarantined;
  struct a733_imx214_probe id_trial;
  unsigned id_polls;
  uint32_t budget_ms;
  bool mode_trial_attempted;
  struct a733_imx214_session mode_trial;
};

/* Initial power reads precede MMIO. Require ID=03/version=00, DCDC1 enabled
 * at configured 3300mV and BLDO1 enabled at configured 1800mV. These do not
 * measure physical rails or prove module presence/5V. No PMIC write API.
 * Prepare holds RESET/STBY LOW, programs sys24M /1 MCLK1 and 100kHz TWI3.
 * It does NOT release reset, issue sensor transfers, claim measured MCLK,
 * construct a legacy qualification token, stream, or access CSI/DMA/ISP.
 * Caller must park/release a successful session; no boot-time invocation.
 */
int a733_imx214_cold_prepare(struct a733_imx214_cold *,
                            const struct a733_imx214_cold_io *);
int a733_imx214_cold_park(struct a733_imx214_cold *,
                         const struct a733_imx214_cold_io *);
/* Fixed diagnostic clock choices only, RESET/STBY remain asserted.
 * 1MHz is measurable by a 24MHz logic analyser; neither choice is proof of
 * measured frequency or permission to release the sensor / issue ID reads.
 */
int a733_imx214_cold_clock(struct a733_imx214_cold *,
                          const struct a733_imx214_cold_io *,bool calibration);

/* Separate explicit v193 exploratory operation, not strict qualification:
 * require the owned PREPARED nominal-24M session, verify its register plan,
 * release PE8/9 HIGH, settle 30ms, read ONLY ID0016 at0x10 once, then park.
 * Polarity/settle facts follow the vendor IMX214 control sequence; retained
 * rails and physical clock remain ASSUMED. No mode/stream write permission.
 * Uncertain bus ownership/failed STOP: assert controls only, retain/quarantine
 * resources until reboot; do not reset/gate a possibly active TWI transfer.
 */
int a733_imx214_cold_trial_id(struct a733_imx214_cold *,
                             const struct a733_imx214_cold_io *);
/* Pinned artifact already SHA256-verified by adapter. Configure/read back
 * while in standby, then RESET/park; no receiver MMIO or STREAM_ON. */
int a733_imx214_cold_trial_mode(struct a733_imx214_cold *,
  const struct a733_imx214_cold_io *,const void *,size_t);
/* Separate bounded sensor-on session; old ID/mode gates keep their budgets.
 * Callback must stop source/DMA and restore receiver before returning. Each
 * transfer keeps its original deadline; caller also has a <=1h wall limit. */
int a733_imx214_cold_trial_session(struct a733_imx214_cold *,
  const struct a733_imx214_cold_io *,const void *,size_t,uint32_t);
#endif
