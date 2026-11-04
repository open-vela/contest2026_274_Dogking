if(CONFIG_SYSTEM_A733FTPD)
  nuttx_add_application(NAME ftpd SRCS a733ftpd_main.c
                        STACKSIZE 8192 PRIORITY 100)
endif()
