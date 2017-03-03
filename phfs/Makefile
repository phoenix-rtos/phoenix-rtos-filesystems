#
# Makefile for Phoenix-RTOS kernel (fs/phfs)
#
# Copyright 2012-2015 Phoenix Systems
#

SRCS = phfs.c phfs_msg.c  phfs_serial.c phfs_udp.c

OBJS = $(SRCS:.c=.o)


all: check $(ARCH)


check:
	@(if test -z "$(ARCH)"; then\
		echo "Error: Only main Makefile can be used for compilation!";\
		exit 1;\
	fi;)


.c.o:
	@(echo "  CC $<"; $(CC) -c $(CFLAGS) $<)


-include .depend


$(ARCH): $(OBJS)
	$(SIL)$(AR) $(ARFLAGS) $(ARCH) $(OBJS) 2>/dev/null


depend: check
	$(SIL)$(MKDEP) $(MKDEPFLAGS) $(SRCS) >.depend


clean: check
	@rm -f core *.o $(ARCH)
