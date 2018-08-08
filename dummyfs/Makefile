#
# Makefile for Phoenix-RTOS dummyfs
#
# Copyright 2017, 2018 Phoenix Systems
#

PROGS_SRCS := dummyfs.c file.c dir.c object.c dev.c

ifneq (, $(findstring arm-imx, $(TARGET)))
	PROGS_SRCS += usb.c
endif

PROGS := dummyfs

#include after all dependencies are set
include $(TOPDIR)/Makefile.rules

