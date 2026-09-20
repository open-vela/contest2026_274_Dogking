/****************************************************************************
 * vendor/rockchip/chips/rk3506/rk3506_i2c.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_RK3506_I2C2

#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <debug.h>

#include <nuttx/arch.h>
#include <nuttx/i2c/i2c_master.h>
#include <nuttx/mutex.h>

#include <arch/chip/rk3506_i2c.h>

#include "arm_internal.h"

/* RK3506 I2C2 integration.  The Zero W device tree routes I2C2 through
 * Rockchip Matrix IO to GPIO0_A0 (SDA) and GPIO0_A1 (SCL).  The generic
 * Rockchip RTOS pin defaults use different pins and must not be copied.
 */

#define RK3506_I2C2_BASE              0xff060000u
#define RK3506_CRU_BASE               0xff9a0000u
#define RK3506_CRU_PMU_BASE           0xff9b0000u
#define RK3506_GPIO0_IOC_BASE         0xff950000u
#define RK3506_RM_IO_BASE             0xff910000u

#define RK3506_CRU_CLKSEL_CON33       (RK3506_CRU_BASE + 0x0384u)
#define RK3506_CRU_GATE_CON12         (RK3506_CRU_BASE + 0x0830u)
#define RK3506_CRU_PMU_GATE_CON00     (RK3506_CRU_PMU_BASE + 0x0800u)

#define RK3506_GPIO0A_IOMUX_SEL_0     (RK3506_GPIO0_IOC_BASE + 0x0000u)
#define RK3506_GPIO0A_PULL            (RK3506_GPIO0_IOC_BASE + 0x0200u)
#define RK3506_GPIO0A_SMT             (RK3506_GPIO0_IOC_BASE + 0x0400u)
#define RK3506_RM_GPIO0A0_SEL         (RK3506_RM_IO_BASE + 0x0080u)
#define RK3506_RM_GPIO0A1_SEL         (RK3506_RM_IO_BASE + 0x0084u)

#define RK3506_WRITE_MASK(mask, val)  \
  (((uint32_t)(mask) << 16) | ((uint32_t)(val) & (mask)))

/* Controller registers */

#define I2C_CON                       (RK3506_I2C2_BASE + 0x0000u)
#define I2C_CLKDIV                    (RK3506_I2C2_BASE + 0x0004u)
#define I2C_MRXADDR                   (RK3506_I2C2_BASE + 0x0008u)
#define I2C_MRXRADDR                  (RK3506_I2C2_BASE + 0x000cu)
#define I2C_MTXCNT                    (RK3506_I2C2_BASE + 0x0010u)
#define I2C_MRXCNT                    (RK3506_I2C2_BASE + 0x0014u)
#define I2C_IEN                       (RK3506_I2C2_BASE + 0x0018u)
#define I2C_IPD                       (RK3506_I2C2_BASE + 0x001cu)
#define I2C_TXDATA(n)                 (RK3506_I2C2_BASE + 0x0100u + 4u * (n))
#define I2C_RXDATA(n)                 (RK3506_I2C2_BASE + 0x0200u + 4u * (n))

#define I2C_CON_EN                    (1u << 0)
#define I2C_CON_MODE_SHIFT            1
#define I2C_CON_MODE_MASK             (3u << I2C_CON_MODE_SHIFT)
#define I2C_CON_START                 (1u << 3)
#define I2C_CON_STOP                  (1u << 4)
#define I2C_CON_LAST_NACK             (1u << 5)
#define I2C_CON_STOP_ON_NACK          (1u << 6)
#define I2C_CON_TUNING_MASK           (0xffu << 8)
#define I2C_CON_SDA_CFG(v)            ((uint32_t)(v) << 8)
#define I2C_CON_START_CFG(v)          ((uint32_t)(v) << 12)

#define I2C_MODE_TX                   0
#define I2C_MODE_REGISTER_TX          1
#define I2C_MODE_RX                   2

#define I2C_INT_BTF                   (1u << 0)
#define I2C_INT_BRF                   (1u << 1)
#define I2C_INT_MBTF                  (1u << 2)
#define I2C_INT_MBRF                  (1u << 3)
#define I2C_INT_START                 (1u << 4)
#define I2C_INT_STOP                  (1u << 5)
#define I2C_INT_NACK                  (1u << 6)
#define I2C_INT_ALL                   0xffu

#define I2C_ADDR_VALID(n)             (1u << (24 + (n)))
#define I2C_INPUT_CLOCK               24000000u
#define I2C_DEFAULT_FREQUENCY         100000u
#define I2C_POLL_STEP_US              5u
#define I2C_BASE_TIMEOUT_US           20000u
#define I2C_BYTE_TIMEOUT_US           200u

enum rk3506_i2c_state_e
{
  RK3506_I2C_IDLE = 0,
  RK3506_I2C_START,
  RK3506_I2C_READ,
  RK3506_I2C_WRITE,
  RK3506_I2C_STOP
};

struct rk3506_i2cdev_s
{
  struct i2c_master_s dev;
  mutex_t lock;
  enum rk3506_i2c_state_e state;
  FAR struct i2c_msg_s *msg;
  size_t processed;
  uint32_t tuning;
  uint32_t frequency;
  uint8_t mode;
  int error;
  bool last;
};

static int rk3506_i2c_transfer(FAR struct i2c_master_s *dev,
                               FAR struct i2c_msg_s *msgs, int count);
static int rk3506_i2c_setup(FAR struct i2c_master_s *dev);
static int rk3506_i2c_shutdown(FAR struct i2c_master_s *dev);

static const struct i2c_ops_s g_rk3506_i2c_ops =
{
  .transfer = rk3506_i2c_transfer,
#ifdef CONFIG_I2C_RESET
  .reset = NULL,
#endif
  .setup = rk3506_i2c_setup,
  .shutdown = rk3506_i2c_shutdown,
};

static struct rk3506_i2cdev_s g_i2c2 =
{
  .dev = { .ops = &g_rk3506_i2c_ops },
  .lock = NXMUTEX_INITIALIZER,
  .state = RK3506_I2C_IDLE,
  .frequency = I2C_DEFAULT_FREQUENCY,
};

static uint32_t rk3506_div_round_up(uint32_t a, uint32_t b)
{
  return (a + b - 1) / b;
}

static void rk3506_i2c_setfrequency(struct rk3506_i2cdev_s *priv,
                                    uint32_t frequency)
{
  uint32_t ratekhz;
  uint32_t speedkhz;
  uint32_t total;
  uint32_t high;
  uint32_t low;
  uint32_t hold;
  uint32_t extra;
  uint32_t extralow;
  uint32_t minhighns;
  uint32_t minlowns;
  uint32_t startsetup;

  /* Standard and fast mode are supported.  Clamp other requests to the
   * electrically conservative 100 kHz mode used for initial board bring-up.
   */

  if (frequency > I2C_SPEED_STANDARD && frequency <= I2C_SPEED_FAST)
    {
      speedkhz = 400;
      minhighns = 900;  /* 600 ns high + 300 ns rise */
      minlowns = 1600;  /* 1300 ns low + 300 ns fall */
      startsetup = 0;
      frequency = I2C_SPEED_FAST;
    }
  else
    {
      speedkhz = 100;
      minhighns = 5000; /* 4000 ns high + 1000 ns rise */
      minlowns = 5000;  /* 4700 ns low + 300 ns fall */
      startsetup = 1;
      frequency = I2C_SPEED_STANDARD;
    }

  if (priv->frequency == frequency && priv->tuning != 0)
    {
      return;
    }

  ratekhz = rk3506_div_round_up(I2C_INPUT_CLOCK, 1000);
  total = rk3506_div_round_up(ratekhz, speedkhz * 8);
  high = rk3506_div_round_up(ratekhz * minhighns, 8000000);
  low = rk3506_div_round_up(ratekhz * minlowns, 8000000);
  high = high < 2 ? 2 : high;
  low = low < 2 ? 2 : low;
  hold = high + low;

  if (hold < total)
    {
      extra = total - hold;
      extralow = rk3506_div_round_up(low * extra, hold);
      low += extralow;
      high += extra - extralow;
    }

  priv->tuning = I2C_CON_SDA_CFG(1) | I2C_CON_START_CFG(startsetup);
  priv->frequency = frequency;
  putreg32(((high - 1) << 16) | (low - 1), I2C_CLKDIV);
  putreg32(priv->tuning, I2C_CON);
}

static void rk3506_i2c_stop(struct rk3506_i2cdev_s *priv, int error)
{
  uint32_t con;

  priv->processed = 0;
  priv->error = error;

  if (priv->last || error != 0)
    {
      priv->state = RK3506_I2C_STOP;
      con = getreg32(I2C_CON) | I2C_CON_STOP;
      putreg32(con, I2C_CON);
    }
  else
    {
      putreg32(getreg32(I2C_CON) & I2C_CON_TUNING_MASK, I2C_CON);
      priv->state = RK3506_I2C_IDLE;
    }
}

static void rk3506_i2c_prepare_read(struct rk3506_i2cdev_s *priv)
{
  size_t len = priv->msg->length - priv->processed;
  uint32_t con = getreg32(I2C_CON);

  if (len > 32)
    {
      len = 32;
      con &= ~I2C_CON_LAST_NACK;
    }
  else
    {
      con |= I2C_CON_LAST_NACK;
    }

  if (priv->processed != 0)
    {
      con &= ~I2C_CON_MODE_MASK;
      con |= I2C_MODE_RX << I2C_CON_MODE_SHIFT;
    }

  putreg32(con, I2C_CON);
  putreg32((uint32_t)len, I2C_MRXCNT);
}

static void rk3506_i2c_fill_tx(struct rk3506_i2cdev_s *priv)
{
  unsigned int i;
  unsigned int j;
  uint32_t value;
  uint32_t count = 0;
  uint8_t byte;

  for (i = 0; i < 8; i++)
    {
      value = 0;
      for (j = 0; j < 4; j++)
        {
          if (priv->processed == (size_t)priv->msg->length && count != 0)
            {
              break;
            }

          if (priv->processed == 0 && count == 0)
            {
              byte = (priv->msg->addr & 0x7f) << 1;
            }
          else
            {
              byte = priv->msg->buffer[priv->processed++];
            }

          value |= (uint32_t)byte << (j * 8);
          count++;
        }

      putreg32(value, I2C_TXDATA(i));
      if (priv->processed == (size_t)priv->msg->length)
        {
          break;
        }
    }

  putreg32(count, I2C_MTXCNT);
}

static int rk3506_i2c_poll(struct rk3506_i2cdev_s *priv)
{
  uint32_t ipd = getreg32(I2C_IPD) & I2C_INT_ALL;
  uint32_t con;
  size_t len;
  size_t i;
  uint32_t value = 0;

  if ((ipd & (I2C_INT_BRF | I2C_INT_BTF)) != 0)
    {
      putreg32(ipd & (I2C_INT_BRF | I2C_INT_BTF), I2C_IPD);
      ipd &= ~(I2C_INT_BRF | I2C_INT_BTF);
    }

  if ((ipd & I2C_INT_NACK) != 0)
    {
      putreg32(I2C_INT_NACK, I2C_IPD);
      rk3506_i2c_stop(priv, -ENXIO);
      return -EINPROGRESS;
    }

  switch (priv->state)
    {
      case RK3506_I2C_START:
        if ((ipd & I2C_INT_START) == 0)
          {
            return -EINPROGRESS;
          }

        putreg32(I2C_INT_START, I2C_IPD);
        putreg32(getreg32(I2C_CON) & ~I2C_CON_START, I2C_CON);
        if (priv->mode == I2C_MODE_TX)
          {
            priv->state = RK3506_I2C_WRITE;
            rk3506_i2c_fill_tx(priv);
          }
        else
          {
            priv->state = RK3506_I2C_READ;
            rk3506_i2c_prepare_read(priv);
          }
        break;

      case RK3506_I2C_WRITE:
        if ((ipd & I2C_INT_MBTF) == 0)
          {
            return -EINPROGRESS;
          }

        putreg32(I2C_INT_MBTF, I2C_IPD);
        if (priv->processed == (size_t)priv->msg->length)
          {
            rk3506_i2c_stop(priv, 0);
          }
        else
          {
            rk3506_i2c_fill_tx(priv);
          }
        break;

      case RK3506_I2C_READ:
        if ((ipd & I2C_INT_MBRF) == 0)
          {
            return -EINPROGRESS;
          }

        putreg32(I2C_INT_MBRF, I2C_IPD);
        len = priv->msg->length - priv->processed;
        len = len > 32 ? 32 : len;
        for (i = 0; i < len; i++)
          {
            if ((i & 3) == 0)
              {
                value = getreg32(I2C_RXDATA(i / 4));
              }

            priv->msg->buffer[priv->processed++] =
              (value >> ((i & 3) * 8)) & 0xff;
          }

        if (priv->processed == (size_t)priv->msg->length)
          {
            rk3506_i2c_stop(priv, 0);
          }
        else
          {
            rk3506_i2c_prepare_read(priv);
          }
        break;

      case RK3506_I2C_STOP:
        if ((ipd & I2C_INT_STOP) == 0)
          {
            return -EINPROGRESS;
          }

        putreg32(I2C_INT_STOP, I2C_IPD);
        con = getreg32(I2C_CON) & ~I2C_CON_STOP;
        putreg32(con, I2C_CON);
        priv->state = RK3506_I2C_IDLE;
        return priv->error;

      case RK3506_I2C_IDLE:
        return priv->error;
    }

  return priv->state == RK3506_I2C_IDLE ? priv->error : -EINPROGRESS;
}

static int rk3506_i2c_startmsg(struct rk3506_i2cdev_s *priv,
                               FAR struct i2c_msg_s *msgs, int count,
                               FAR int *consumed)
{
  FAR struct i2c_msg_s *msg = &msgs[0];
  uint32_t addr;
  uint32_t regaddr = 0;
  uint32_t con;
  uint32_t timeout;
  int i;
  int ret;

  *consumed = 1;
  if ((msg->flags & (I2C_M_TEN | I2C_M_NOSTART)) != 0 ||
      msg->length < 0 || (msg->length != 0 && msg->buffer == NULL))
    {
      return -ENOTSUP;
    }

  rk3506_i2c_setfrequency(priv, msg->frequency);
  priv->msg = msg;
  priv->mode = I2C_MODE_TX;

  /* The RK3x hardware implements the common short register-address write
   * followed by read as one transaction.  This is the only reliable true
   * repeated-start form exposed by this controller.
   */

  if (count >= 2 && msg->length < 4 &&
      (msg->flags & I2C_M_READ) == 0 &&
      (msgs[1].flags & I2C_M_READ) != 0 &&
      msgs[1].addr == msg->addr &&
      (msgs[1].flags & (I2C_M_TEN | I2C_M_NOSTART)) == 0)
    {
      for (i = 0; i < msg->length; i++)
        {
          regaddr |= (uint32_t)msg->buffer[i] << (i * 8);
          regaddr |= I2C_ADDR_VALID(i);
        }

      addr = ((msg->addr & 0x7f) << 1) | I2C_ADDR_VALID(0);
      putreg32(addr, I2C_MRXADDR);
      putreg32(regaddr, I2C_MRXRADDR);
      priv->msg = &msgs[1];
      priv->mode = I2C_MODE_REGISTER_TX;
      *consumed = 2;
    }
  else if ((msg->flags & I2C_M_READ) != 0)
    {
      addr = ((msg->addr & 0x7f) << 1) | 1u | I2C_ADDR_VALID(0);
      putreg32(addr, I2C_MRXADDR);
      putreg32(0, I2C_MRXRADDR);
      priv->mode = I2C_MODE_REGISTER_TX;
    }

  priv->processed = 0;
  priv->error = 0;
  priv->last = *consumed == count;
  priv->state = RK3506_I2C_START;
  putreg32(I2C_INT_ALL, I2C_IPD);
  con = priv->tuning | I2C_CON_EN |
        ((uint32_t)priv->mode << I2C_CON_MODE_SHIFT) |
        I2C_CON_START | I2C_CON_STOP_ON_NACK;
  putreg32(con, I2C_CON);

  timeout = I2C_BASE_TIMEOUT_US +
            (uint32_t)priv->msg->length * I2C_BYTE_TIMEOUT_US;
  do
    {
      ret = rk3506_i2c_poll(priv);
      if (ret != -EINPROGRESS)
        {
          return ret;
        }

      up_udelay(I2C_POLL_STEP_US);
      if (timeout <= I2C_POLL_STEP_US)
        {
          break;
        }

      timeout -= I2C_POLL_STEP_US;
    }
  while (true);

  putreg32(0, I2C_IEN);
  putreg32(I2C_INT_ALL, I2C_IPD);
  putreg32(priv->tuning, I2C_CON);
  priv->state = RK3506_I2C_IDLE;
  return -ETIMEDOUT;
}

static int rk3506_i2c_transfer(FAR struct i2c_master_s *dev,
                               FAR struct i2c_msg_s *msgs, int count)
{
  FAR struct rk3506_i2cdev_s *priv =
    (FAR struct rk3506_i2cdev_s *)dev;
  int consumed;
  int index;
  int ret;

  if (msgs == NULL || count <= 0)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  for (index = 0; index < count; index += consumed)
    {
      ret = rk3506_i2c_startmsg(priv, &msgs[index], count - index,
                                &consumed);
      if (ret < 0)
        {
          break;
        }
    }

  putreg32(0, I2C_IEN);
  putreg32(priv->tuning, I2C_CON);
  nxmutex_unlock(&priv->lock);
  return ret;
}

static int rk3506_i2c_setup(FAR struct i2c_master_s *dev)
{
  return 0;
}

static int rk3506_i2c_shutdown(FAR struct i2c_master_s *dev)
{
  return 0;
}

FAR struct i2c_master_s *rk3506_i2cbus_initialize(int bus)
{
  if (bus != 2)
    {
      return NULL;
    }

  /* Select the 24 MHz oscillator with divider one and enable I2C2 clocks.
   * Rockchip clock gates are active high: writing zero enables the clock.
   */

  putreg32(RK3506_WRITE_MASK(0x3fu, 0), RK3506_CRU_CLKSEL_CON33);
  putreg32(RK3506_WRITE_MASK((1u << 2) | (1u << 3), 0),
           RK3506_CRU_GATE_CON12);
  putreg32(RK3506_WRITE_MASK(1u << 7, 0),
           RK3506_CRU_PMU_GATE_CON00);

  /* Function 7 selects RMIO.  RMIO function 0x14 is I2C2 SDA and 0x13 is
   * I2C2 SCL.  Enable weak internal pull-ups and Schmitt inputs as in the
   * official Zero W device tree's pull-up pin configuration.
   */

  putreg32(RK3506_WRITE_MASK(0x00ffu, 0x0077u),
           RK3506_GPIO0A_IOMUX_SEL_0);
  putreg32(RK3506_WRITE_MASK(0x007fu, 0x0014u),
           RK3506_RM_GPIO0A0_SEL);
  putreg32(RK3506_WRITE_MASK(0x007fu, 0x0013u),
           RK3506_RM_GPIO0A1_SEL);
  putreg32(RK3506_WRITE_MASK(0x000fu, 0x0005u),
           RK3506_GPIO0A_PULL);
  putreg32(RK3506_WRITE_MASK(0x0003u, 0x0003u),
           RK3506_GPIO0A_SMT);

  putreg32(0, I2C_IEN);
  putreg32(I2C_INT_ALL, I2C_IPD);
  g_i2c2.tuning = 0;
  rk3506_i2c_setfrequency(&g_i2c2, I2C_DEFAULT_FREQUENCY);
  return &g_i2c2.dev;
}

#endif /* CONFIG_RK3506_I2C2 */
