#
# Meterfs
# Copyright 2012 Pawel Pisarczyk
#


SRCS = spi.c flash.c node.c meterfs.c
OBJS = $(SRCS:.c=.o)
BIN = meterfs

all: meterfs

.c.o:
	$(CC) -c $(CFLAGS) $<

$(OBJS):

meterfs: $(OBJS) $(LDLIBS)
	$(LD) $(LDFLAGS) -o $(BIN) $(OBJS) $(LDLIBS)

clean:
	rm -f *.o *~ core $(BIN)
