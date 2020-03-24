#
# Makefile for phoenix-rtos-filesystems
#
# Copyright 2019 Phoenix Systems
#
# %LICENSE%
#

SIL ?= @
MAKEFLAGS += --no-print-directory

TARGET ?= ia32-generic
#TARGET ?= armv7m4-stm32l4x6
#TARGET ?= armv7m7-imxrt105x
#TARGET ?= armv7a7-imx6ull
#TARGET ?= riscv64-spike

include ../phoenix-rtos-build/Makefile.common
include ../phoenix-rtos-build/Makefile.$(TARGET_SUFF)

CFLAGS += -I"$(PREFIX_H)"
LDFLAGS += -L"$(PREFIX_A)"

.PHONY: clean
clean:
	@echo "rm -rf $(BUILD_DIR)"

ifneq ($(filter clean,$(MAKECMDGOALS)),)
	$(shell rm -rf $(BUILD_DIR))
endif

include dummyfs/Makefile
include jffs2/Makefile
include ext2/Makefile
#include meterfs/Makefile


T1 := $(filter-out clean all,$(MAKECMDGOALS))
ifneq ($(T1),)
	include $(T1)/Makefile
.PHONY: $(T1)
$(T1):
	@echo >/dev/null
else
#	include Makefile.$(TARGET_SUBFAMILY)
endif
