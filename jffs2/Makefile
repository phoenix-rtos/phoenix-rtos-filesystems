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

#linux base jffs2 files + writebuffer for nand support + summary for faster mount times
LIB_SRCS := compr.c dir.c file.c ioctl.c nodelist.c malloc.c read.c nodemgmt.c \
	   readinode.c write.c scan.c gc.c symlink.c build.c erase.c \
	   background.c fs.c writev.c super.c debug.c wbuf.c summary.c

#phoenix specific files
LIB_SRCS += os-phoenix/fs.c os-phoenix/completion.c os-phoenix/slab.c \
		os-phoenix/dev.c os-phoenix/crc32.c os-phoenix/rb.c os-phoenix/dentry.c \
		os-phoenix/wait.c os-phoenix/os-phoenix.c os-phoenix/locks.c jffs2.c \
		os-phoenix/object.c

ifeq ($(MTD_EM), 1)
	LIB_SRCS += os-phoenix/mtd_em.c
else
	LIB_SRCS += os-phoenix/mtd.c
endif

#rest of additional jffs2 options (not supported)
ifdef $(CONFIG_JFFS2_FS_XATTR)
	LIB_SRCS += xattr.c xattr_trusted.c xattr_user.c
endif

ifdef $(CONFIG_JFFS2_FS_SECURITY)
	LIB_SRCS += security.c
endif

ifdef $(CONFIG_JFFS2_FS_POSIX_ACL)
	LIB_SRCS += acl.c
endif

ifdef $(CONFIG_JFFS2_RUBIN)
	LIB_SRCS += compr_rubin.c
endif

ifdef $(CONFIG_JFFS2_RTIME)
	LIB_SRCS += compr_rtime.c
endif

ifdef $(CONFIG_JFFS2_ZLIB)
	LIB_SRCS += compr_zlib.c
endif

ifdef $(CONFIG_JFFS2_LZO)
	LIB_SRCS += compr_lzo.c
endif

LIB := libjffs2.a
LDLIBS += -lflashdrv
INSTALL_HDRS := libjffs2.h

#include after all dependencies are set
include $(TOPDIR)/Makefile.rules

