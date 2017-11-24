#
# Misc. FAT
# Copyright 2012 Pawel Pisarczyk
#

#CFLAGS = -c -Wall -I. -I../../libphoenix -O2 -g -DARCH=\"arch/ia32/arch.h\" -nostartfiles -nostdlib -fno-builtin
CC = arm-phoenix-gcc
LIBPHOENIXDIR := "$(SRCDIR)../../libphoenix/"

ifeq ($(DEBUG), 1)
	CFLAGS = -O0 -g
else
	CFLAGS = -O2 -DNDEBUG
endif

CFLAGS += -Wall -Wstrict-prototypes -I$(LIBPHOENIXDIR) -nostartfiles -nostdlib -fomit-frame-pointer \
	-ffreestanding -DARCH=\"arch/armv7/arch.h\" -fpic -msingle-pic-base -mno-pic-data-is-text-relative \
	-mcpu=cortex-m3 -mthumb

LDFLAGS = -nostdlib -e _start --section-start .text=0 -Tbss=20000000 -z max-page-size=0x10
LD = arm-phoenix-ld

GCCLIB := $(shell $(CC) $(CFLAGS) -print-libgcc-file-name)

SRCS = meterfs.c spi.c flash.c opened.c
OBJS = $(SRCS:.c=.o)
BIN = meterfs

all: meterfs

.c.o:
	$(CC) -c $(CFLAGS) $<

$(OBJS):

meterfs: $(OBJS) ../../libphoenix/libphoenix.a
	$(LD) $(LDFLAGS) -o $(BIN) $(OBJS) ../../libphoenix/libphoenix.a $(GCCLIB)

clean:
	rm -f *.o *~ core $(BIN)
