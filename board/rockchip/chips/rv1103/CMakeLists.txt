# SPDX-License-Identifier: Apache-2.0

set(SRCS
    rv1103_boot.c
    rv1103_memorymap.c
    rv1103_irq.c
    rv1103_timer.c
    rv1103_gpio.c
    rv1103_periph.c
    rv1103_i2c.c
    rv1103_spi.c
    rv1103_pwm.c
    rv1103_saradc.c
    rv1103_wdt.c
    rv1103_tsadc.c
    rv1103_trng.c
    rv1103_dma.c
    rv1103_sfc_nand.c
    rv1103_sdmmc.c
    rv1103_usb.c
    rv1103_usb_cdc.c
    rv1103_serial.c
    rv1103_lowputc.c)

target_sources(arch PRIVATE ${SRCS})
target_include_directories(arch PRIVATE ${NUTTX_CHIP_ABS_DIR})
