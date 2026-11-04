# ##############################################################################
# apps/system/a733wifi/CMakeLists.txt
# ##############################################################################

if(CONFIG_SYSTEM_A733WIFI)
  nuttx_add_application(
    NAME
    ${CONFIG_A733WIFI_PROGNAME}
    SRCS
    a733wifi_main.c
    STACKSIZE
    ${CONFIG_A733WIFI_STACKSIZE}
    PRIORITY
    ${CONFIG_A733WIFI_PRIORITY})
endif()
