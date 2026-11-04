/* SPDX-License-Identifier: Apache-2.0 */
#ifndef A733_CAMERA_POWER_HANDOFF_H
#define A733_CAMERA_POWER_HANDOFF_H
#include <stdint.h>
#define A733_CAMERA_POWER_SIGNATURE 0x41375057u
#define A733_CAMERA_POWER_ABI 0x00019201u
#define A733_CAMERA_POWER_READY 0x434d5031u
/* EXACT 64-byte in-Image .data record. U-Boot checks this Image's CRC before
 * touching it, reads only AXP8191 0e/0f/10/12/20/2a, commits ready LAST, then
 * booti relocates the modified Image normally. Plain boot loads ready=0.
 * CRC detects mistakes, NOT malicious firmware. This is operator-owned
 * bring-up evidence, NOT a security boundary or runtime rail measurement.
 */
struct a733_camera_power_handoff
{
  uint32_t signature,abi,ready,crc_scratch;
  uint8_t values[6];
  uint8_t reserved[42];
};
_Static_assert(sizeof(struct a733_camera_power_handoff)==64,"handoff ABI size");
extern volatile struct a733_camera_power_handoff g_a733_camera_power_handoff;
int a733_camera_control_initialize(void);
#define A733_CAMERA_MODE_SIGNATURE 0x41374d44u
#define A733_CAMERA_MODE_READY 0x434d4f31u
#define A733_CAMERA_MODE_BYTES 660u
/* Separate external GPL-derived data, loaded by U-Boot; no table compiled
 * into the Apache runtime. Runtime checks the fixed SHA256 before transfer.
 */
struct a733_camera_mode_handoff
{
  uint32_t signature,abi,bytes,ready;
  uint8_t data[A733_CAMERA_MODE_BYTES];
  uint8_t reserved[28];
};
_Static_assert(sizeof(struct a733_camera_mode_handoff)==704,"mode handoff size");
#endif
