#
# Meterfs
# Copyright 2012 Pawel Pisarczyk
#


SRCS = test.c
OBJS = $(SRCS:.c=.o)
BIN = meterfs-test

all: meterfs-test

.c.o:
	$(CC) -c $(CFLAGS) $<

$(OBJS):

meterfs-test: $(OBJS) $(LDLIBS)
	$(LD) $(LDFLAGS) -o $(BIN) $(OBJS) $(LDLIBS)

clean:
	rm -f *.o *~ core $(BIN)
