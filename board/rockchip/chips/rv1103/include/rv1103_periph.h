/****************************************************************************
 * vendor/rockchip/chips/rv1103/include/rv1103_periph.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __VENDOR_ROCKCHIP_CHIPS_RV1103_INCLUDE_RV1103_PERIPH_H
#define __VENDOR_ROCKCHIP_CHIPS_RV1103_INCLUDE_RV1103_PERIPH_H

#include <nuttx/config.h>
#include <stdbool.h>
#include <stdint.h>

enum rv1103_pull_e
{
  RV1103_PULL_NONE = 0,
  RV1103_PULL_UP   = 1,
  RV1103_PULL_DOWN = 2
};

int rv1103_pinctrl_config(unsigned int bank, unsigned int pin,
                          unsigned int function, enum rv1103_pull_e pull,
                          bool schmitt);
void rv1103_periph_clock_i2c(int bus);
void rv1103_periph_clock_spi(int bus);
void rv1103_periph_clock_pwm(int block);
void rv1103_periph_clock_saradc(void);
void rv1103_periph_clock_uart(int uart);
void rv1103_extended_uart_setup(void);

#ifdef CONFIG_RV1103_I2C
struct i2c_master_s;
FAR struct i2c_master_s *rv1103_i2cbus_initialize(int bus);
#endif

#ifdef CONFIG_RV1103_SPI
struct spi_dev_s;
FAR struct spi_dev_s *rv1103_spibus_initialize(int bus);
#endif

#ifdef CONFIG_RV1103_PWM
struct pwm_lowerhalf_s;
FAR struct pwm_lowerhalf_s *rv1103_pwminitialize(int channel);
#endif

#ifdef CONFIG_RV1103_SARADC
uint16_t rv1103_saradc_read(unsigned int channel);
int rv1103_saradc_initialize(void);
#endif

#endif
