/* SPDX-License-Identifier: Apache-2.0 */
#ifndef A733_IMX214_MODE_H
#define A733_IMX214_MODE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Original runtime, not a copy of a Linux mode table. Configuration data
 * must be separately sourced, hashed and licence-reviewed. A7CM v1 is LE:
 * 48-byte header, 4-byte ops (type8,value8,reg16). CRC32 is corruption
 * detection, NOT authentication or proof that a sensor mode works.
 */
#define A733_IMX214_MODE_HEADER 48u
#define A733_IMX214_MODE_MAX_OPS 512u
struct a733_imx214_mode
{
  const uint8_t *ops; /* Caller owns IMMUTABLE bytes for the entire session. */
  uint32_t count;
  uint32_t crc32;
  uint16_t width, height, line_length, frame_length;
  uint8_t orientation, bayer_hint;
};

enum a733_imx214_state
{
  A733_IMX214_IDLE = 0,
  A733_IMX214_PREPARED,
  A733_IMX214_STREAMING,
  A733_IMX214_FAULT
};

struct a733_imx214_mode_io
{
  int (*read)(void *, uint16_t reg, uint8_t *, size_t, unsigned timeout_ms);
  int (*write8)(void *, uint16_t reg, uint8_t, unsigned timeout_ms);
  int (*delay_ms)(void *, unsigned ms);
  uint32_t (*now_ms)(void *);
  /* Must prove receiver armed with owned/mapped buffers and correct mode.
   * stop returns 0 ONLY after proving receiver DMA/IRQ/callbacks idle.
   * receiver_ready may return -EBUSY without fault ONLY if it acquired no
   * new resource and started no DMA. Other failures retain conservatively.
   * No reset/retry or NPU ownership side effects are permitted.
   */
  int (*receiver_ready)(void *, unsigned timeout_ms);
  int (*receiver_stop)(void *, unsigned timeout_ms);
  void *context;
  bool power_clock_qualified;
  bool exclusive_sensor_and_receiver;
  bool artifact_sha256_verified; /* Fixed source/version, not CRC alone. */
};

/* One object per owned hardware lifetime; zero once before first use.
 * Do not clear a fault to retry. The caller holds its ownership lock for
 * each whole operation. No external CLI sets these qualification fields.
 */
struct a733_imx214_session
{
  enum a733_imx214_state state;
  int error, stop_error;
  const char *stage;
  uint32_t writes, reads;
  bool retain_resources;
  bool stop_attempted;
  uint16_t last_register;
  unsigned verified_registers;
};

/* Explicit v194 standby-only experiment. Actual callbacks must hold the
 * v193 same-boot controls/ID session and reject STREAM_ON independently.
 * No power/frequency/receiver qualification or streaming lease is created.
 * The adapter verifies the pinned SHA256 BEFORE calling this function.
 */
struct a733_imx214_trial_mode_io
{
  int (*read)(void *,uint16_t,uint8_t *,size_t,unsigned);
  int (*write8)(void *,uint16_t,uint8_t,unsigned);
  int (*delay_ms)(void *,unsigned);
  uint32_t (*now_ms)(void *);
  void *context;
};
int a733_imx214_mode_trial_prepare(struct a733_imx214_session *,
  const struct a733_imx214_trial_mode_io *,const void *,size_t);

int a733_imx214_mode_parse(const void *bytes, size_t size,
                           struct a733_imx214_mode *mode);
int a733_imx214_mode_prepare(struct a733_imx214_session *session,
                             const struct a733_imx214_mode_io *io,
                             const void *bytes, size_t size);
int a733_imx214_mode_start(struct a733_imx214_session *session,
                           const struct a733_imx214_mode_io *io);
int a733_imx214_mode_stop(struct a733_imx214_session *session,
                          const struct a733_imx214_mode_io *io);
#endif
