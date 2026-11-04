/* SPDX-License-Identifier: Apache-2.0; host-only, not linked on target. */
#include "a733_hci_framer.h"
#include <assert.h>
#include <stdio.h>

static size_t received;
static size_t last;
static uint8_t copy[A733_HCI_RX_MAX];
static int deliver(const uint8_t *data, size_t len)
{
  received++;
  last = len;
  memcpy(copy, data, len);
  return 0;
}
static int reject(const uint8_t *data, size_t len)
{
  (void)data;
  (void)len;
  return -ENOMEM;
}

int main(void)
{
  struct a733_hci_frame_s f = {0};
  uint8_t events[] = {0x0e, 3, 1, 1, 0x10, 0x0f, 0};
  uint8_t acl[A733_HCI_RX_MAX] = {1, 0, 0xfd, 3};
  uint8_t huge[] = {1, 0, 0xff, 0xff};
  uint32_t random = 0xa733;
  uint8_t bytes[64];
  size_t i;
  size_t split;

  for (split = 0; split <= sizeof(events); split++)
    {
      memset(&f, 0, sizeof(f));
      received = 0;
      assert(a733_hci_feed(&f, 2, events, split, deliver) == 0);
      assert(a733_hci_feed(&f, 2, events + split,
                           sizeof(events) - split, deliver) == 0);
      assert(received == 2 && last == 2 && f.used == 0);
    }

  for (i = 4; i < sizeof(acl); i++) { acl[i] = (uint8_t)i; }
  received = 0;
  for (i = 0; i < sizeof(acl); i++)
    {
      assert(a733_hci_feed(&f, 4, acl + i, 1, deliver) == 0);
    }
  assert(received == 1 && last == sizeof(acl));
  assert(memcmp(copy, acl, sizeof(acl)) == 0);
  assert(a733_hci_feed(&f, 4, huge, sizeof(huge), deliver) == -EMSGSIZE);
  assert(f.used == 0 && f.target == 0);
  assert(a733_hci_feed(&f, 2, events, 5, reject) == -ENOMEM);
  assert(f.used == 0 && f.target == 0);
  assert(a733_hci_feed(NULL, 2, events, 5, deliver) == -EINVAL);
  assert(a733_hci_feed(&f, 3, events, 5, deliver) == -EINVAL);
  assert(a733_hci_feed(&f, 2, NULL, 1, deliver) == -EINVAL);
  for (i = 0; i < 100000; i++)
    {
      size_t j;
      for (j = 0; j < sizeof(bytes); j++)
        {
          random = random * 1664525u + 1013904223u;
          bytes[j] = random >> 24;
        }
      (void)a733_hci_feed(&f, 4, bytes, sizeof(bytes), deliver);
    }
  puts("hci-framer: fragmentation/coalescing/max/bounds/fuzz passed");
  return 0;
}
