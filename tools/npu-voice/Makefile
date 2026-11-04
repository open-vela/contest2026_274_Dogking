# SPDX-License-Identifier: Apache-2.0
CC = aarch64-linux-gnu-gcc
SDK ?= ../../../a733-ai-sdk-reference/viplite-tina/v2.0
OUT ?= ../../out/npu-voice
SYSROOT ?=
BINARY ?= voice-golden-linux-aarch64
ifneq ($(SYSROOT),)
SYSFLAGS = --sysroot=$(SYSROOT) -isystem $(SYSROOT)/usr/aarch64-linux-gnu/include \
           -B$(SYSROOT)/usr/aarch64-linux-gnu/lib -L$(SYSROOT)/usr/aarch64-linux-gnu/lib
endif
# Do not copy or redistribute vendor shared objects from this build.
all:
	mkdir -p $(OUT)
	$(CC) $(SYSFLAGS) -std=c11 -D_POSIX_C_SOURCE=200809L -O2 -g -Wall -Wextra -Werror \
	  $(EXTRA_CFLAGS) \
	  -I$(SDK)/inc voice-golden.c -L$(SDK) -Wl,-rpath,'$$ORIGIN/lib' \
	  -lNBGlinker -lVIPhal -lm -lpthread -ldl -o $(OUT)/$(BINARY)
.PHONY: all
