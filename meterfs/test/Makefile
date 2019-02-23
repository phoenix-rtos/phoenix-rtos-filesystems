#
# Makefile for Phoenix-RTOS meterfs test
#
# Copyright 2018 Phoenix Systems
#

METERFS_OBJS := test.o

$(PREFIX_PROG)meterfs-test: $(addprefix $(PREFIX_O)meterfs/test/, $(METERFS_OBJS))
	$(LINK)

all: $(PREFIX_PROG_STRIPPED)meterfs-test
