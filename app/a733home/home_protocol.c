/* SPDX-License-Identifier: Apache-2.0 */
#include "home_protocol.h"
#include <errno.h>
#include <string.h>

uint16_t home_crc16(const uint8_t *bytes, size_t size)
{
  uint16_t crc = 0xffff;
  size_t i;
  unsigned int bit;

  for (i = 0; i < size; i++)
    {
      crc ^= (uint16_t)bytes[i] << 8;
      for (bit = 0; bit < 8; bit++)
        {
          crc = (uint16_t)((crc << 1) ^ ((crc & 0x8000) ? 0x1021 : 0));
        }
    }

  return crc;
}

static int home_validate(const struct home_message_s *msg)
{
  if (msg->type < HOME_REQUEST || msg->type > HOME_EVENT ||
      msg->length > HOME_MAX_PAYLOAD)
    {
      return -EINVAL;
    }

  if (msg->opcode != HOME_PING && msg->opcode != HOME_GET_STATE &&
      msg->opcode != HOME_SET_OUTPUT)
    {
      return -ENOTSUP;
    }

  if (msg->type == HOME_ACK && msg->length == 0)
    {
      return -EINVAL; /* ACK must contain an application status. */
    }

  if (msg->type == HOME_REQUEST &&
      ((msg->opcode == HOME_SET_OUTPUT && msg->length != 2) ||
       (msg->opcode != HOME_SET_OUTPUT && msg->length != 0)))
    {
      return -EINVAL;
    }

  return 0;
}

int home_encode(const struct home_message_s *msg, uint8_t *out, size_t cap)
{
  uint8_t frame[HOME_MAX_FRAME];
  uint16_t crc;
  size_t len;
  int ret;

  if (msg == NULL || out == NULL)
    {
      return -EINVAL;
    }

  ret = home_validate(msg);
  if (ret < 0)
    {
      return ret;
    }

  len = 8 + msg->length;
  if (cap < len)
    {
      return -ENOSPC;
    }

  frame[0] = HOME_VERSION;
  frame[1] = msg->type;
  frame[2] = (uint8_t)msg->seq;
  frame[3] = (uint8_t)(msg->seq >> 8);
  frame[4] = msg->opcode;
  frame[5] = msg->length;
  memcpy(frame + 6, msg->payload, msg->length);
  crc = home_crc16(frame, len - 2);
  frame[len - 2] = (uint8_t)crc;
  frame[len - 1] = (uint8_t)(crc >> 8);
  memcpy(out, frame, len);
  return (int)len;
}

int home_decode(const uint8_t *frame, size_t size, struct home_message_s *out)
{
  struct home_message_s msg = {0};
  uint16_t crc;
  int ret;

  if (frame == NULL || out == NULL || size < 8 || size > HOME_MAX_FRAME)
    {
      return -EINVAL;
    }

  if (frame[0] != HOME_VERSION)
    {
      return -ENOTSUP;
    }

  if (frame[5] > HOME_MAX_PAYLOAD || size != (size_t)frame[5] + 8)
    {
      return -EINVAL;
    }

  crc = (uint16_t)frame[size - 2] | ((uint16_t)frame[size - 1] << 8);
  if (crc != home_crc16(frame, size - 2))
    {
      return -EBADMSG;
    }

  msg.type = frame[1];
  msg.seq = (uint16_t)frame[2] | ((uint16_t)frame[3] << 8);
  msg.opcode = frame[4];
  msg.length = frame[5];
  memcpy(msg.payload, frame + 6, msg.length);
  ret = home_validate(&msg);
  if (ret == 0)
    {
      *out = msg;
    }

  return ret;
}
