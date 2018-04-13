#
# Phoenix-RTOS
#
# jffs2 - jffs2 filesystem driver
#
# Copyright 2018 Phoenix Systems
# Author: Kamil Amanowicz
#

CC = arm-phoenix-gcc
CFLAGS = -mthumb -c -Wall -Wno-pointer-sign -Wno-discarded-qualifiers -Wno-unused-value -Os -nostartfiles -nostdlib -mno-unaligned-access -mcpu=cortex-a7 -mtune=cortex-a7 -mfloat-abi=hard -fomit-frame-pointer -ffreestanding -I. -I../../libphoenix/ -I../../phoenix-rtos-kernel/src/ -DARCH=\"arch/arm-imx/arch.h\" -DCONFIG_JFFS2_CMODE_NONE -DCONFIG_JFFS2_FS_WRITEBUFFER


LD = arm-phoenix-gcc
LDFLAGS = -nostdlib -z max-page-size=0x1000


STRIP = arm-phoenix-strip


SRCS = compr.c dir.c file.c ioctl.c nodelist.c malloc.c read.c nodemgmt.c \
	   readinode.c write.c scan.c gc.c symlink.c build.c erase.c \
	   background.c fs.c writev.c debug.c wbuf.c

#phoenix specific files
SRCS += os-phoenix/fs.c os-phoenix/completion.c os-phoenix/mtd.c os-phoenix/slab.c \
		os-phoenix/dev.c os-phoenix/crc32.c os-phoenix/rb.c os-phoenix/dentry.c \
		os-phoenix/wait.c os-phoenix/os-phoenix.c os-phoenix/locks.c jffs2.c


ifdef $(CONFIG_JFFS2_FS_XATTR)
	SRCS += xattr.c xattr_trusted.c xattr_user.c
endif

ifdef $(CONFIG_JFFS2_FS_SECURITY)
	SRCS += security.c
endif

ifdef $(CONFIG_JFFS2_FS_POSIX_ACL)
	SRCS += acl.c
endif

ifdef $(CONFIG_JFFS2_RUBIN)
	SRCS += compr_rubin.c
endif

ifdef $(CONFIG_JFFS2_RTIME)
	SRCS += compr_rtime.c
endif

ifdef $(CONFIG_JFFS2_ZLIB)
	SRCS += compr_zlib.c
endif

ifdef $(CONFIG_JFFS2_LZO)
	SRCS += compr_lzo.c
endif

ifdef $(CONFIG_JFFS2_SUMMARY)
	SRCS += summary.c
endif

OBJS = $(SRCS:.c=.o)

LIBPHOENIX = ../../libphoenix/libphoenix.a
GCCLIB := $(shell $(CC) $(CFLAGS) -print-libgcc-file-name)
BIN = jffs2

all: jffs2 jffs2.debug

%.c%.o:
	$(CC) $(CFLAGS) $<

jffs2: $(OBJS) $(LIBPHOENIX)
	$(LD) $(LDFLAGS) -o $(BIN) $(OBJS) $(LIBPHOENIX) $(GCCLIB)
	$(STRIP) $(BIN)

jffs2.debug: $(OBJS) $(LIBPHOENIX)
	$(LD) $(LDFLAGS) -o $(BIN).debug $(OBJS) $(LIBPHOENIX) $(GCCLIB)

clean:
	rm -f *.o os-phoenix/*.o *~ core
