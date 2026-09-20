/****************************************************************************
 * vendor/rockchip/boards/rv1103/luckfox-pico-mini/include/board.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __VENDOR_ROCKCHIP_BOARDS_RV1103_LUCKFOX_PICO_MINI_INCLUDE_BOARD_H
#define __VENDOR_ROCKCHIP_BOARDS_RV1103_LUCKFOX_PICO_MINI_INCLUDE_BOARD_H

#define BOARD_UART2_CLOCK      24000000
#define BOARD_UART2_BAUD       115200

#define BOARD_WORK_LED_PIN     22 /* GPIO3_C6, active high */

#define BOARD_SDMMC_FREQUENCY       24000000
#define BOARD_CLKDIV_INIT           30 /* 24 MHz / (2 * 30) = 400 kHz */
#define BOARD_CLKDIV_MMCXFR          1 /* 24 MHz / (2 * 1)  = 12 MHz */
#define BOARD_CLKDIV_SDWIDEXFR       1
#define BOARD_CLKDIV_SDXFR           1

#ifdef CONFIG_DEV_GPIO
int luckfox_gpio_initialize(void);
#endif

#ifdef CONFIG_RV1103_WATCHDOG
int rv1103_wdt_initialize(void);
#endif

int luckfox_peripherals_initialize(void);

#ifdef CONFIG_RV1103_SC3336_CHECKPOINT
int luckfox_sc3336_initialize(void);
#endif

#ifdef CONFIG_RV1103_SDMMC
int luckfox_sdmmc_initialize(void);
#endif

#endif
