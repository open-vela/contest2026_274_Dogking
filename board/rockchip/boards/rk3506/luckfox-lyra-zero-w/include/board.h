/****************************************************************************
 * vendor/rockchip/boards/rk3506/luckfox-lyra-zero-w/include/board.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __VENDOR_ROCKCHIP_BOARDS_RK3506_LUCKFOX_LYRA_ZERO_W_INCLUDE_BOARD_H
#define __VENDOR_ROCKCHIP_BOARDS_RK3506_LUCKFOX_LYRA_ZERO_W_INCLUDE_BOARD_H

#define BOARD_UART0_CLOCK      24000000
#define BOARD_UART0_BAUD       1500000

#define BOARD_ACT_LED_PIN      0 /* GPIO1_A0, active high */

#define BOARD_SDMMC_FREQUENCY       52000000
#define BOARD_SDMMC_IDENT_TARGET      400000
#define BOARD_SDMMC_TRANSFER_TARGET 25000000
#define BOARD_SDMMC_SAFE_TARGET     12500000
#define BOARD_SDMMC_CEIL(a,b)       (((a) + (b) - 1) / (b))

/* CLKDIV contains half of the source/target ratio because the controller
 * divides the CIU clock by 2*n.  A 52 MHz source and the 25 MHz target below
 * therefore select n=2 and produce an actual 13 MHz card clock.  This stays
 * below the 25 MHz SD default-speed limit; SD high-speed CMD6 negotiation is
 * not implemented by the generic mmcsd layer yet.  BOARD_SDMMC_SAFE_TARGET
 * documents the previous, approximately 8.67 MHz rollback setting.
 */

#define BOARD_CLKDIV_INIT      \
  BOARD_SDMMC_CEIL(BOARD_SDMMC_FREQUENCY, BOARD_SDMMC_IDENT_TARGET)
#define BOARD_CLKDIV_MMCXFR    \
  BOARD_SDMMC_CEIL(BOARD_SDMMC_FREQUENCY, BOARD_SDMMC_TRANSFER_TARGET)
#define BOARD_CLKDIV_SDWIDEXFR \
  BOARD_SDMMC_CEIL(BOARD_SDMMC_FREQUENCY, BOARD_SDMMC_TRANSFER_TARGET)
#define BOARD_CLKDIV_SDXFR     \
  BOARD_SDMMC_CEIL(BOARD_SDMMC_FREQUENCY, BOARD_SDMMC_TRANSFER_TARGET)

#ifdef CONFIG_RK3506_SDMMC
int luckfox_sdmmc_initialize(void);
#endif

#ifdef CONFIG_DEV_GPIO
int luckfox_gpio_initialize(void);
#endif

#ifdef CONFIG_RK3506_I2C2
int luckfox_i2c_initialize(void);
#endif

#ifdef CONFIG_RK3506_SPI0
int luckfox_spi_initialize(void);
#endif

#ifdef CONFIG_RK3506_AIC8800DC_PROBE
int luckfox_wifi_probe_initialize(void);
#endif

#endif
