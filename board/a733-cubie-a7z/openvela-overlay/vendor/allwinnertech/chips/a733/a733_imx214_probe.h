/* SPDX-License-Identifier: Apache-2.0 */
#ifndef A733_IMX214_PROBE_H
#define A733_IMX214_PROBE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Single-connector V1.10/V1.11 is NOT the newer CAM2 wiring. No automatic
 * board revision selection, voltage programming or GPIO ownership implied. */
enum a733_camera_profile
{
  A733_CAMERA_UNQUALIFIED = 0,
  A733_CAMERA_A7Z_V11_SINGLE,
  A733_CAMERA_A7Z_CAM2
};

struct a733_imx214_io
{
  /* Provider holds the power/MCLK/bus ownership lock throughout this call.
   * It must verify the actual board profile and electrical rails first.
   * read performs ONE 16-bit big-endian subaddress + repeated-start read at
   * 7-bit address 0x10, bounded by timeout_ms. Zero or negative errno only.
   * No reset/retry, scanning, stream start, or register-value writes allowed. */
  int (*read)(void *context, uint8_t address, uint16_t reg, uint8_t *data,
              size_t count, unsigned timeout_ms);
  uint32_t (*now_ms)(void *context);
  enum a733_camera_profile profile;
  bool board_power_qualified;
  uint32_t measured_mclk_hz;
};

struct a733_imx214_probe
{
  int error;
  const char *stage;
  uint16_t chip_id;
  uint16_t width;
  uint16_t height;
  uint16_t wire_format;
  uint8_t lane_mode;
  uint8_t stream_mode;
  unsigned reads;
  uint32_t elapsed_ms;
  bool identity_match;
  bool snapshot_complete;
};

/* This is only sensor identity/read-only status, NOT CSI/VIN/ISP capture.
 * There is deliberately no production provider or /dev/video registration
 * until power/clock, sensor and DMA gates have independently passed. */
int a733_imx214_identify(struct a733_imx214_probe *out,
                        const struct a733_imx214_io *io, void *context);

#endif
