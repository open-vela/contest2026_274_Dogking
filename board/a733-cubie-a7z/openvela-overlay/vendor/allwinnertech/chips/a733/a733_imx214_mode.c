/* SPDX-License-Identifier: Apache-2.0 */
#include "a733_imx214_mode.h"
#include <errno.h>
#include <string.h>

static uint16_t le16(const uint8_t *p)
{return p[0] | (uint16_t)p[1] << 8;}
static uint32_t le32(const uint8_t *p)
{return le16(p) | (uint32_t)le16(p + 2) << 16;}
static uint32_t crc32(const uint8_t *p, size_t n)
{
  uint32_t crc = UINT32_MAX;
  while (n--)
    {
      crc ^= *p++;
      for (unsigned i = 0; i < 8; ++i)
        crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
  return ~crc;
}

/* Exact 24MHz/4lane/RAW10, 1920x1080 profile, initially sourced from the
 * hash-pinned Allwinner VIN IMX214 reference. No table is linked here.
 * Validate effective (last-written) values, not just nominal metadata.
 */
static const uint16_t check_regs[] =
{0x0101,0x0112,0x0113,0x0114,0x0136,0x0137,
 0x0340,0x0341,0x0342,0x0343,0x034c,0x034d,0x034e,0x034f};
static const uint8_t check_values[] =
{3,10,10,3,24,0,8,0x50,0x13,0x90,7,0x80,4,0x38};

int a733_imx214_mode_parse(const void *bytes, size_t size,
                           struct a733_imx214_mode *mode)
{
  if (!bytes || !mode || size < A733_IMX214_MODE_HEADER) return -EINVAL;
  const uint8_t *p = bytes;
  if (memcmp(p, "A7CM", 4) || le16(p + 4) != 1 || le16(p + 6) != 48 ||
      le32(p + 8) != size || le16(p + 20) != 0x0214 ||
      le16(p + 22) != 1920 || le16(p + 24) != 1080 ||
      le16(p + 26) != 5008 || le16(p + 28) != 2128 ||
      p[30] != 24 || p[31] != 4 || p[32] != 10 || p[33] != 3 ||
      p[34] > 4 || p[35] || le32(p + 36) != 800 || le32(p + 40))
    return -EPROTO;
  uint32_t count = le32(p + 12);
  if (!count || count > A733_IMX214_MODE_MAX_OPS ||
      size != 48u + (size_t)count * 4) return -EMSGSIZE;
  if (crc32(p, 44) != le32(p + 44) ||
      crc32(p + 48, size - 48) != le32(p + 16)) return -EBADMSG;
  uint8_t effective[sizeof(check_values)] = {0};
  uint32_t seen = 0, delay_sum = 0;
  for (uint32_t i = 0; i < count; ++i)
    {
      const uint8_t *op = p + 48 + i * 4;
      uint16_t reg = le16(op + 2);
      if (op[0] == 2)
        {
          if (op[1] || !reg || reg > 50 || (delay_sum += reg) > 250)
            return -EINVAL;
          continue;
        }
      /* Configuration cannot implicitly start streaming or soft-reset the
       * sensor. It also cannot write identity/OTP subaddresses below 0x100.
       */
      if (op[0] != 1 || reg <= 0x0100 || reg == 0x0103 || reg == 0xffff)
        return -EACCES;
      for (unsigned j = 0; j < sizeof(check_values); ++j)
        if (reg == check_regs[j]) {effective[j] = op[1]; seen |= 1u << j;}
    }
  if (seen != (1u << sizeof(check_values)) - 1 ||
      memcmp(effective, check_values, sizeof(effective))) return -EPROTO;
  *mode = (struct a733_imx214_mode){.ops = p + 48, .count = count,
    .crc32 = le32(p + 16), .width = 1920, .height = 1080,
    .line_length = 5008, .frame_length = 2128,
    .orientation = p[33], .bayer_hint = p[34]};
  return 0;
}

static int gate(const struct a733_imx214_mode_io *io, bool configuring)
{
  if (!io || !io->read || !io->write8 || !io->now_ms) return -EINVAL;
  if (!io->power_clock_qualified || !io->exclusive_sensor_and_receiver ||
      (configuring && !io->artifact_sha256_verified)) return -EACCES;
  return 0;
}

static int checked(struct a733_imx214_session *s,
                    const struct a733_imx214_mode_io *io, uint32_t before,
                    unsigned budget, int result)
{
  if (result > 0) result = -EPROTO;
  if (!result && io->now_ms(io->context) - before > budget) result = -ETIMEDOUT;
  if (result)
    {
      if (!s->error) s->error = result;
      s->state = A733_IMX214_FAULT;
      s->retain_resources = true;
    }
  return result;
}

static int read_bytes(struct a733_imx214_session *s,
                       const struct a733_imx214_mode_io *io, uint16_t reg,
                       uint8_t *data, size_t count, unsigned budget)
{
  uint32_t before = io->now_ms(io->context);
  s->last_register=reg;
  ++s->reads;
  int ret = io->read(io->context, reg, data, count, budget);
  return checked(s, io, before, budget, ret);
}

static int write_byte(struct a733_imx214_session *s,
                       const struct a733_imx214_mode_io *io, uint16_t reg,
                       uint8_t value, unsigned budget)
{
  uint32_t before = io->now_ms(io->context);
  s->last_register=reg;
  ++s->writes;
  int ret = io->write8(io->context, reg, value, budget);
  return checked(s, io, before, budget, ret);
}

static int fail(struct a733_imx214_session *s, int error)
{
  if (!s->error) s->error = error;
  s->state = A733_IMX214_FAULT;
  s->retain_resources = true;
  return error;
}

static int prepare_impl(struct a733_imx214_session *s,
                             const struct a733_imx214_mode_io *io,
                             const void *bytes, size_t size)
{
  if (!s) return -EINVAL;
  int ret;
  if (s->state != A733_IMX214_IDLE || s->error || s->retain_resources)
    return -EBUSY;
  if (!bytes || size > A733_IMX214_MODE_HEADER + A733_IMX214_MODE_MAX_OPS * 4)
    return -EMSGSIZE;
  /* Own the complete script BEFORE parsing or calling any external I/O.
   * A callback cannot mutate a later opcode into STREAM_ON after parsing.
   */
  uint8_t owned[A733_IMX214_MODE_HEADER + A733_IMX214_MODE_MAX_OPS * 4];
  memcpy(owned, bytes, size);
  struct a733_imx214_mode mode;
  ret = a733_imx214_mode_parse(owned, size, &mode);
  if (ret) return ret; /* Invalid package: zero I/O. */
  uint32_t start = io->now_ms(io->context);
  uint8_t data[2];
  s->stage = "verify-identity-before-any-write";
  ret = read_bytes(s, io, 0x0016, data, 2, 30);
  if (ret) return ret;
  if (data[0] != 2 || data[1] != 0x14) return fail(s, -ENODEV);
  s->stage = "require-existing-standby";
  ret = read_bytes(s, io, 0x0100, data, 1, 30);
  if (ret) return ret;
  if (data[0]) return -EBUSY; /* Never hijack somebody else's stream. */
  s->stage = "write-mode-without-start";
  for (uint32_t i = 0; i < mode.count; ++i)
    {
      uint32_t elapsed = io->now_ms(io->context) - start;
      if (elapsed >= 4970) return fail(s, -ETIMEDOUT);
      const uint8_t *op = mode.ops + i * 4;
      uint16_t reg = le16(op + 2);
      if (op[0] == 1) ret = write_byte(s, io, reg, op[1], 30);
      else
        {
          if (!io->delay_ms) return fail(s, -ENOSYS);
          if (io->now_ms(io->context) - start >= 5000u - reg - 30u)
            return fail(s, -ETIMEDOUT);
          uint32_t before = io->now_ms(io->context);
          ret = io->delay_ms(io->context, reg);
          ret = checked(s, io, before, reg + 30, ret);
          if (!ret && io->now_ms(io->context) - before < reg)
            return fail(s, -EPROTO);
        }
      if (ret) return ret;
    }
  s->stage = "readback-mode-and-standby";
  for (unsigned i = 0; i < sizeof(check_values); ++i)
    {
      if (io->now_ms(io->context) - start >= 4970)
        return fail(s, -ETIMEDOUT);
      ret = read_bytes(s, io, check_regs[i], data, 1, 30);
      if (ret) return ret;
      if (data[0] != check_values[i]) return fail(s, -EIO);
      ++s->verified_registers;
    }
  if (io->now_ms(io->context) - start >= 4970)
    return fail(s, -ETIMEDOUT);
  ret = read_bytes(s, io, 0x0100, data, 1, 30);
  if (ret) return ret;
  if (data[0] || io->now_ms(io->context) - start >= 5000)
    return fail(s, data[0] ? -EIO : -ETIMEDOUT);
  s->state = A733_IMX214_PREPARED;
  s->stage = "sensor-prepared-NOT-camera-capture";
  return 0;
}

int a733_imx214_mode_prepare(struct a733_imx214_session *s,
                             const struct a733_imx214_mode_io *io,
                             const void *bytes,size_t size)
{
  int ret=gate(io,true);
  return ret ? ret : prepare_impl(s,io,bytes,size);
}

int a733_imx214_mode_trial_prepare(struct a733_imx214_session *s,
  const struct a733_imx214_trial_mode_io *trial,const void *bytes,size_t size)
{
  if (!trial || !trial->read || !trial->write8 || !trial->delay_ms ||
      !trial->now_ms) return -EINVAL;
  /* Deliberately leave all strict qualification/receiver fields FALSE. */
  const struct a733_imx214_mode_io io={.read=trial->read,.write8=trial->write8,
    .delay_ms=trial->delay_ms,.now_ms=trial->now_ms,.context=trial->context};
  return prepare_impl(s,&io,bytes,size);
}

int a733_imx214_mode_start(struct a733_imx214_session *s,
                           const struct a733_imx214_mode_io *io)
{
  if (!s) return -EINVAL;
  int ret = gate(io, true);
  if (ret) return ret;
  if (s->state != A733_IMX214_PREPARED || s->error || s->retain_resources)
    return -EBUSY;
  if (!io->receiver_ready || !io->receiver_stop) return -ENOSYS;
  s->stage = "receiver-must-be-armed-before-stream-on";
  uint32_t before = io->now_ms(io->context);
  ret = io->receiver_ready(io->context, 100);
  /* A clean 'not ready' rejection does not write to the sensor. A late or
   * otherwise failed callback may own buffers, so retain conservatively.
   */
  if (ret == -EBUSY && io->now_ms(io->context) - before <= 100) return ret;
  ret = checked(s, io, before, 100, ret);
  if (ret) return ret;
  s->retain_resources = true; s->stop_attempted = false;
  ret = write_byte(s, io, 0x0100, 1, 30);
  if (ret) return ret;
  uint8_t data;
  ret = read_bytes(s, io, 0x0100, &data, 1, 30);
  if (ret) return ret;
  if (data != 1) return fail(s, -EIO);
  s->state = A733_IMX214_STREAMING;
  s->stage = "stream-enabled-NOT-frame-complete";
  return 0;
}

int a733_imx214_mode_stop(struct a733_imx214_session *s,
                          const struct a733_imx214_mode_io *io)
{
  if (!s) return -EINVAL;
  /* Fixed standby/idle cleanup must remain possible if the mode artifact
   * loses its trust qualification after a stream was already started.
   */
  int ret = gate(io, false);
  if (ret) return ret;
  if (!s->retain_resources) return s->state == A733_IMX214_PREPARED ? 0 : -EINVAL;
  if (s->stop_attempted) return -EBUSY; /* No repeated cleanup/reset loop. */
  s->stop_attempted = true;
  s->stage = "stop-sensor-then-prove-receiver-idle";
  ret = write_byte(s, io, 0x0100, 0, 30);
  uint8_t data;
  if (!ret) ret = read_bytes(s, io, 0x0100, &data, 1, 30);
  if (!ret && data) ret = fail(s, -EIO);
  if (!ret)
    {
      if (!io->receiver_stop) ret = fail(s, -ENOSYS);
      else
        {
          uint32_t before = io->now_ms(io->context);
          ret = io->receiver_stop(io->context, 100);
          ret = checked(s, io, before, 100, ret);
        }
    }
  s->stop_error = ret;
  if (ret) return ret;
  s->retain_resources = false;
  s->state = s->error ? A733_IMX214_FAULT : A733_IMX214_PREPARED;
  s->stage = "sensor-standby-receiver-idle";
  return 0;
}
