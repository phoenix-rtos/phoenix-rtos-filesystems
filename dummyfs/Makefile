#
# Makefile for Phoenix-RTOS dummyfs
#
# Copyright 2017, 2018 Phoenix Systems
#

DUMMYFS_OBJS := dummyfs.o file.o dir.o object.o dev.o

ifneq (, $(findstring arm-imx, $(TARGET)))
	DUMMYFS_OBJS += usb.o
endif

$(PREFIX_PROG)dummyfs: $(addprefix $(PREFIX_O)dummyfs/, $(DUMMYFS_OBJS))
	$(LINK)

all: $(PREFIX_PROG_STRIPPED)dummyfs
