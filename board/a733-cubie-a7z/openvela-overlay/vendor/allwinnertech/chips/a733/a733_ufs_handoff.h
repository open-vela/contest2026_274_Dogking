/* SPDX-License-Identifier: Apache-2.0 */
#ifndef A733_UFS_HANDOFF_H
#define A733_UFS_HANDOFF_H
#include "a733_ufs_board.h"

#define A733_UFS_HANDOFF_REGS 16
struct a733_ufs_handoff
{
  uint32_t raw[A733_UFS_HANDOFF_REGS];
  uint32_t reads;
  uint32_t last_offset;
  bool stable;
  bool link_ready;
  bool pending;
  bool needs_quiesce;
  int error;
  const char *stage;
};

bool a733_ufs_handoff_profile(const struct a733_ufs_board *board);
int a733_ufs_handoff_snapshot(struct a733_ufs_handoff *state,
                              const struct a733_ufs_board *board,
                              a733_ufs_board_read_t read, void *context);
size_t a733_ufs_handoff_report(const struct a733_ufs_handoff *state,
                               char *buffer, size_t capacity);
#endif
