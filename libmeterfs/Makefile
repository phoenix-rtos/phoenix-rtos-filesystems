#
# Makefile for Phoenix-RTOS meterfs filesystem
#
# Copyright 2020 Phoenix Systems
#


$(PREFIX_A)libmeterfs.a: $(addprefix $(PREFIX_O)libmeterfs/, meterfs.o node.o)
	$(ARCH)

$(PREFIX_H)meterfs.h: libmeterfs/meterfs.h
	$(HEADER)

all: $(PREFIX_A)libmeterfs.a $(PREFIX_H)meterfs.h

