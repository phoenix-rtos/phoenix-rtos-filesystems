#
# Misc. FAT
# Copyright 2012 Pawel Pisarczyk
#

.PHONY: all ph2 ph2-clean

CC = gcc
LD = gcc
CFLAGS = -c -Wall -I . -O2 -g -m32 -D_FILE_OFFSET_BITS=64
LDFLAGS = -lm -m32 -D_FILE_OFFSET_BITS=64

PH2_DIR ?= ""
SRCS = fat.c fatfat.c fatdev.c fatio.c pcache.c
PH2_SRCS = fatfat.c fatio.c pcache.c fatfat.h fatio.h pcache.h
OBJS = $(SRCS:.c=.o)
BIN = fat

all: fat

ph2: $(PH2_SRCS:%=$(PH2_DIR)/%)

ph2-clean:
	rm $(PH2_SRCS:%=$(PH2_DIR)/%)

$(PH2_DIR)/%.h : %.h
	cp $? $@

$(PH2_DIR)/%.c : %.c
	cp $? $@

.c.o:
	$(CC) $(CFLAGS) $<

$(OBJS): fat.h fatfat.h fatdev.h fatio.h pcache.h

fat: $(OBJS)
	$(LD) $(LDFLAGS) -o $(BIN) $(OBJS)

clean:
	rm -f *.o *~ core
