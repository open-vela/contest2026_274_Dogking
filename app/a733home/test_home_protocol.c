/* SPDX-License-Identifier: Apache-2.0
 * Host-only test, never linked to the board image. */
#include "home_protocol.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
  struct home_message_s msg = {HOME_REQUEST, 0x1234, HOME_SET_OUTPUT, 2,
                                {3, 1}};
  struct home_message_s out;
  struct home_message_s before;
  uint8_t frame[HOME_MAX_FRAME + 1];
  unsigned int i;
  unsigned int j;
  uint32_t random = 0xa733;
  int n;

  assert(home_crc16((const uint8_t *)"123456789", 9) == 0x29b1);
  n = home_encode(&msg, frame, sizeof(frame));
  assert(n == 10 && frame[2] == 0x34 && frame[3] == 0x12);
  assert(home_decode(frame, n, &out) == 0);
  assert(out.seq == msg.seq && out.payload[0] == 3 && out.payload[1] == 1);
  assert(home_encode(&msg, frame, 9) == -ENOSPC);
  memset(&out, 0x5a, sizeof(out));
  before = out;
  frame[6] ^= 1;
  assert(home_decode(frame, n, &out) == -EBADMSG);
  assert(memcmp(&out, &before, sizeof(out)) == 0);
  assert(home_decode(NULL, 0, &out) == -EINVAL);
  assert(home_encode(NULL, frame, sizeof(frame)) == -EINVAL);
  msg.opcode = 0xff;
  assert(home_encode(&msg, frame, sizeof(frame)) == -ENOTSUP);
  msg.opcode = HOME_SET_OUTPUT;
  msg.length = 1;
  assert(home_encode(&msg, frame, sizeof(frame)) == -EINVAL);
  msg.type = HOME_ACK;
  msg.length = 0;
  assert(home_encode(&msg, frame, sizeof(frame)) == -EINVAL);
  msg.length = HOME_MAX_PAYLOAD;
  assert(home_encode(&msg, frame, sizeof(frame)) == HOME_MAX_FRAME);
  assert(home_decode(frame, HOME_MAX_FRAME, &out) == 0);
  frame[5] = 255;
  assert(home_decode(frame, HOME_MAX_FRAME, &out) == -EINVAL);

  for (i = 0; i < 100000; i++)
    {
      for (j = 0; j < sizeof(frame); j++)
        {
          random = random * 1664525u + 1013904223u;
          frame[j] = random >> 24;
        }

      (void)home_decode(frame, i % (sizeof(frame) + 1), &out);
    }

  puts("home-protocol: CRC/roundtrip/bounds/negative/fuzz passed");
  return 0;
}
