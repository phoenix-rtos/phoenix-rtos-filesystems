#
# Makefile for Phoenix-RTOS ext2 filesystem
#
# Copyright 2018 Phoenix Systems
#

EXT2_OBJS := ext2.o mbr.o inode.o block.o file.o pc-ata.o dir.o object.o sb.o

$(PREFIX_PROG)ext2: $(addprefix $(PREFIX_O)ext2/, $(EXT2_OBJS))
	$(LINK)

all: $(PREFIX_PROG_STRIPPED)ext2
