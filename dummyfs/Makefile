#
# Makefile for Phoenix-RTOS dummyfs
#
# Copyright 2017, 2018 Phoenix Systems
#

SRCS = dummyfs.c file.c dir.c object.c dev.c

ifneq (, $(findstring arm-imx, $(TARGET)))
	SRCS += usb.c
endif

OBJS = $(SRCS:.c=.o)
BIN = dummyfs

all: check $(BIN)


check:
	@(if test -z "$(TARGET)"; then\
		echo "Error: Only main Makefile can be used for compilation!";\
		exit 1;\
	fi;)


.c.o:
	@(printf " CC    %-24s  \n" "$<"; $(CC) -c $(CFLAGS) $<)

-include .depend

$(BIN): $(OBJS) $(LIBS)
	@echo " LD    $@"
	$(SIL)$(LD) $(LDFLAGS) -o $(BIN)_unstripped $^ $(LDLIBS)
	@echo " STRIP $@"
	$(SIL)$(STRIP) $(BIN)_unstripped -o $(BIN) 2>/dev/null


depend: check
	$(SIL)$(MKDEP) $(MKDEPFLAGS) $(SRCS) >.depend


clean: check
	$(SIL)rm -f core *.o $(ARCH)


.PHONY: clean
# DO NOT DELETE
