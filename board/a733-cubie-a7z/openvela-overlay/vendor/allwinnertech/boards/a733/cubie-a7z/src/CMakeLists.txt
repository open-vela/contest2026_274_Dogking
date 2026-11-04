# SPDX-License-Identifier: Apache-2.0

set(SRCS a7z_boardinit.c)

if(CONFIG_BOARDCTL_RESET OR CONFIG_BOARDCTL_POWEROFF)
  list(APPEND SRCS a7z_power.c)
endif()

if(CONFIG_BOARD_A7Z_USER_LED)
  list(APPEND SRCS a7z_gpio.c)
endif()

if(CONFIG_BOARD_A7Z_STORAGE)
  list(APPEND SRCS a7z_storage.c)
endif()

if(CONFIG_BOARD_A7Z_ST7735)
  list(APPEND SRCS a7z_st7735.c)
endif()

target_sources(board PRIVATE ${SRCS})
