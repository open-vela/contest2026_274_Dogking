/****************************************************************************
 * vendor/allwinnertech/boards/a733/cubie-a7z/include/board.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __VENDOR_ALLWINNERTECH_BOARDS_A733_CUBIE_A7Z_INCLUDE_BOARD_H
#define __VENDOR_ALLWINNERTECH_BOARDS_A733_CUBIE_A7Z_INCLUDE_BOARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BOARD_LOOPSPERMSEC 100000

int a7z_gpio_initialize(void);
int a7z_storage_initialize(void);
bool a7z_st7735_is_ready(void);
int a7z_st7735_write_area(uint16_t x0, uint16_t y0,
                          uint16_t x1, uint16_t y1,
                          const uint16_t *pixels, size_t npixels);

#endif
