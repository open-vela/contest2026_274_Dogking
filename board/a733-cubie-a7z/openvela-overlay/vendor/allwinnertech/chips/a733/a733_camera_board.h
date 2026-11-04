/* SPDX-License-Identifier: Apache-2.0 */
#ifndef A733_CAMERA_BOARD_H
#define A733_CAMERA_BOARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define A733_CAMERA_BOARD_REGS 12

/* Read-only always-on CCU/PIO inventory for the confirmed V1.11 J5 board.
 * No read from unqualified CSI/ISP/CCI/D-PHY/IOMMU or sensor addresses.
 * Register state is NOT measured voltage/frequency or an ownership token.
 */
struct a733_camera_board
{
  uint32_t raw[A733_CAMERA_BOARD_REGS];
  unsigned reads;
  uint32_t elapsed_ms;
  uint32_t last_address;
  int error;
  bool complete;
};

struct a733_camera_board_io
{
  /* Zero/negative errno, bounded by the supplied timeout. No writes. */
  int (*read)(void *, uint32_t, uint32_t *, unsigned timeout_ms);
  uint32_t (*now_ms)(void *);
};

int a733_camera_board_sample(struct a733_camera_board *snapshot,
                             const struct a733_camera_board_io *io,
                             void *context);
size_t a733_camera_board_report(const struct a733_camera_board *snapshot,
                                char *buffer, size_t capacity);
#endif
