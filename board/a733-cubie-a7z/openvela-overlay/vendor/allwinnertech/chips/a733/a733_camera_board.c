/* SPDX-License-Identifier: Apache-2.0 */
#include "a733_camera_board.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>

/* Factual register locations checked against Allwinner sun60iw2 CCU and
 * type-4 main PIO layout. sys24M and dcxo are distinct clock parents.
 * Do not use a 26MHz RTC selector as a measured CSI master frequency.
 */
static const uint32_t registers[A733_CAMERA_BOARD_REGS] =
{
  0x02002518, /* CCU APB1 */
  0x02002e8c, /* CCU TWI3 gate/reset */
  0x02003804, /* CCU CSI master1: parent[26:24], N[12:8], M[4:0], gate31 */
  0x02003840, /* CCU CSI module clock */
  0x02003844, /* CCU CSI bus */
  0x02003860, /* CCU ISP module clock */
  0x020025e4, /* CCU MBUS peripheral gates, CSI8 / ISP9; no key writes */
  0x02000280, /* PE configuration0: PE3/4=TWI3, PE5=MCLK1 */
  0x02000284, /* PE configuration1: PE8=STBY, PE9=RESET */
  0x02000290, /* PE data */
  0x020002a0, /* PE drive0 */
  0x020002b0  /* PE pull0 */
};

int a733_camera_board_sample(struct a733_camera_board *s,
                             const struct a733_camera_board_io *io,
                             void *context)
{
  if (!s) return -EINVAL;
  memset(s, 0, sizeof(*s));
  if (!io || !io->read || !io->now_ms) return s->error = -EINVAL;
  uint32_t start = io->now_ms(context);
  int ret = 0;
  for (unsigned i = 0; i < A733_CAMERA_BOARD_REGS; ++i)
    {
      uint32_t elapsed = io->now_ms(context) - start;
      if (elapsed >= 100) {ret = -ETIMEDOUT; break;}
      unsigned allowance = 100 - elapsed;
      if (allowance > 10) allowance = 10;
      uint32_t before = io->now_ms(context);
      s->last_address = registers[i];
      ++s->reads;
      ret = io->read(context, registers[i], &s->raw[i], allowance);
      if (ret > 0) ret = -EPROTO;
      if (ret) break;
      uint32_t after = io->now_ms(context);
      if (after - before > allowance || after - start >= 100)
        {ret = -ETIMEDOUT; break;}
    }
  s->elapsed_ms = io->now_ms(context) - start;
  s->error = ret;
  s->complete = ret == 0;
  return ret;
}

size_t a733_camera_board_report(const struct a733_camera_board *s,
                                char *buffer, size_t capacity)
{
  if (!s || !buffer || !capacity) return 0;
  /* Configuration observations only. Even all six matches do NOT qualify
   * a rail, measure MCLK, grant bus ownership or permit a sensor transfer.
   * Incomplete snapshots must never look like a favourable configuration.
   */
  unsigned checks = 0;
  if (s->complete && !s->error && s->reads == A733_CAMERA_BOARD_REGS)
    {
      checks += (s->raw[1] & 0x10001) == 0x10001;
      checks += (s->raw[0] & 0x0300001f) == 0;
      checks += ((s->raw[7] >> 12) & 0xff) == 0x22;
      checks += ((s->raw[7] >> 20) & 15) == 2;
      checks += (s->raw[2] & 0x87001f1f) == 0x80000000;
      checks += (s->raw[8] & 0xff) == 0x11;
    }
  int n = snprintf(buffer, capacity,
    "A733 IMX214 v191 V1.11/J5 read-only board checkpoint (NOT video capture)\n"
    "complete=%u error=%d reads=%u elapsed-ms=%lu last-address=%08lx\n"
    "route: J5 31-pin 0.3mm, MCSIB 4 lanes; J3 is PCIe, NOT camera\n"
    "control: TWI3 PE3/SCL PE4/SDA mux2; MCLK1 PE5/mux2; STBY=PE8 RESET=PE9\n"
    "ccu: apb1=%08lx twi3-bgr=%08lx csi-master1=%08lx csi=%08lx "
    "csi-bus=%08lx isp=%08lx mbus=%08lx\n"
    "pe: cfg0=%08lx cfg1=%08lx data=%08lx drive0=%08lx pull0=%08lx\n"
    "decode: scl-mux=%lu sda-mux=%lu mclk-mux=%lu master-parent=%lu "
    "master-M=%lu master-N=%lu gate=%lu reset-pin=%lu stby-pin=%lu\n"
    "handoff-config: matched=%u/6 (TWI gate/reset, APB1 sys24M /1, "
    "SCL/SDA mux2, MCLK mux2, MCLK sys24M /1 gated, control GPIO outputs)\n"
    "gate-policy: configuration matches NEVER unlock a sensor transfer; "
    "physical provider=unbound cold-start=unimplemented capture=unimplemented\n"
    "qualification: rails=unknown measured-mclk=unknown sensor-id=not-read "
    "CSI/DPHY/DMA/ISP=not-accessed\n"
    "module: pins28/29=3V3 pins30/31=5V per Radxa brief; "
    "NOT bare IMX214 power rails\n"
    "This inventory is NOT power/clock qualification; no register writes, "
    "sensor scan, reset, capture, /dev/video0 registration or NPU call\n",
    s->complete ? 1 : 0, s->error, s->reads,
    (unsigned long)s->elapsed_ms, (unsigned long)s->last_address,
    (unsigned long)s->raw[0], (unsigned long)s->raw[1],
    (unsigned long)s->raw[2], (unsigned long)s->raw[3],
    (unsigned long)s->raw[4], (unsigned long)s->raw[5],
    (unsigned long)s->raw[6], (unsigned long)s->raw[7],
    (unsigned long)s->raw[8], (unsigned long)s->raw[9],
    (unsigned long)s->raw[10], (unsigned long)s->raw[11],
    (unsigned long)((s->raw[7] >> 12) & 15),
    (unsigned long)((s->raw[7] >> 16) & 15),
    (unsigned long)((s->raw[7] >> 20) & 15),
    (unsigned long)((s->raw[2] >> 24) & 7),
    (unsigned long)((s->raw[2] & 31) + 1),
    (unsigned long)(((s->raw[2] >> 8) & 31) + 1),
    (unsigned long)(s->raw[2] >> 31),
    (unsigned long)((s->raw[9] >> 9) & 1),
    (unsigned long)((s->raw[9] >> 8) & 1), checks);
  return n < 0 ? 0 : (size_t)n >= capacity ? capacity - 1 : (size_t)n;
}
