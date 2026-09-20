# SPDX-License-Identifier: Apache-2.0

set(SRCS
    rk3506_boot.c
    rk3506_memorymap.c
    rk3506_irq.c
    rk3506_timer.c
    rk3506_serial.c
    rk3506_lowputc.c
    rk3506_gpio.c
    rk3506_i2c.c
    rk3506_spi.c
    rk3506_sdmmc.c)

target_sources(arch PRIVATE ${SRCS})
target_include_directories(arch PRIVATE ${NUTTX_CHIP_ABS_DIR})
