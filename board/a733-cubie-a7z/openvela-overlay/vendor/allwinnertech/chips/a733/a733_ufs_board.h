/* SPDX-License-Identifier: Apache-2.0 */
#ifndef A733_UFS_BOARD_H
#define A733_UFS_BOARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Fixed read-only CCU/RTC inventory, NOT a power qualification token. */
#define A733_UFS_BOARD_REGS 13
struct a733_ufs_board
{
  uint32_t raw[A733_UFS_BOARD_REGS];
  uint32_t attempts;
  uint32_t reads;
  uint32_t last_address;
  uint32_t dcxo_hz;
  unsigned int axi_mux;
  unsigned int cfg_mux;
  unsigned int axi_div;
  unsigned int cfg_div;
  unsigned int resets;
  unsigned int gates;
  bool stable;
  int error;
  const char *stage;
};

typedef int (*a733_ufs_board_read_t)(void *, uint32_t, uint32_t *);
int a733_ufs_board_sample(struct a733_ufs_board *state,
                          a733_ufs_board_read_t read, void *context);
size_t a733_ufs_board_report(const struct a733_ufs_board *state,
                             char *buffer, size_t capacity);
bool a733_ufs_board_hci_allowed(void);

#endif
