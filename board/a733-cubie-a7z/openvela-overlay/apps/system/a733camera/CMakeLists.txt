# SPDX-License-Identifier: Apache-2.0
if(CONFIG_SYSTEM_A733CAMERA)
  set(CAMERA_SRCS a733camera_main.c a733_camera_image.c)
  if(CONFIG_A733_CAMERA_PERF_TEST)
    # Local CPU pixel optimisation only: no fast-math, ISA or global flags.
    set_source_files_properties(a733_camera_image.c PROPERTIES COMPILE_OPTIONS "-O2")
  endif()
  if(CONFIG_A733_CAMERA_LIVE_TEST)
    list(APPEND CAMERA_SRCS a733_camera_live.c)
  endif()
  nuttx_add_application(NAME camera
    SRCS ${CAMERA_SRCS}
    STACKSIZE 8192 PRIORITY 100)
endif()
