#
# Makefile for Phoenix-RTOS meterfs filesystem
#
# Copyright 2018 Phoenix Systems
#

METERFS_OBJS := spi.o flash.o node.o meterfs.o

$(PREFIX_PROG)meterfs: $(addprefix $(PREFIX_O)meterfs/, $(METERFS_OBJS))
	$(LINK)

all: $(PREFIX_PROG_STRIPPED)meterfs
