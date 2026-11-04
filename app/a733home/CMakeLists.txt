if(CONFIG_SYSTEM_A733HOME)
  nuttx_add_application(NAME home SRCS a733home_main.c home_protocol.c
                        STACKSIZE 16384 PRIORITY 100)
endif()
