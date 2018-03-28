#
# Phoenix-RTOS
#
# dummyfs - dummy filesystem driver
#
# Copyright 2018 Phoenix Systems
# Author: Pawel Pisarczyk
#

CC = arm-phoenix-gcc
CFLAGS = -mthumb -c -Wall -Os -nostartfiles -mno-unaligned-access -nostdlib -mcpu=cortex-a7 -mtune=cortex-a7 -mfloat-abi=hard -fomit-frame-pointer -ffreestanding -I. -I../../libphoenix/ -DARCH=\"arch/arm-imx/arch.h\"

LD = arm-phoenix-gcc
LDFLAGS = -nostdlib -z max-page-size=0x1000

STRIP = arm-phoenix-strip

SRCS = dummyfs.c file.c dir.c object.c usb.c
OBJS = $(SRCS:.c=.o)

LIBPHOENIX = ../../libphoenix/libphoenix.a
GCCLIB := $(shell $(CC) $(CFLAGS) -print-libgcc-file-name)
BIN = dummyfs

all: dummyfs dummyfs.debug

.c.o:
	$(CC) $(CFLAGS) $<

dummyfs: $(OBJS) $(LIBPHOENIX)
	$(LD) $(LDFLAGS) -o $(BIN) $(OBJS) $(LIBPHOENIX) $(GCCLIB)
	$(STRIP) $(BIN) -o $(BIN)

dummyfs.debug: $(OBJS) $(LIBPHOENIX)
	$(LD) $(LDFLAGS) -o $(BIN).debug $(OBJS) $(LIBPHOENIX) $(GCCLIB)

clean:
	rm -f *.o *~ core
