# ##############################################################################
# apps/system/a733display/CMakeLists.txt
# ##############################################################################

if(CONFIG_SYSTEM_A733DISPLAY)
  nuttx_add_application(
    NAME
    ${CONFIG_A733DISPLAY_PROGNAME}
    SRCS
    a733display_main.c
    STACKSIZE
    ${CONFIG_A733DISPLAY_STACKSIZE}
    PRIORITY
    ${CONFIG_A733DISPLAY_PRIORITY})
endif()
