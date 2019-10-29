#
# Makefile for Phoenix-RTOS dummyfs
#
# Copyright 2017, 2018 Phoenix Systems
#

DUMMYFS_OBJS := dummyfs.o file.o dir.o object.o

$(PREFIX_PROG)dummyfs: $(addprefix $(PREFIX_O)dummyfs/, $(DUMMYFS_OBJS))
	$(LINK)

all: $(PREFIX_PROG_STRIPPED)dummyfs
