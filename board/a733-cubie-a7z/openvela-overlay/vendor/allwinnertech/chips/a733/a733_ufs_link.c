/* SPDX-License-Identifier: Apache-2.0 */
/* Independently structured A733 M-PHY/UIC link core. Register/attribute facts
 * are cross-checked against the local Allwinner BSP and UFSHCI definitions;
 * no Linux tables, structures, lifecycle or error handling are imported.
 * Board power/clock/reset qualification remains a separate mandatory adapter.
 */
#include <errno.h>
#include <stddef.h>
#include <string.h>
#include "a733_ufs_link.h"

#define LINK_FATAL UINT32_C(0x00070884)
#define LINK_UCC UINT32_C(0x400)
#define LINK_TOTAL_US UINT32_C(10000000)

struct link_work
{
  struct a733_ufs_link_state *state;
  const struct a733_ufs_link_io *io;
};

static int link_deadline(struct link_work *w)
{
  w->state->elapsed_ms = w->io->now_ms(w->io->context) - w->state->start_ms;
  return w->state->elapsed_ms >= 10000 ? -ETIMEDOUT : 0;
}

static int link_read(struct link_work *w, unsigned int offset, uint32_t *value)
{
  int ret = link_deadline(w);
  if (ret < 0) return ret;
  w->state->last_offset = offset;
  ret = w->io->read(w->io->context, offset, value);
  if (ret > 0) ret = -EPROTO;
  if (ret == 0) w->state->last_value = *value;
  return ret;
}

static int link_write(struct link_work *w, unsigned int offset, uint32_t value)
{
  int ret = link_deadline(w);
  if (ret < 0) return ret;
  w->state->last_offset = offset;
  w->state->last_value = value;
  ret = w->io->write(w->io->context, offset, value);
  if (ret > 0) ret = -EPROTO;
  w->io->fence(w->io->context);
  return ret;
}

static int link_budget(struct link_work *w, unsigned int us)
{
  struct a733_ufs_link_state *s = w->state;
  s->elapsed_ms = w->io->now_ms(w->io->context) - s->start_ms;
  if (s->elapsed_ms >= 10000 || us > LINK_TOTAL_US - s->delay_total_us)
    return -ETIMEDOUT;
  s->delay_total_us += us;
  w->io->delay_us(w->io->context, us);
  return 0;
}

static int link_wait(struct link_work *w, unsigned int offset,
                      uint32_t mask, uint32_t wanted, unsigned int ms)
{
  uint32_t value;
  uint32_t status;
  uint32_t began = w->io->now_ms(w->io->context);
  unsigned int i;
  int ret;
  for (i = 0; i <= ms * 10; i++)
    {
      ++w->state->polls;
      ret = link_read(w, 0x20, &status);
      if (ret < 0) return ret;
      if (status & LINK_FATAL) return -EIO;
      ret = link_read(w, offset, &value);
      if (ret < 0) return ret;
      if (w->io->now_ms(w->io->context) - began >= ms) return -ETIMEDOUT;
      if ((value & mask) == wanted) return 0;
      if (i == ms * 10) break;
      ret = link_budget(w, 100);
      if (ret < 0) return ret;
    }
  return -ETIMEDOUT;
}

static int link_uic(struct link_work *w, unsigned int opcode,
                     uint32_t attribute, uint32_t *value)
{
  uint32_t result;
  int ret;
  if (opcode != 1 && opcode != 2 && opcode != 0x16) return -EPERM;
  ret = link_wait(w, 0x30, 8, 8, 50);
  if (ret < 0) return ret;
  w->state->last_attribute = attribute;
  /* No interrupt enable. W1C only the UIC-complete bit owned by this caller. */
  ret = link_write(w, 0x20, LINK_UCC);
  if (ret < 0) return ret;
  ret = link_write(w, 0x94, attribute);
  if (ret < 0) return ret;
  ret = link_write(w, 0x98, 0);
  if (ret < 0) return ret;
  ret = link_write(w, 0x9c, opcode == 2 ? *value : 0);
  if (ret < 0) return ret;
  ret = link_write(w, 0x90, opcode);
  if (ret < 0) return ret;
  ++w->state->commands;
  ret = link_wait(w, 0x20, LINK_UCC, LINK_UCC, 100);
  if (ret < 0) return ret;
  ret = link_read(w, 0x98, &result);
  if (ret < 0) return ret;
  w->state->last_uic_result = result & 255;
  if (result & 255) return -EPROTO;
  if (opcode == 1)
    {
      ret = link_read(w, 0x9c, value);
      if (ret < 0) return ret;
    }
  return link_write(w, 0x20, LINK_UCC);
}

static int link_attr(struct link_work *w, unsigned int attr,
                       unsigned int selector, uint32_t value)
{
  return link_uic(w, 2, (uint32_t)attr << 16 | selector, &value);
}

static int link_get(struct link_work *w, unsigned int attr, uint32_t *value)
{
  return link_uic(w, 1, (uint32_t)attr << 16, value);
}

/* The byte-addressed CR bridge is a hardware protocol, not a raw user API. */
static int link_cr(struct link_work *w, unsigned int address,
                    uint32_t *value, bool write)
{
  unsigned int byte;
  uint32_t low;
  uint32_t high;
  int ret;
  ret = link_attr(w, 0x8116, 0, address & 255);
  if (ret < 0) return ret;
  ret = link_attr(w, 0x8117, 0, address >> 8);
  if (ret < 0) return ret;
  if (write)
    for (byte = 0; byte < 2; byte++)
      {
        ret = link_attr(w, 0x8118 + byte, 0, (*value >> (byte * 8)) & 255);
        if (ret < 0) return ret;
      }
  ret = link_attr(w, 0x811c, 0, write ? 1 : 0);
  if (ret < 0) return ret;
  ret = link_attr(w, 0xd085, 0, 1);
  if (ret < 0 || write) return ret;
  ret = link_get(w, 0x811a, &low);
  if (ret < 0) return ret;
  ret = link_get(w, 0x811b, &high);
  if (ret < 0) return ret;
  if ((low | high) & ~UINT32_C(255)) return -EPROTO;
  *value = low | high << 8;
  return 0;
}

static int link_cr_set(struct link_work *w, unsigned int address, uint32_t value)
{
  return link_cr(w, address, &value, true);
}

static int link_cal_valid(const struct a733_ufs_link_board *b)
{
  unsigned int a = (b->cal_high >> 16) & 255;
  unsigned int rateb = b->cal_high >> 24;
  uint32_t afe = (b->cal_low >> 16) | (b->cal_high << 16);
  unsigned int i;
  if ((a && (a < 0x14 || a > 0x4b)) ||
      (rateb && (rateb < 0x4b || rateb > 0x73))) return -ERANGE;
  /* Whole AFE set or whole default. Never silently mix partly absent efuses. */
  if (afe)
    for (i = 0; i < 4; i++)
      if (((afe >> (8 * i)) & 255) == 0 ||
          ((afe >> (8 * i)) & 255) == 255) return -ERANGE;
  return 0;
}

static int link_phy(struct link_work *w, const struct a733_ufs_link_board *b)
{
  unsigned int a = (b->cal_high >> 16) & 255;
  unsigned int rateb = b->cal_high >> 24;
  unsigned int lane;
  uint32_t value;
  int ret;
  w->state->phase = A733_UFS_LINK_RMMI;
  ret = link_attr(w, 0x8114, 0, rateb || !a ? 1 : 0);
  if (ret < 0) return ret;
  ret = link_attr(w, 0x8132, 0, 0x90);
  if (ret < 0) return ret;
  for (lane = 0; lane < 2; lane++)
    {
      ret = link_attr(w, 0x8009, 4 + lane, 1);
      if (ret < 0) return ret;
      ret = link_attr(w, 0x8013, 4 + lane, 2);
      if (ret < 0) return ret;
    }
  ret = link_attr(w, 0x814d, 0, a ? a : 2);
  if (ret < 0) return ret;
  ret = link_attr(w, 0x814e, 0, rateb ? rateb : 0x80);
  if (ret < 0) return ret;
  ret = link_attr(w, 0x811f, 0, 1);
  if (ret < 0) return ret;
  ret = link_attr(w, 0xd085, 0, 1);
  if (ret < 0) return ret;

  w->state->phase = A733_UFS_LINK_SRAM;
  ret = w->io->release_phy(w->io->context);
  w->io->fence(w->io->context);
  if (ret > 0) ret = -EPROTO;
  if (ret < 0) return ret;
  ret = link_wait(w, 0x200, 1u << 24, 1u << 24, 10);
  if (ret < 0) return ret;
  ret = link_cr(w, 0xc002, &value, false);
  if (ret < 0) return ret;
  ret = link_cr_set(w, 0xc002, value); /* Keep existing PHY SRAM word. */
  if (ret < 0) return ret;

  w->state->phase = A733_UFS_LINK_CAL;
  if (a || rateb)
    {
      ret = link_cr_set(w, 0x7014, rateb ? rateb : a);
      if (ret < 0) return ret;
      ret = link_cr_set(w, 0x0028, rateb ? rateb : a);
      if (ret < 0) return ret;
    }
  for (lane = 0; lane < 2; lane++)
    {
      ret = link_cr_set(w, 0x401c + lane * 0x100, 4);
      if (ret < 0) return ret;
    }
  if ((b->cal_low >> 16) || (b->cal_high & 65535))
    for (lane = 0; lane < 2; lane++)
      {
        unsigned int shift = lane ? 0 : 16;
        uint32_t word = lane ? b->cal_high : b->cal_low;
        ret = link_cr_set(w, 0x4000 + lane * 0x100, (word >> shift) & 255);
        if (ret < 0) return ret;
        ret = link_cr_set(w, 0x4001 + lane * 0x100, (word >> (shift + 8)) & 255);
        if (ret < 0) return ret;
        ret = link_cr_set(w, 0x401e + lane * 0x100, 0xa00);
        if (ret < 0) return ret;
      }
  if (!a && !rateb)
    {
      ret = link_cr_set(w, 0x0020, 8);
      if (ret < 0) return ret;
    }
  ret = link_read(w, 0x200, &value);
  if (ret < 0) return ret;
  ret = link_write(w, 0x200, value | (1u << 19));
  if (ret < 0) return ret;
  ret = link_attr(w, 0xd085, 0, 1);
  if (ret < 0) return ret;

  w->state->phase = A733_UFS_LINK_ENABLE;
  ret = link_attr(w, 0xd0c1, 0, 0);
  if (ret < 0) return ret;
  ret = link_attr(w, 0xd0a0, 0, 0x1b);
  if (ret < 0) return ret;
  return link_attr(w, 0xd0fb, 0, 10);
}

static int link_attempt(struct link_work *w,
                         const struct a733_ufs_link_board *board)
{
  uint32_t value;
  int ret;
  w->state->attempted = true; /* Before the first write, including failed write. */
  w->state->phase = A733_UFS_LINK_HOST;
  ret = link_write(w, 0x34, 1);
  if (ret < 0) return ret;
  ret = link_budget(w, 1000);
  if (ret < 0) return ret;
  ret = link_wait(w, 0x34, 1, 1, 50);
  if (ret < 0) return ret;
  ret = link_phy(w, board);
  if (ret < 0) return ret;
  w->state->phase = A733_UFS_LINK_START;
  value = 0;
  ret = link_uic(w, 0x16, 0, &value);
  if (ret < 0) return ret;
  w->state->phase = A733_UFS_LINK_VERIFY;
  ret = link_wait(w, 0x30, 0x33, 3, 50);
  if (ret < 0) return ret;
  ret = link_get(w, 0xd083, &value);
  if (ret < 0) return ret;
  if (value != 2) return -ENOLINK;
  w->state->phase = A733_UFS_LINK_CPORT;
  ret = link_attr(w, 0x4020, 0, 0);
  if (ret < 0) return ret;
  ret = link_attr(w, 0x4025, 0, 6);
  if (ret < 0) return ret;
  ret = link_attr(w, 0x4020, 0, 1);
  if (ret < 0) return ret;
  ret = link_get(w, 0x4020, &value);
  if (ret < 0) return ret;
  if (value != 1) return -ENOLINK;
  /* An error appearing during the last DME read must not become link_up. */
  ret = link_read(w, 0x20, &value);
  if (ret < 0) return ret;
  if (value & LINK_FATAL) return -EIO;
  ret = link_read(w, 0x30, &value);
  if (ret < 0) return ret;
  if ((value & 0x33) != 3) return -ENOLINK;
  ret = link_deadline(w);
  if (ret < 0) return ret;
  w->state->phase = A733_UFS_LINK_UP;
  w->state->link_up = true;
  return 0;
}

int a733_ufs_link_start(struct a733_ufs_link_state *state,
                        const struct a733_ufs_link_io *io)
{
  struct a733_ufs_link_board board = {0};
  struct link_work work = {state, io};
  uint32_t value;
  unsigned int i;
  int ret;
  /* Reject absent adapters before any HCI access or write. */
  if (!state || !io || !io->qualify || !io->read || !io->write ||
      !io->release_phy || !io->fence || !io->delay_us || !io->now_ms)
    return -EINVAL;
  if (state->quarantined) return -EIO;
  if (state->attempted) return -EALREADY;
  state->phase = A733_UFS_LINK_BOARD;
  ret = io->qualify(io->context, &board);
  if (ret > 0) ret = -EPROTO;
  if (ret == 0 && board.proofs != A733_UFS_LINK_PROOFS) ret = -ENOTSUP;
  if (ret == 0) ret = link_cal_valid(&board);
  if (ret < 0) goto done;
  state->start_ms = io->now_ms(io->context);
  /* A reset/idle host only. No takeover of running HCE/transfer/task/UIC. */
  for (i = 0; i < 7; i++)
    {
      static const unsigned int zero[] = {0x34, 0x24, 0x18, 0x58, 0x78, 0x60, 0x80};
      ret = link_read(&work, zero[i], &value);
      if (ret < 0) goto done;
      if (value) {ret = -EBUSY; goto done;}
    }
  ret = link_read(&work, 8, &value);
  if (ret < 0) goto done;
  if (value < 0x200 || value > 0x310) {ret = -ENODEV; goto done;}
  ret = link_read(&work, 0x20, &value);
  if (ret < 0) goto done;
  if (value & LINK_FATAL) {ret = -EIO; goto done;}
  ret = link_read(&work, 0x200, &value);
  if (ret < 0) goto done;
  /* This engine supports only verified internal 26MHz + cfg19.2MHz profile. */
  if ((value & 0x00183f3b) != 0x1309) {ret = -ENOTSUP; goto done;}
  ret = link_read(&work, 0x204, &value);
  if (ret < 0) goto done;
  if ((value & 0x800303ff) != 0x800003ff) {ret = -ENOTSUP; goto done;}
  ret = link_read(&work, 0x32c, &value);
  if (ret < 0) goto done;
  if (value & 1) {ret = -ENOTSUP; goto done;}
  ret = link_attempt(&work, &board);
done:
  state->error = ret;
  if (ret < 0) state->link_up = false;
  state->quarantined = state->attempted && ret < 0;
  if (state->attempted)
    state->elapsed_ms = io->now_ms(io->context) - state->start_ms;
  return ret;
}
