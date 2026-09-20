############################################################################
# vendor/allwinnertech/boards/a733/cubie-a7z/src/Makefile
#
# SPDX-License-Identifier: Apache-2.0
############################################################################

include $(APPDIR)/Make.defs

CSRCS = a7z_boardinit.c

ifneq ($(filter y,$(CONFIG_BOARDCTL_RESET) $(CONFIG_BOARDCTL_POWEROFF)),)
CSRCS += a7z_power.c
endif

ifeq ($(CONFIG_BOARD_A7Z_USER_LED),y)
CSRCS += a7z_gpio.c
endif

ifeq ($(CONFIG_BOARD_A7Z_STORAGE),y)
CSRCS += a7z_storage.c
endif

include $(APPDIR)/Application.mk
