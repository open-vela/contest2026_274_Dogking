# SPDX-License-Identifier: Apache-2.0
if(CONFIG_SYSTEM_A733STORAGE)
  nuttx_add_application(NAME ufs SRCS a733storage_main.c a733storage_boot.c
                        STACKSIZE 8192 PRIORITY 100)
endif()
