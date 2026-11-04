/* SPDX-License-Identifier: Apache-2.0 */
#ifndef A733_HOME_PROTOCOL_H
#define A733_HOME_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#define HOME_VERSION 1
#define HOME_SERVICE_UUID "a7330001-6b42-4f65-8d37-9b184b771001"
#define HOME_COMMAND_UUID "a7330002-6b42-4f65-8d37-9b184b771001"
#define HOME_STATUS_UUID "a7330003-6b42-4f65-8d37-9b184b771001"
#define HOME_MAX_PAYLOAD 12
#define HOME_MAX_FRAME 20
#define HOME_REQUEST 1
#define HOME_ACK 2
#define HOME_EVENT 3
#define HOME_PING 0x01
#define HOME_GET_STATE 0x02
#define HOME_SET_OUTPUT 0x10

struct home_message_s
{
  uint8_t type;
  uint16_t seq;
  uint8_t opcode;
  uint8_t length;
  uint8_t payload[HOME_MAX_PAYLOAD];
};

/* Stateless codec shared by central/peripheral; no actuator calls here.
 * Returns frame length / 0 on success, negative errno on failure.
 * Decode only publishes a message after all validation has succeeded. */
int home_encode(const struct home_message_s *msg, uint8_t *out, size_t cap);
int home_decode(const uint8_t *frame, size_t size, struct home_message_s *out);
uint16_t home_crc16(const uint8_t *bytes, size_t size);

#endif
