#
# Phoenix-RTOS
#
# dummyfs - dummy filesystem driver
#
# Copyright 2018 Phoenix Systems
# Author: Pawel Pisarczyk
#

CC = i386-pc-phoenix-gcc
CFLAGS = -c -Wall -O2 -g -I. -I../../libphoenix/ -DARCH=\"arch/ia32/arch.h\"

LD = i386-pc-phoenix-gcc
LDFLAGS = -nostartfiles -nostdlib

STRIP = i386-pc-phoenix-strip

SRCS = dummyfs.c file.c dir.c object.c
OBJS = $(SRCS:.c=.o)

LIBPHOENIX = ../../libphoenix/libphoenix.a
GCCLIB := $(shell $(CC) $(CFLAGS) -print-libgcc-file-name)
BIN = dummyfs

all: dummyfs

.c.o:
	$(CC) $(CFLAGS) $<

dummyfs: $(OBJS) $(LIBPHOENIX)
	$(LD) $(LDFLAGS) -o $(BIN) $(OBJS) $(LIBPHOENIX) $(GCCLIB)
	$(STRIP) $(BIN)

clean:
	rm -f *.o *~ core
