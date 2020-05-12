#
# Makefile for Phoenix-RTOS ext2 filesystem
#
# Copyright 2018 Phoenix Systems
#

EXT2_OBJS := ext2.o inode.o block.o file.o dir.o object.o sb.o

$(PREFIX_A)libext2.a: $(addprefix $(PREFIX_O)ext2/, $(EXT2_OBJS))
	$(ARCH)

$(PREFIX_H)libext2.h: ext2/libext2.h
	$(HEADER)

all: $(PREFIX_A)libext2.a $(PREFIX_H)libext2.h