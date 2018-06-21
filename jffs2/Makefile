#
# Phoenix-RTOS
#
# jffs2 - jffs2 filesystem driver
#
# Copyright 2018 Phoenix Systems
# Author: Kamil Amanowicz
#



CFLAGS += -Wno-pointer-sign -Wno-discarded-qualifiers -Wno-unused-value -Wno-strict-prototypes\
	-DCONFIG_JFFS2_CMODE_NONE -DCONFIG_JFFS2_FS_WRITEBUFFER -DLIBFLASHDRV
#-DJFFS2_DBG_READINODE_MESSAGES

LIBS := flashdrv.o

#linux base jffs2 files + writebuffer for nand support
SRCS = compr.c dir.c file.c ioctl.c nodelist.c malloc.c read.c nodemgmt.c \
	   readinode.c write.c scan.c gc.c symlink.c build.c erase.c \
	   background.c fs.c writev.c super.c debug.c wbuf.c

#phoenix specific files
SRCS += os-phoenix/fs.c os-phoenix/completion.c os-phoenix/slab.c \
		os-phoenix/dev.c os-phoenix/crc32.c os-phoenix/rb.c os-phoenix/dentry.c \
		os-phoenix/wait.c os-phoenix/os-phoenix.c os-phoenix/locks.c jffs2.c \
		os-phoenix/object.c

ifeq ($(MTD_EM), 1)
	SRCS += os-phoenix/mtd_em.c
else
	SRCS += os-phoenix/mtd.c
endif

#rest of additional jffs2 options (not supported)
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

BIN = jffs2

all: check $(BIN)


check:
	@(if test -z "$(TARGET)"; then\
		echo "Error: Only main Makefile can be used for compilation!";\
		exit 1;\
	fi;)


.c.o:
	@(printf " CC    %-24s  \n" "$<"; $(CC) -c $(CFLAGS) $< -o $@)

-include .depend

.c.o:
flashdrv.o: ../../phoenix-rtos-devices/storage/imx6ull-flash/flashdrv.c
	@(printf " CC    %-24s  \n" "$<"; $(CC) -c $(CFLAGS) "$<" -o "$@")

jffs2: $(OBJS) $(LIBS)
	$(SIL)$(LD) $(LDFLAGS) -o $(BIN)_unstripped $^ $(LDLIBS)
	$(STRIP) $(BIN)_unstripped -o $(BIN)

clean:
	$(SIL)rm -f *.o os-phoenix/*.o *~ core
