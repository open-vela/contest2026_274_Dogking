/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "a733_imx214_probe.h"

struct fixture
{
  unsigned calls;
  unsigned fail_at;
  unsigned delay;
  int injected;
  uint16_t id;
  uint32_t time;
};

static uint32_t now(void *opaque)
{
  return ((struct fixture *)opaque)->time;
}

static int read_reg(void *opaque, uint8_t address, uint16_t reg, uint8_t *data,
                    size_t count, unsigned timeout)
{
  struct fixture *f = opaque;
  static const uint16_t registers[] = {0x0016, 0x0100, 0x0112, 0x0114, 0x034c};
  static const size_t counts[] = {2, 1, 2, 1, 4};
  assert(address == 0x10 && f->calls < 5);
  assert(reg == registers[f->calls] && count == counts[f->calls]);
  assert(timeout > 0 && timeout <= 100);
  f->calls++;
  f->time += f->delay;
  if (f->calls == f->fail_at)
    return f->injected;
  memset(data, 0, count);
  if (reg == 0x0016)
    { data[0] = f->id >> 8; data[1] = f->id; }
  if (reg == 0x0112)
    { data[0] = 10; data[1] = 10; }
  if (reg == 0x0114)
    data[0] = 3;
  if (reg == 0x034c)
    { data[0] = 7; data[1] = 128; data[2] = 4; data[3] = 56; }
  return 0;
}

int main(void)
{
  struct a733_imx214_probe p;
  struct fixture f = {.id = 0x0214, .delay = 1};
  struct a733_imx214_io io = {read_reg, now, A733_CAMERA_UNQUALIFIED, false, 0};
  assert(a733_imx214_identify(NULL, &io, &f) == -EINVAL);
  assert(a733_imx214_identify(&p, NULL, &f) == -EINVAL);
  for (unsigned gate = 0; gate < 3; gate++)
    {
      io.profile = gate ? A733_CAMERA_A7Z_V11_SINGLE : A733_CAMERA_UNQUALIFIED;
      io.board_power_qualified = gate != 1;
      io.measured_mclk_hz = gate == 2 ? 26000000 : 24000000;
      assert(a733_imx214_identify(&p, &io, &f) == -EACCES && !f.calls);
    }
  io.board_power_qualified = true;
  io.measured_mclk_hz = 24000000;
  for (unsigned profile = A733_CAMERA_A7Z_V11_SINGLE; profile <= A733_CAMERA_A7Z_CAM2; profile++)
    {
      f = (struct fixture){.id = 0x0214, .delay = 1};
      io.profile = profile;
      assert(!a733_imx214_identify(&p, &io, &f));
      assert(p.identity_match && p.snapshot_complete && p.reads == 5);
      assert(p.width == 1920 && p.height == 1080 && p.wire_format == 0x0a0a);
      assert(p.lane_mode == 3 && !p.stream_mode && p.elapsed_ms == 5);
    }
  const uint16_t bad_ids[] = {0, 0xffff, 0x1402, 0x0138};
  for (unsigned i = 0; i < sizeof(bad_ids)/sizeof(bad_ids[0]); i++)
    {
      f = (struct fixture){.id = bad_ids[i], .delay = 1};
      assert(a733_imx214_identify(&p, &io, &f) == -ENODEV);
      assert(f.calls == 1 && !p.identity_match && !p.snapshot_complete);
    }
  const int failures[] = {-EIO, -ENXIO, -ETIMEDOUT, 1};
  for (unsigned i = 0; i < 4; i++)
    for (unsigned step = 1; step <= 5; step++)
      {
        f = (struct fixture){.id = 0x0214, .delay = 1, .fail_at = step, .injected = failures[i]};
        assert(a733_imx214_identify(&p, &io, &f) == (failures[i] > 0 ? -EPROTO : failures[i]));
        assert(f.calls == step && !p.snapshot_complete);
      }
  f = (struct fixture){.id = 0x0214, .delay = 500};
  assert(a733_imx214_identify(&p, &io, &f) == -ETIMEDOUT && f.calls == 1);
  f = (struct fixture){.id = 0x0214, .delay = 101};
  assert(a733_imx214_identify(&p, &io, &f) == -ETIMEDOUT && f.calls == 1);
  f = (struct fixture){.id = 0x0214, .delay = 100};
  assert(a733_imx214_identify(&p, &io, &f) == -ETIMEDOUT && f.calls == 5);
  f = (struct fixture){.id = 0x0214, .delay = 1, .time = UINT32_MAX - 2};
  assert(!a733_imx214_identify(&p, &io, &f) && p.elapsed_ms == 5);
  puts("IMX214 real probe core PASS: qualified board/24MHz gates, strict 0016 ID, five bounded reads, every transport error, deadline/wrap; no hardware or stream/DMA writes");
  return 0;
}
