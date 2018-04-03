#
# Makefile for Phoenix-RTOS ext2
#
# Copyright 2017, 2018 Phoenix Systems
#

SRCS = ext2.c mbr.c inode.c block.c file.c pc-ata.c dir.c object.c sb.c
OBJS = $(SRCS:.c=.o)
BIN = ext2

CFLAGS += -I. -I../../libphoenix/ -DARCH=\"arch/ia32/arch.h\" -I../../phoenix-rtos-devices/pci/pc-pci
#CFLAGS += -mthumb


all: check $(BIN)


check:
	@(if test -z "$(TARGET)"; then\
		echo "Error: Only main Makefile can be used for compilation!";\
		exit 1;\
	fi;)


.c.o:
	@(printf " CC    %-24s  \n" "$<"; $(CC) -c $(CFLAGS) $<)

-include .depend


$(BIN): $(OBJS)
	@echo " LD    $@"
	$(SIL)$(LD) -o $(BIN) $(OBJS) ../../libphoenix/libphoenix.a $(GCCLIB)
	@echo " STRIP $@"
	$(SIL)$(STRIP) $(BIN) 2>/dev/null


depend: check
	$(SIL)$(MKDEP) $(MKDEPFLAGS) $(SRCS) >.depend


clean: check
	$(SIL)rm -f core *.o $(ARCH)


.PHONY: clean
# DO NOT DELETE
