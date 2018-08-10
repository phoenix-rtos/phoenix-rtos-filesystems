#
# Phoenix-RTOS
#
# jffs2 - jffs2 filesystem driver
#
# Copyright 2018 Phoenix Systems
# Author: Kamil Amanowicz
#


CFLAGS += -Wno-pointer-sign -Wno-discarded-qualifiers -Wno-unused-value -Wno-strict-prototypes\
	-DCONFIG_JFFS2_CMODE_NONE -DCONFIG_JFFS2_FS_WRITEBUFFER -DCONFIG_JFFS2_SUMMARY

LDFLAGS += -L../../phoenix-rtos-devices/build/arm-imx/lib

#linux base jffs2 files + writebuffer for nand support + summary for faster mount times
PROGS_SRCS := compr.c dir.c file.c ioctl.c nodelist.c malloc.c read.c nodemgmt.c \
	   readinode.c write.c scan.c gc.c symlink.c build.c erase.c \
	   background.c fs.c writev.c super.c debug.c wbuf.c summary.c

#phoenix specific files
PROGS_SRCS += os-phoenix/fs.c os-phoenix/completion.c os-phoenix/slab.c \
		os-phoenix/dev.c os-phoenix/crc32.c os-phoenix/rb.c os-phoenix/dentry.c \
		os-phoenix/wait.c os-phoenix/os-phoenix.c os-phoenix/locks.c jffs2.c \
		os-phoenix/object.c

ifeq ($(MTD_EM), 1)
	PROGS_SRCS += os-phoenix/mtd_em.c
else
	PROGS_SRCS += os-phoenix/mtd.c
endif

#rest of additional jffs2 options (not supported)
ifdef $(CONFIG_JFFS2_FS_XATTR)
	PROGS_SRCS += xattr.c xattr_trusted.c xattr_user.c
endif

ifdef $(CONFIG_JFFS2_FS_SECURITY)
	PROGS_SRCS += security.c
endif

ifdef $(CONFIG_JFFS2_FS_POSIX_ACL)
	PROGS_SRCS += acl.c
endif

ifdef $(CONFIG_JFFS2_RUBIN)
	PROGS_SRCS += compr_rubin.c
endif

ifdef $(CONFIG_JFFS2_RTIME)
	PROGS_SRCS += compr_rtime.c
endif

ifdef $(CONFIG_JFFS2_ZLIB)
	PROGS_SRCS += compr_zlib.c
endif

ifdef $(CONFIG_JFFS2_LZO)
	PROGS_SRCS += compr_lzo.c
endif

PROGS := jffs2
LDLIBS += -lflashdrv

#include after all dependencies are set
include $(TOPDIR)/Makefile.rules

