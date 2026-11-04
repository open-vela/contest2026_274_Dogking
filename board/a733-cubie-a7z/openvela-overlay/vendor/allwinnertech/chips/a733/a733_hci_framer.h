/* SPDX-License-Identifier: Apache-2.0
 * USB packet boundaries are not HCI message boundaries. Portable stream
 * assembler, shared with the host-only acceptance test. No ATT implementation.
 */
#ifndef A733_HCI_FRAMER_H
#define A733_HCI_FRAMER_H
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define A733_HCI_RX_MAX 1025
struct a733_hci_frame_s
{
  size_t used;
  size_t target;
  uint8_t bytes[A733_HCI_RX_MAX];
};

typedef int (*a733_hci_deliver_t)(const uint8_t *, size_t);

static inline int a733_hci_feed(struct a733_hci_frame_s *frame,
                                unsigned int header, const uint8_t *data,
                                size_t len, a733_hci_deliver_t deliver)
{
  size_t take;
  size_t need;
  int ret;

  if (frame == NULL || (header != 2 && header != 4) || deliver == NULL ||
      (data == NULL && len != 0))
    {
      return -EINVAL;
    }

  while (len > 0)
    {
      need = frame->used < header ? header : frame->target;
      if (need < frame->used || need > sizeof(frame->bytes))
        {
          frame->used = frame->target = 0;
          return -EMSGSIZE;
        }

      take = need - frame->used;
      if (take > len)
        {
          take = len;
        }

      memcpy(frame->bytes + frame->used, data, take);
      frame->used += take;
      data += take;
      len -= take;
      if (frame->used < header)
        {
          continue;
        }

      if (frame->target == 0)
        {
          frame->target = header == 2 ? 2u + (size_t)frame->bytes[1] :
            4u + (size_t)frame->bytes[2] + ((size_t)frame->bytes[3] << 8);
          if (frame->target > sizeof(frame->bytes))
            {
              frame->used = frame->target = 0;
              return -EMSGSIZE;
            }
        }

      if (frame->used == frame->target)
        {
          ret = deliver(frame->bytes, frame->used);
          frame->used = frame->target = 0;
          if (ret < 0)
            {
              return ret;
            }
        }
    }

  return 0;
}
#endif
