#
# Makefile for Phoenix-RTOS EXT2 filesystem
#
# Copyright 2018, 2020 Phoenix Systems
#

EXT2_OBJS := block.o dir.o ext2.o file.o gdt.o inode.o libext2.o obj.o sb.o

$(PREFIX_A)libext2.a: $(addprefix $(PREFIX_O)ext2/, $(EXT2_OBJS))
	$(ARCH)

$(PREFIX_H)libext2.h: ext2/libext2.h
	$(HEADER)

all: $(PREFIX_A)libext2.a $(PREFIX_H)libext2.h
