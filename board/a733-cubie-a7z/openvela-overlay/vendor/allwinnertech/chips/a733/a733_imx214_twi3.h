/* SPDX-License-Identifier: Apache-2.0 */
#ifndef A733_IMX214_TWI3_H
#define A733_IMX214_TWI3_H
#include "a733_imx214_probe.h"

/* Control-plane provider for an ALREADY qualified/owned V1.11/J5 camera.
 * No rail/clock/pin configuration or reset. Identify is read-only; v187
 * adds explicit, identity-gated sensor mode/stream transaction callbacks.
 * This is not a cold-start driver. Nothing in the passive inventory or
 * user CLI constructs a power/clock qualification record for this API.
 */
struct a733_imx214_twi3
{
  int (*read32)(void *, uint32_t address, uint32_t *value);
  int (*write32)(void *, uint32_t address, uint32_t value);
  uint32_t (*now_ms)(void *);
  void *context;
  bool board_power_qualified;
  bool exclusive_sensor_and_bus;
  uint32_t measured_mclk_hz;
  bool quarantined; /* Latch; caller must not clear to retry a failed bus. */
  bool identity_verified; /* Set only by successful strict identify. */
  bool trial_attempted; /* Separate one-shot ID experiment; NOT qualification. */
  bool trial_identity_match; /* Only permits standby configuration, not stream. */
  unsigned polls;
  bool packet_stream_attempted;
};

/* Caller holds a non-blocking exclusive board/sensor/TWI3 ownership lock
 * throughout. Its register callbacks must themselves be bounded and use
 * the existing A733 Device mappings, not a guessed virtual/physical map.
 * Wrong/missing qualification performs ZERO MMIO calls.
 */
int a733_imx214_twi3_identify(struct a733_imx214_probe *out,
                             struct a733_imx214_twi3 *bus);

/* v193: one fixed 0x10/0x0016 two-byte read on an owned, already released
 * sensor. Caller explicitly assumes firmware-retained rails and nominal
 * sys24M; this NEVER supplies measured_mclk_hz/board_power_qualified or
 * identity_verified, and cannot unlock mode/write APIs. It is an experiment,
 * not a replacement for the strict provider contract. No retry/reset/scan.
 */
int a733_imx214_twi3_trial_id(struct a733_imx214_probe *out,
                             struct a733_imx214_twi3 *bus);
int a733_imx214_twi3_trial_read(void *,uint16_t,uint8_t *,size_t,unsigned);
int a733_imx214_twi3_trial_write8(void *,uint16_t,uint8_t,unsigned);
/* Separate packet-only source control. ON requires a receiver callback
 * which checks the live owned configuration, not a CLI ready flag. OFF is
 * allowed for cleanup after a partial ON error, but never after quarantine.
 * The generic standby trial write API continues to reject STREAM_ON.
 */
int a733_imx214_twi3_packet_stream(struct a733_imx214_twi3 *,
  int (*receiver_check)(void *),void *receiver,bool on);

/* Mode-session callbacks after strict identify under the SAME ownership
 * lifetime. Recheck power/clock/pins each transaction. No soft reset, OTP
 * or identity writes. STREAM_ON remains the mode engine's separate step.
 * Void context signatures match a733_imx214_mode_io directly.
 */
int a733_imx214_twi3_read_bytes(void *, uint16_t, uint8_t *, size_t, unsigned);
int a733_imx214_twi3_write8(void *, uint16_t, uint8_t, unsigned);
#endif
