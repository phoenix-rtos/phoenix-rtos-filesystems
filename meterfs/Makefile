#
# Makefile for Phoenix-RTOS meterfs filesystem
#
# Copyright 2020 Phoenix Systems
#


$(PREFIX_A)meterfs.a: $(addprefix $(PREFIX_O)meterfs/, meterfs.o node.o)
	$(ARCH)

$(PREFIX_H)meterfs.h: meterfs/meterfs.h
	$(HEADER)

all: $(PREFIX_A)meterfs.a $(PREFIX_H)meterfs.h

