/* SPDX-License-Identifier: Apache-2.0 */
#ifndef A733_WIFI_SECRET_INPUT_H
#define A733_WIFI_SECRET_INPUT_H
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* Byte-preserving WPA passphrase editor. NSH normally owns line editing;
 * fgets() alone in a nested command can instead put DEL/BS into the PSK.
 * No echo, trimming, truncation, normalization, or password logging. */
static int wifi_secret_step(char *text, size_t capacity, size_t *length,
                            bool *invalid, int key)
{
  if (key == 3) return -ECANCELED;
  if (key == '\r' || key == '\n')
    return !*invalid && *length >= 8 && *length <= 63 ? 1 : -EINVAL;
  if (key == 21) /* Ctrl+U: discard the complete provisional input. */
    { memset(text, 0, capacity); *length = 0; *invalid = false; return 0; }
  if (key == 8 || key == 127)
    {
      size_t old = *length;
      if (*length) --*length;
      while (*length && ((unsigned char)text[*length] & 0xc0) == 0x80)
        --*length;
      memset(text + *length, 0, old - *length);
      return 0;
    }
  if (key < 32 || key > 255 || *length + 1 >= capacity)
    { *invalid = true; return 0; }
  text[(*length)++] = (char)key;
  text[*length] = 0;
  return 0;
}
#endif
