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
  if(CONFIG_A733_CAMERA_PERF_TEST)
    # Keep floating-point ordering/NaN semantics. No fast-math or global ISA.
    # arch is created in the parent directory; source properties must be
    # attached to THAT target's directory with the absolute source path.
    set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/a733_npu_runtime.c"
      TARGET_DIRECTORY arch PROPERTIES COMPILE_OPTIONS "-O2")
  endif()
endif()

if(CONFIG_A733_NPU_TENSOR_IO)
  list(APPEND SRCS a733_tensor_plan.c)
endif()

if(CONFIG_A733_WIFI_USB_CHECKPOINT)
  list(APPEND SRCS a733_wifi_usb.c)
  target_include_directories(arch PRIVATE ${NUTTX_APPS_DIR}/include)
endif()

if(CONFIG_A733_USB_CAMERA)
  list(APPEND SRCS a733_usb_camera.c)
endif()

if(CONFIG_A733_IMX214_CHECKPOINT)
  list(APPEND SRCS a733_camera_board.c a733_camera_diag.c
                   a733_imx214_probe.c a733_imx214_twi3.c
                   a733_imx214_mode.c a733_csi_frame_plan.c
                   a733_imx214_cold.c)
endif()

if(CONFIG_A733_SDMMC0)
  list(APPEND SRCS a733_sdmmc.c)
endif()

if(CONFIG_A733_CAMERA_CONTROL_TEST)
  list(APPEND SRCS a733_camera_control.c a733_mipi_rx.c a733_csi_capture.c)
endif()

if(CONFIG_A733_UFS_READONLY)
  list(APPEND SRCS a733_ufs.c a733_ufs_link.c a733_ufs_board.c a733_ufs_handoff.c a733_ufs_quiesce.c a733_ufs_nop.c a733_ufs_nop_map.c a733_ufs_readcheck.c a733_ufs_scratch.c a733_ufs_data.c)
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
