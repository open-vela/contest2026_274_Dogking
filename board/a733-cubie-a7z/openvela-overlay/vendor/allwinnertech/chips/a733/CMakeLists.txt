# SPDX-License-Identifier: Apache-2.0

set(SRCS
    a733_boot.c
    a733_serial.c
    a733_image_header.S)

if(CONFIG_A733_WATCHDOG)
  list(APPEND SRCS a733_watchdog.c)
endif()

if(CONFIG_A733_TSADC)
  list(APPEND SRCS a733_tsadc.c)
endif()

if(CONFIG_A733_HARDWARE_DIAG)
  list(APPEND SRCS a733_hwdiag.c)
endif()

if(CONFIG_A733_TRNG)
  list(APPEND SRCS a733_trng.c)
endif()

if(CONFIG_A733_NPU_CHECKPOINT)
  list(APPEND SRCS a733_npu.c a733_npu_memory.c a733_npu_runtime.c)
endif()

if(CONFIG_A733_WIFI_USB_CHECKPOINT)
  list(APPEND SRCS a733_wifi_usb.c)
  target_include_directories(arch PRIVATE ${NUTTX_APPS_DIR}/include)
endif()

if(CONFIG_A733_USB_CAMERA)
  list(APPEND SRCS a733_usb_camera.c)
endif()

if(CONFIG_A733_SDMMC0)
  list(APPEND SRCS a733_sdmmc.c)
endif()

if(CONFIG_A733_HEADER_I2C OR CONFIG_A733_HEADER_SPI OR CONFIG_A733_FAN_PWM)
  list(APPEND SRCS a733_header_peripherals.c)
endif()

if(CONFIG_A733_UART4)
  list(APPEND SRCS a733_uart4.c)
endif()

if(CONFIG_A733_I2S0_AUDIO)
  list(APPEND SRCS a733_i2s0_audio.c)
endif()

if(CONFIG_ARCH_EARLY_PRINT)
  list(APPEND SRCS a733_lowputc.S)
endif()

target_sources(arch PRIVATE ${SRCS})
target_include_directories(arch PRIVATE ${NUTTX_CHIP_ABS_DIR}/include)
