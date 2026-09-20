/****************************************************************************
 * vendor/allwinnertech/chips/a733/include/a733_peripherals.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __VENDOR_ALLWINNERTECH_CHIPS_A733_INCLUDE_A733_PERIPHERALS_H
#define __VENDOR_ALLWINNERTECH_CHIPS_A733_INCLUDE_A733_PERIPHERALS_H

struct spi_dev_s;

int a733_watchdog_initialize(void);
int a733_tsadc_initialize(void);
int a733_hwdiag_initialize(void);
int a733_trng_initialize(void);
int a733_wifi_usb_initialize(void);
int a733_usb_camera_initialize(void);
int a733_sdmmc0_initialize(void);
int a733_header_peripherals_initialize(void);
struct spi_dev_s *a733_spibus_initialize(int bus);
int a733_npu_initialize(void);
int a733_uart4_initialize(void);
int a733_i2s0_audio_initialize(void);

#endif
