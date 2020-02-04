#
# Makefile for Phoenix-RTOS meterfs filesystem
#
# Copyright 2018 Phoenix Systems
#

METERFS_OBJS := spi.o flash.o node.o meterfs.o

$(PREFIX_PROG)meterfs: $(addprefix $(PREFIX_O)meterfs/, $(METERFS_OBJS))
	$(LINK)

$(PREFIX_H)meterfs.h: meterfs/meterfs.h
	$(HEADER)

all: $(PREFIX_PROG_STRIPPED)meterfs $(PREFIX_H)meterfs.h
