/* SPDX-License-Identifier: Apache-2.0 */
#include "a733_imx214_probe.h"
#include <errno.h>
#include <string.h>

/* Register facts checked against current upstream Linux IMX214 driver.
 * Independently written protocol logic: no Linux code/mode tables copied. */
#define IMX214_ADDRESS 0x10
#define IMX214_ID 0x0214
#define PROBE_BUDGET_MS 500u
#define TRANSFER_LIMIT_MS 100u

static uint16_t be16(const uint8_t *data)
{
  return (uint16_t)((unsigned)data[0] << 8 | data[1]);
}

static int read_once(struct a733_imx214_probe *out,
                     const struct a733_imx214_io *io, void *context,
                     uint32_t start, uint16_t reg, uint8_t *data, size_t count)
{
  uint32_t elapsed = io->now_ms(context) - start;
  if (elapsed >= PROBE_BUDGET_MS)
    return -ETIMEDOUT;
  unsigned remaining = PROBE_BUDGET_MS - elapsed;
  if (remaining > TRANSFER_LIMIT_MS)
    remaining = TRANSFER_LIMIT_MS;
  ++out->reads;
  uint32_t transfer_start = io->now_ms(context);
  int ret = io->read(context, IMX214_ADDRESS, reg, data, count, remaining);
  if (ret > 0)
    return -EPROTO;
  if (ret)
    return ret;
  uint32_t end = io->now_ms(context);
  if (end - transfer_start > remaining || end - start >= PROBE_BUDGET_MS)
    return -ETIMEDOUT;
  return 0;
}

int a733_imx214_identify(struct a733_imx214_probe *out,
                        const struct a733_imx214_io *io, void *context)
{
  if (!out)
    return -EINVAL;
  memset(out, 0, sizeof(*out));
  out->stage = "board-power-clock-unqualified";
  out->error = -EACCES;
  if (!io || !io->read || !io->now_ms)
    return out->error = -EINVAL;
  if ((io->profile != A733_CAMERA_A7Z_V11_SINGLE &&
       io->profile != A733_CAMERA_A7Z_CAM2) || !io->board_power_qualified ||
      io->measured_mclk_hz != 24000000)
    return out->error;

  uint32_t start = io->now_ms(context);
  uint8_t data[4];
  out->stage = "read-chip-id-0016";
  int ret = read_once(out, io, context, start, 0x0016, data, 2);
  if (!ret)
    {
      out->chip_id = be16(data);
      if (out->chip_id != IMX214_ID)
        {
          out->stage = "chip-id-mismatch";
          ret = -ENODEV;
        }
      else
        out->identity_match = true;
    }
  if (!ret)
    {
      out->stage = "read-stream-state";
      ret = read_once(out, io, context, start, 0x0100, data, 1);
      if (!ret)
        out->stream_mode = data[0];
    }
  if (!ret)
    {
      out->stage = "read-wire-format";
      ret = read_once(out, io, context, start, 0x0112, data, 2);
      if (!ret)
        out->wire_format = be16(data);
    }
  if (!ret)
    {
      out->stage = "read-lane-mode";
      ret = read_once(out, io, context, start, 0x0114, data, 1);
      if (!ret)
        out->lane_mode = data[0];
    }
  if (!ret)
    {
      out->stage = "read-output-dimensions";
      ret = read_once(out, io, context, start, 0x034c, data, 4);
      if (!ret)
        {
          out->width = be16(data);
          out->height = be16(data + 2);
          out->snapshot_complete = true;
          out->stage = "sensor-identified-NOT-camera-capture";
        }
    }
  out->elapsed_ms = io->now_ms(context) - start;
  out->error = ret;
  return ret;
}
