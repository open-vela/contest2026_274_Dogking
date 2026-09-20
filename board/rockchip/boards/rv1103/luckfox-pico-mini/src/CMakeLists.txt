# SPDX-License-Identifier: Apache-2.0

target_sources(board PRIVATE luckfox_boardinit.c luckfox_gpio.c
                             luckfox_peripherals.c luckfox_sdmmc.c
                             luckfox_sc3336.c)

# Old incremental build directories predate CONFIG_VIDEO_STREAM and do not
# automatically refresh the drivers/video source list.  Keep them buildable;
# a fresh configuration uses the normal drivers/video CMake target instead.

if(NOT CONFIG_VIDEO_STREAM)
  target_sources(board PRIVATE ${NUTTX_DIR}/drivers/video/v4l2_core.c)
endif()
