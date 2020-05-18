#
# Makefile for Phoenix-RTOS partition table
#
# Copyright 2020 Phoenix Systems
#


$(PREFIX_A)libptable.a: $(addprefix $(PREFIX_O)ptable/, ptable.o)
	$(ARCH)

$(PREFIX_H)ptable.h: ptable/ptable.h 
	$(HEADER)

all: $(PREFIX_A)libptable.a $(PREFIX_H)ptable.h

