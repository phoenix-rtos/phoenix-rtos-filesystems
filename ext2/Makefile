#
# Phoenix-RTOS
#
# descriptions
#
# Copyright 2017 Phoenix Systems
# Author: Pawel Pisarczyk
#
CROSS ?= i386-phoenix-

CC = $(CROSS)gcc
CFLAGS = -c -Wall -O2 -g -I. -I../../libphoenix/ -DARCH=\"arch/ia32/arch.h\"\
			-DHAL=\"hal//ia32//hal.h\" \
                        -I../../phoenix-rtos-kernel/src/\
		     	-I../../phoenix-rtos-devices/bus/pci/pc-pci/

LD = $(CROSS)gcc
LDFLAGS = -nostdlib -nostartfiles
#-z max-page-size=0x10000

STRIP = $(CROSS)strip

SRCS = ext2.c mbr.c inode.c block.c file.c pc-ata.c dir.c object.c sb.c
OBJS = $(SRCS:.c=.o)
GCCLIB := $(shell $(CC) $(CFLAGS) -print-libgcc-file-name)
BIN = ext2

all: ext2

.c.o:
	$(CC) $(CFLAGS) $<

ext2: $(OBJS)
	$(LD) $(LDFLAGS) -o $(BIN) $(OBJS) ../../libphoenix/libphoenix.a $(GCCLIB)
	$(STRIP) $(BIN)

clean:
	rm -f *.o *~ core $(BIN)
