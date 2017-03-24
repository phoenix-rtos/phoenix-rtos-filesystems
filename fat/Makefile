#
# Misc. FAT
# Copyright 2012 Pawel Pisarczyk
#

CC = gcc
LD = gcc
CFLAGS = -c -Wall -I . -O2 -g -m32 -D_FILE_OFFSET_BITS=64
LDFLAGS = -lm -m32 -D_FILE_OFFSET_BITS=64

SRCS = fat.c fatfat.c fatdev.c fatio.c pcache.c
OBJS = $(SRCS:.c=.o)
BIN = fat

all: fat

.c.o:
	$(CC) $(CFLAGS) $<

$(OBJS): fat.h fatfat.h fatdev.h fatio.h pcache.h

fat: $(OBJS)
	$(LD) $(LDFLAGS) -o $(BIN) $(OBJS)

clean:
	rm -f *.o *~ core
