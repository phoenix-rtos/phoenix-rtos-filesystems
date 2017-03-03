#
# Misc. FAT
# Copyright 2012 Pawel Pisarczyk
#

CC = gcc
LD = gcc
CFLAGS = -c -Wall -I . -O2 -g
LDFLAGS = -lm

SRCS = fat.c fatfat.c fatdev.c
OBJS = $(SRCS:.c=.o)
BIN = fat

all: fat

.c.o:
	$(CC) $(CFLAGS) $<

$(OBJS): fat.h fatfat.h fatdev.h

fat: $(OBJS)
	$(LD) $(LDFLAGS) -o $(BIN) $(OBJS)

clean:
	rm -f *.o *~ core
