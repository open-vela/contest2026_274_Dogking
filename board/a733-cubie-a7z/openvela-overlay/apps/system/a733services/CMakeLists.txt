if(CONFIG_SYSTEM_A733SERVICES)
  nuttx_add_application(NAME service SRCS a733services_main.c services_config.c
                        STACKSIZE 8192 PRIORITY 100)
endif()
