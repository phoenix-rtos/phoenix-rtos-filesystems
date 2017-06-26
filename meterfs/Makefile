#
# Misc. FAT
# Copyright 2012 Pawel Pisarczyk
#

CC = i386-pc-phoenix-gcc
CFLAGS = -c -Wall -I. -I../../libphoenix -O2 -g -DARCH=\"arch/ia32/arch.h\" -nostartfiles -nostdlib -fno-builtin

LD = i386-pc-phoenix-gcc
LDFLAGS = -nostartfiles -nostdlib

GCCLIB := $(shell $(CC) $(CFLAGS) -print-libgcc-file-name)

SRCS = meterfs.c
OBJS = $(SRCS:.c=.o)
BIN = meterfs

all: meterfs

.c.o:
	$(CC) $(CFLAGS) $<

$(OBJS):

meterfs: $(OBJS)
	$(LD) $(LDFLAGS) -o $(BIN) $(OBJS) ../../libphoenix/libphoenix.a $(GCCLIB)

clean:
	rm -f *.o *~ core
