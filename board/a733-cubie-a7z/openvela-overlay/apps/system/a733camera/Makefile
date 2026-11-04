# SPDX-License-Identifier: Apache-2.0
include $(APPDIR)/Make.defs
PROGNAME = camera
PRIORITY = 100
STACKSIZE = 8192
MODULE = $(CONFIG_SYSTEM_A733CAMERA)
MAINSRC = a733camera_main.c
CSRCS = a733_camera_image.c
ifeq ($(CONFIG_A733_CAMERA_LIVE_TEST),y)
CSRCS += a733_camera_live.c
endif
include $(APPDIR)/Application.mk
