# ##############################################################################
# apps/system/a733npu/CMakeLists.txt
# ##############################################################################

if(CONFIG_SYSTEM_A733NPU)
  nuttx_add_application(
    NAME
    ${CONFIG_A733NPU_PROGNAME}
    SRCS
    a733npu_main.c
    STACKSIZE
    ${CONFIG_A733NPU_STACKSIZE}
    PRIORITY
    ${CONFIG_A733NPU_PRIORITY})
endif()
